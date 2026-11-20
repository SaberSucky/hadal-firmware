#!/usr/bin/env python3
"""Pre-flight check of an update package, before it goes anywhere near a Flipper.

    python3 scripts/update_package_check.py PACKAGE [--expect light|full|any] [--reference PACKAGE]

PACKAGE is a flipper-z-f7-update-*.tgz or the folder inside it. Nothing here runs the update; it
checks the things that decide whether one can go wrong in a way that is hard to undo, by working
them out again from the files instead of trusting what the packager wrote:

  - the manifest is the right kind and every file it names is there with the CRC it states
  - the firmware image is a DFU file that writes only below the radio stack, its vector table is
    sane (stack pointer in RAM, reset vector inside the image) and its own CRC is right
  - the updater image is small enough for the bootloader and has a sane vector table too
  - the radio image is a real stack image, its version and flash address are what the manifest says
  - the option bytes the updater will compare and write are exactly what the named option-byte file
    makes, and the radio's flash address agrees with the secure flash start address (SFSA) in them
  - the resources archive unpacks to plain relative paths, and every app in it was built for an API
    the firmware provides (the Flipper refuses to load one that was not)

--expect light also requires the standard configuration: the stock Light radio stack with the
stock option bytes (scripts/ob.data), the one every Unleashed or official update uses. --expect
full is the same for the Full stack and scripts/ob_custradio.data, the recipe documented in
documentation/OTA.md. --reference compares the radio and option-byte settings with another
package (say a stock build) and reports any difference.

Exit status is 1 if anything failed.
"""

import argparse
import os
import struct
import sys
import tarfile
import tempfile
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import heatshrink2  # noqa: E402
from flipper.assets.coprobin import CoproBinary  # noqa: E402
from flipper.assets.heatshrink_stream import HeatshrinkDataStreamHeader  # noqa: E402
from flipper.assets.obdata import OptionBytesData  # noqa: E402

FLASH_BASE = 0x08000000
FLASH_END = 0x08100000
FLASH_PAGE = 0x1000
RAM_START, RAM_END = 0x20000000, 0x20030000  # SRAM1
UPDATER_SIZE_LIMIT = 128 * 1024
SFR_WORD = 14  # option byte word holding SFSA, see flipper/assets/obdata.py
STACK_TYPES = {1: "BLE_FULL", 2: "BLE_HCI", 3: "BLE_LIGHT", 4: "BLE_BEACON", 5: "BLE_BASIC"}

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
API_CSV = os.path.join(REPO, "targets", "f7", "api_symbols.csv")
FAP_MANIFEST_MAGIC = 0x52474448
STANDARD = {
    "light": ("BLE_LIGHT", "scripts/ob.data"),
    "full": ("BLE_FULL", "scripts/ob_custradio.data"),
}


class Report:
    def __init__(self):
        self.lines = []
        self.failed = False

    def ok(self, text):
        self.lines.append(("ok  ", text))

    def info(self, text):
        self.lines.append(("info", text))

    def fail(self, text):
        self.failed = True
        self.lines.append(("FAIL", text))

    def check(self, condition, good, bad):
        (self.ok if condition else self.fail)(good if condition else bad)
        return condition

    def print(self):
        for level, text in self.lines:
            print(f"[{level}] {text}")


def read_manifest(path):
    keys, header = {}, []
    with open(path) as f:
        for line in f:
            line = line.rstrip("\n")
            if not line or line.startswith("#"):
                continue
            key, _, value = line.partition(":")
            if not header and key == "Filetype":
                header.append(value.strip())
            keys[key.strip()] = value.strip()
    return keys


def hex_le(text):
    return bytes(int(x, 16) for x in text.split())


def le_int(text):
    return int.from_bytes(hex_le(text), "little") if text.strip() else 0


