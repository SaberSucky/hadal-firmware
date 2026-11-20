#!/usr/bin/env python3
"""Turn a Hadal crash report (/ext/crash/crash_*.txt on the SD card) into function names.

    python3 scripts/crash_report.py crash_20261120_032145.txt [--elf build/f7-firmware-C/firmware.elf]

The report lists code addresses (where the fault was, where the crash was raised, and the
likely return addresses on the stack). This looks each up in the firmware's ELF with
arm-none-eabi-addr2line. The ELF must be from the same build: the report names its commit.
"""

import argparse
import os
import re
import shutil
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_ELF = os.path.join(REPO, "build", "f7-firmware-C", "firmware.elf")


def find_addr2line():
    local = os.path.join(REPO, "toolchain", "current", "bin", "arm-none-eabi-addr2line")
    return local if os.path.exists(local) else shutil.which("arm-none-eabi-addr2line")


def report_addresses(text):
    """[(label, address)] in the order the report gives them."""
    found = []
    for label, key in (("fault", "Fault at:"), ("raised", "Raised from:")):
        m = re.search(re.escape(key) + r"\s*0x([0-9A-Fa-f]{8})", text)
        if m:
            found.append((label, int(m.group(1), 16)))
    m = re.search(r"Fault at:.*called from 0x([0-9A-Fa-f]{8})", text)
    if m:
        found.append(("fault caller", int(m.group(1), 16)))
    m = re.search(r"Call chain:([^\n]*)", text)
    if m:
        for i, a in enumerate(re.findall(r"0x([0-9A-Fa-f]{8})", m.group(1))):
            found.append((f"stack #{i}", int(a, 16)))
    return found


def symbolize(elf, addresses, addr2line):
    if not addresses:
        return []
    # a return address points just after the call: look one instruction back
    args = [addr2line, "-f", "-C", "-p", "-e", elf] + [hex(a - 1 if a & ~1 else a) for a in addresses]
    out = subprocess.run(args, capture_output=True, text=True, check=True).stdout.splitlines()
    return [line.replace(REPO + os.sep, "") for line in out]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("report")
    parser.add_argument("--elf", default=DEFAULT_ELF)
    args = parser.parse_args()

    with open(args.report, errors="replace") as f:
        text = f.read()
    addr2line = find_addr2line()
    if not addr2line:
        raise SystemExit("arm-none-eabi-addr2line not found (run ./fbt once to get the toolchain)")
    if not os.path.exists(args.elf):
        raise SystemExit(f"no ELF at {args.elf}: build the same commit first")

    commit = re.search(r"commit ([0-9a-f]{7,})", text)
    if commit:
        head = subprocess.run(["git", "rev-parse", "HEAD"], cwd=REPO, capture_output=True, text=True).stdout
        if not head.startswith(commit.group(1)):
            print(f"note: the report is from commit {commit.group(1)}, this tree is at {head[:8]}: "
                  "names are only right with the ELF of that commit\n")

    for line in text.splitlines()[:6]:
        print(line)
    print()
    pairs = report_addresses(text)
    for (label, address), where in zip(pairs, symbolize(args.elf, [a for _, a in pairs], addr2line)):
        print(f"{label:13} 0x{address:08X}  {where}")


if __name__ == "__main__":
    sys.exit(main())