def words(text):
    data = hex_le(text)
    return struct.unpack("<%dI" % (len(data) // 4), data)


def parse_dfu(data):
    """(elements, target_label, crc_ok, vid, pid) of a DfuSe file."""
    sig, version, image_size, targets = struct.unpack("<5sBIB", data[:11])
    if sig != b"DfuSe":
        raise ValueError("not a DfuSe file")
    offset, elements = 11, []
    label = ""
    for _ in range(targets):
        tsig, alt, named, name, tsize, count = struct.unpack("<6sBI255sII", data[offset : offset + 274])
        label = name.split(b"\0")[0].decode("ascii", "replace")
        offset += 274
        for _ in range(count):
            address, size = struct.unpack("<II", data[offset : offset + 8])
            elements.append((address, data[offset + 8 : offset + 8 + size]))
            offset += 8 + size
    suffix = data[offset : offset + 16]
    bcd_device, pid, vid, bcd_dfu, ufd, length = struct.unpack("<HHHH3sB", suffix[:12])
    stored = struct.unpack("<I", suffix[12:16])[0]
    crc_ok = ufd == b"UFD" and stored == (~zlib.crc32(data[: offset + 12]) & 0xFFFFFFFF)
    return elements, label, crc_ok and offset + 16 == len(data), vid, pid


def vector_table_ok(image, limit, runs_from_ram=False):
    """Initial stack pointer in RAM and a Thumb reset vector inside the image. The updater is
    different: the bootloader copies it to RAM and runs it from there, so its vectors are RAM's."""
    if len(image) < 8:
        return False, "image shorter than a vector table"
    sp, reset = struct.unpack("<II", image[:8])
    if not RAM_START < sp <= RAM_END:
        return False, f"initial stack pointer 0x{sp:08X} is not in RAM"
    start = RAM_START if runs_from_ram else FLASH_BASE
    end = RAM_START + limit if runs_from_ram else FLASH_BASE + limit
    if not (reset & 1) or not start <= (reset & ~1) < end:
        where = "RAM image" if runs_from_ram else "image"
        return False, f"reset vector 0x{reset:08X} is not inside the {where}"
    return True, f"stack pointer 0x{sp:08X}, reset 0x{reset:08X}"


def firmware_api_version(csv_path=API_CSV):
    """(major, minor) the firmware exports, from its API table."""
    with open(csv_path) as f:
        for line in f:
            if line.startswith("Version,"):
                major, minor = line.split(",")[2].strip().split(".")[:2]
                return int(major), int(minor)
    raise ValueError(f"no Version line in {csv_path}")


def fap_manifest(data):
    """(api_major, api_minor, hardware_target) from the .fapmeta section of an app, or None."""
    if data[:4] != b"\x7fELF" or data[4] != 1:  # 32 bit ELF
        return None
    shoff, = struct.unpack_from("<I", data, 0x20)
    shentsize, shnum, shstrndx = struct.unpack_from("<HHH", data, 0x2E)
    if not shoff or shoff + shnum * shentsize > len(data):
        return None
    sections = [struct.unpack_from("<IIIIII", data, shoff + i * shentsize) for i in range(shnum)]
    names_offset = sections[shstrndx][4]
    for name_offset, _, _, _, offset, size in sections:
        start = names_offset + name_offset
        if data[start : data.index(b"\0", start)] == b".fapmeta" and size >= 14:
            magic, _version, minor, major, target = struct.unpack_from("<IIHHH", data, offset)
            return (major, minor, target) if magic == FAP_MANIFEST_MAGIC else None
    return None


def resource_manifest_problems(files, first_file):
    """Why the device could not use the archive's Manifest to skip unchanged files ([] if it can).
    files: name -> data of every regular file in the resources archive."""
    import hashlib

    problems = []
    if first_file != "Manifest":
        problems.append(f"the first file is {first_file!r}, not Manifest")
    manifest = files.get("Manifest")
    if manifest is None:
        return problems + ["there is no Manifest"]
    listed = {}
    for line in manifest.decode(errors="replace").splitlines():
        if line.startswith("F:"):
            parts = line.split(":", 3)
            if len(parts) == 4:
                listed[parts[3]] = (parts[1], int(parts[2]) if parts[2].isdigit() else -1)
    for name, data in files.items():
        if name == "Manifest":
            continue
        entry = listed.get(name)
        if entry is None:
            problems.append(f"{name} is not in the Manifest")
        elif entry != (hashlib.md5(data).hexdigest(), len(data)):
            problems.append(f"{name} differs from its Manifest entry")
    return problems


def read_resource_archive(path):
    """(names, {file name: data}, first file name) of a .ths resources archive"""
    with open(path, "rb") as f:
        header = HeatshrinkDataStreamHeader.unpack(f.read(7))
        body = f.read()
    tar_bytes = heatshrink2.decompress(body, window_sz2=header.window_size, lookahead_sz2=header.lookahead_size)
    with tempfile.NamedTemporaryFile(suffix=".tar") as tmp:
        tmp.write(tar_bytes)
        tmp.flush()
        with tarfile.open(tmp.name) as tar:
            names = tar.getnames()
            files = {m.name: tar.extractfile(m).read() for m in tar.getmembers() if m.isfile()}
            first_file = next((m.name for m in tar.getmembers() if m.isfile()), None)
    return names, files, first_file


def open_package(path, scratch):
    if os.path.isdir(path):
        return path
    with tarfile.open(path) as tar:
        names = tar.getnames()
        if any(n.startswith("/") or ".." in n.split("/") for n in names):
            raise ValueError("package contains unsafe paths")
        tar.extractall(scratch, filter="data")
    folders = [d for d in os.listdir(scratch) if os.path.isdir(os.path.join(scratch, d))]
    if len(folders) != 1:
        raise ValueError(f"expected one folder in the package, found {folders}")
    return os.path.join(scratch, folders[0])


def check_package(path, expect="any", ob_data=None, scratch=None, api_csv=API_CSV):
    report = Report()
    folder = open_package(path, scratch)
    manifest_path = os.path.join(folder, "update.fuf")
    if not report.check(os.path.isfile(manifest_path), "update.fuf present", "no update.fuf"):
        return report, {}
    m = read_manifest(manifest_path)
    report.check(
        m.get("Filetype") == "Flipper firmware upgrade configuration" and m.get("Version") == "2",
        "manifest is a version 2 upgrade configuration",
        f"unexpected manifest type {m.get('Filetype')!r} version {m.get('Version')!r}",
    )
    report.check(m.get("Target") == "7", "target is f7", f"target is {m.get('Target')}")

    def file_of(key):
        name = m.get(key, "")
        return os.path.join(folder, name) if name else None

    for key in ("Loader", "Firmware", "Radio", "Resources", "Splashscreen"):
        path_ = file_of(key)
        if path_:
            report.check(os.path.isfile(path_), f"{key} file {m[key]} present", f"{key} file {m[key]} is missing")

    # ---- updater (loader) ----
    stage = None
    if file_of("Loader") and os.path.isfile(file_of("Loader")):
        with open(file_of("Loader"), "rb") as f:
            stage = f.read()
        report.check(
            zlib.crc32(stage) & 0xFFFFFFFF == le_int(m["Loader CRC"]),
            "updater CRC matches the manifest",
            "updater CRC does not match the manifest",
        )
        report.check(
            len(stage) <= UPDATER_SIZE_LIMIT,
            f"updater is {len(stage)} bytes, within the {UPDATER_SIZE_LIMIT} the bootloader loads",
            f"updater is {len(stage)} bytes, over {UPDATER_SIZE_LIMIT}",
        )
        good, text = vector_table_ok(stage, len(stage), runs_from_ram=True)
        report.check(good, f"updater vector table (runs from RAM): {text}", f"updater vector table: {text}")

    # ---- radio ----
    radio_address = le_int(m.get("Radio address", ""))
    stack_name = None
    if file_of("Radio") and os.path.isfile(file_of("Radio")):
        with open(file_of("Radio"), "rb") as f:
            radio_data = f.read()
        report.check(
            zlib.crc32(radio_data) & 0xFFFFFFFF == le_int(m["Radio CRC"]),
            "radio CRC matches the manifest",
            "radio CRC does not match the manifest",
        )
        binary = CoproBinary(file_of("Radio"))
        if report.check(binary.is_valid() and binary.is_stack(), "radio image is a wireless stack", "radio image is not a stack image"):
            sig = binary.img_sig
            version = le_int(m["Radio version"])
            stack_type = (version >> 40) & 0xFF
            stack_name = STACK_TYPES.get(stack_type, f"type {stack_type}")
            expected_version = (
                sig.version_major
                | (sig.version_minor << 8)
                | (sig.version_sub << 16)
                | (sig.version_branch << 24)
                | (sig.version_build << 32)
            )
            report.check(
                version & 0xFFFFFFFFFF == expected_version,
                f"radio version {sig.version_major}.{sig.version_minor}.{sig.version_sub} agrees with the manifest ({stack_name})",
                "radio version in the manifest is not the one in the image",
            )
            report.check(
                binary.get_flash_load_addr() == radio_address,
                f"radio address 0x{radio_address:08X} is where the image says it belongs",
                f"radio address 0x{radio_address:08X} but the image belongs at 0x{binary.get_flash_load_addr():08X}",
            )
            report.check(
                radio_address % FLASH_PAGE == 0 and radio_address + len(radio_data) <= FLASH_END,
                "radio address is page aligned and the image fits in flash",
                "radio image does not fit in flash at that address",
            )
    else:
        report.info("no radio image in this package (the radio is left as it is)")

    # ---- firmware ----
    firmware_end = 0
    if file_of("Firmware") and os.path.isfile(file_of("Firmware")):
        with open(file_of("Firmware"), "rb") as f:
            dfu = f.read()
        try:
            elements, label, crc_ok, vid, pid = parse_dfu(dfu)
        except (ValueError, struct.error) as e:
            report.fail(f"firmware.dfu is not a valid DfuSe file: {e}")
            elements = []
        else:
            report.check(crc_ok, "firmware.dfu structure and CRC are valid", "firmware.dfu is damaged (CRC or size)")
            report.check((vid, pid) == (0x0483, 0xDF11), "DFU ids are the STM32 bootloader's", f"DFU ids are {vid:04X}:{pid:04X}")
        for address, data in elements:
            end = address + len(data)
            firmware_end = max(firmware_end, end)
            report.check(
                address == FLASH_BASE,
                f"firmware starts at 0x{address:08X}",
                f"firmware element starts at 0x{address:08X}, not 0x{FLASH_BASE:08X}",
            )
            good, text = vector_table_ok(data, len(data))
            report.check(good, f"firmware vector table: {text}", f"firmware vector table: {text}")
            if radio_address:
                spare = radio_address - end
                report.check(
                    spare >= 0,
                    f"firmware ends at 0x{end:08X}, {spare} bytes below the radio stack",
                    f"firmware ends at 0x{end:08X}, OVERLAPPING the radio stack at 0x{radio_address:08X}",
                )

    # ---- option bytes ----
    reference = words(m["OB reference"]) if m.get("OB reference") else ()
    mask = words(m["OB mask"]) if m.get("OB mask") else ()
    write_mask = words(m["OB write mask"]) if m.get("OB write mask") else ()
    wanted_ob = ob_data
    if not wanted_ob and expect in STANDARD:
        wanted_ob = os.path.join(REPO, STANDARD[expect][1])
    if reference and wanted_ob:
        values = OptionBytesData(wanted_ob).gen_values().export()
        same = (
            words(" ".join(f"{b:02X}" for b in values.reference)) == reference
            and words(" ".join(f"{b:02X}" for b in values.compare_mask)) == mask
            and words(" ".join(f"{b:02X}" for b in values.write_mask)) == write_mask
        )
        report.check(
            same,
            f"option bytes are exactly what {os.path.relpath(wanted_ob, REPO)} makes",
            f"option bytes in the manifest are NOT what {os.path.relpath(wanted_ob, REPO)} makes",
        )
    elif reference:
        report.info("option bytes not compared with a file (use --expect or --ob-data)")
    if reference and radio_address:
        sfsa = reference[2 * SFR_WORD] & 0x1FF
        sfsa_checked = mask[2 * SFR_WORD] & 0x1FF == 0x1FF
        if sfsa_checked:
            report.check(
                FLASH_BASE + sfsa * FLASH_PAGE == radio_address,
                f"SFSA 0x{sfsa:X} puts the secure area at the radio address",
                f"SFSA 0x{sfsa:X} (0x{FLASH_BASE + sfsa * FLASH_PAGE:08X}) disagrees with the radio address 0x{radio_address:08X}",
            )
        else:
            report.info("SFSA is not checked by the option bytes (the radio chip sets it)")
    if reference:
        report.check(
            len(reference) == len(mask) == len(write_mask) == 32,
            "option byte tables have 16 entries each",
            "option byte tables have the wrong size",
        )
        report.info(f"updater may rewrite option byte words: {[i // 2 for i in range(0, len(write_mask), 2) if write_mask[i]]}")

    # ---- the standard configuration ----
    if expect in STANDARD:
        want_stack, _ = STANDARD[expect]
        report.check(
            stack_name == want_stack,
            f"radio stack is {want_stack}, the standard one",
            f"radio stack is {stack_name}, --expect {expect} wants {want_stack}",
        )

    # ---- resources ----
    if file_of("Resources") and os.path.isfile(file_of("Resources")):
        # Big files may be in archives of their own, named under "Resources extra"
        extras = m.get("Resources extra", "").split()
        try:
            names, files, first_file = read_resource_archive(file_of("Resources"))
            for extra in extras:
                extra_path = os.path.join(folder, extra)
                if not os.path.isfile(extra_path):
                    raise ValueError(f"extra resource archive {extra} is missing")
                extra_names, extra_files, _ = read_resource_archive(extra_path)
                twice = sorted(set(extra_files) & set(files))
                if twice:
                    raise ValueError(f"{extra} repeats {twice[:3]}")
                names += extra_names
                files.update(extra_files)
            apps = {name: data for name, data in files.items() if name.endswith(".fap")}
        except Exception as e:  # noqa: BLE001
            report.fail(f"resources do not unpack: {e}")
        else:
            if extras:
                report.ok(f"resources: {len(extras)} big files in archives of their own")
            unsafe = [n for n in names if n.startswith("/") or ".." in n.split("/")]
            report.check(not unsafe, f"resources unpack to {len(names)} relative paths", f"resources contain unsafe paths: {unsafe[:3]}")
            # An update leaves a file on the card alone when it has the size and MD5 the new
            # Manifest gives (update_task_resources.c): the Manifest must come first in the archive
            # and describe every file exactly, or every update writes everything again
            # (not a failure: without it the device replaces every file, as it always did)
            problems = resource_manifest_problems(files, first_file)
            if problems:
                report.info("resources: updates will rewrite every file: " + "; ".join(problems[:3]))
            else:
                report.ok("resources: Manifest comes first and gives every file's size and MD5, so updates skip unchanged files")
            fw_major, fw_minor = firmware_api_version(api_csv)
            bad = []
            for name, data in sorted(apps.items()):
                meta = fap_manifest(data)
                if meta is None:
                    bad.append(f"{name}: not an app (no valid manifest)")
                    continue
                major, minor, target = meta
                if major != fw_major or minor > fw_minor:
                    bad.append(f"{name}: built for API {major}.{minor}, the firmware has {fw_major}.{fw_minor}")
                elif target not in (0, 7):
                    bad.append(f"{name}: built for hardware target {target}")
            if apps:
                report.check(
                    not bad,
                    f"all {len(apps)} apps were built for API {fw_major}.x up to {fw_major}.{fw_minor}, which the firmware has",
                    f"{len(bad)} apps would refuse to load: " + "; ".join(bad[:5]),
                )
            else:
                report.info("resources hold no apps")

    summary = {
        "stack": stack_name,
        "radio_address": radio_address,
        "radio_version": m.get("Radio version"),
        "radio_crc": m.get("Radio CRC"),
        "ob": (reference, mask, write_mask),
        "firmware_end": firmware_end,
    }
    return report, summary


def compare_with_reference(report, summary, reference_summary):
    for key in ("stack", "radio_address", "radio_version", "radio_crc"):
        report.check(
            summary[key] == reference_summary[key],
            f"{key} equals the reference",
            f"{key} differs from the reference: {summary[key]!r} vs {reference_summary[key]!r}",
        )
    for name, a, b in zip(("OB reference", "OB mask", "OB write mask"), summary["ob"], reference_summary["ob"]):
        diff = [i // 2 for i in range(0, min(len(a), len(b)), 2) if a[i] != b[i]]
        report.check(not diff, f"{name} equals the reference", f"{name} differs from the reference in entries {diff}")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("package")
    parser.add_argument("--expect", choices=("light", "full", "any"), default="any")
    parser.add_argument("--ob-data", help="option byte file the package should have been made from")
    parser.add_argument("--reference", help="another package whose radio and option bytes this must equal")
    args = parser.parse_args()

    with tempfile.TemporaryDirectory() as scratch:
        report, summary = check_package(args.package, args.expect, args.ob_data, scratch)
        if args.reference:
            with tempfile.TemporaryDirectory() as scratch2:
                ref_report, ref_summary = check_package(args.reference, "any", None, scratch2)
                if ref_report.failed:
                    report.info("the reference package itself has failures:")
                    for level, text in ref_report.lines:
                        if level == "FAIL":
                            report.info(f"  reference: {text}")
                compare_with_reference(report, summary, ref_summary)
    report.print()
    print("\nPASSED" if not report.failed else "\nFAILED")
    return 1 if report.failed else 0


if __name__ == "__main__":
    sys.exit(main())
