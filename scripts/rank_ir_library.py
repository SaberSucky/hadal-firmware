#!/usr/bin/env python3
"""Send the universal remotes' most-used codes first.

    python3 scripts/rank_ir_library.py fetch     # get the pinned public IR database
    python3 scripts/rank_ir_library.py order     # write the .order file next to each library
    python3 scripts/rank_ir_library.py report    # how soon each database model gets its code

A universal remote sends every code of a button and stops when the user sees the device react, so
the codes that work on the most devices should go first. The public Flipper IR database (CC0, one
file per real remote) says how many device models use each code. For every library this script
writes an order file ("tv.ir" -> "tv.order") listing, per button, the button's codes most-used
first; ties keep the library's own order, which already puts the big brands first.

The libraries themselves are not changed: the Remote Finder ("save all") relies on their device
blocks. The firmware (infrared_brute_force.c) reads the order file only if the library still has
the size the file was made for, and otherwise sends in library order.

The timing model in this file mirrors the firmware (infrared_raw_trim.c, infrared_signal.c) for
the report; scripts/asset_packer_test.py checks the two agree.
"""

import argparse
import collections
import os
import re
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASSETS = os.path.join(REPO, "applications", "main", "infrared", "resources", "infrared", "assets")
CACHE = os.path.join(REPO, "build", "ir_database")
IRDB_REPO = "https://github.com/Lucaslhm/Flipper-IRDB"
IRDB_COMMIT = "d126fb1b6f1e114c52b4a8c19839ea65e3a9c24d"

ORDER_FILETYPE = "IR library order"
ORDER_VERSION = 1

# library file -> (database folders it covers, the button the report times)
LIBRARIES = {
    "tv.ir": (["TVs"], "Power"),
    "audio.ir": (["Audio_and_Video_Receivers", "SoundBars", "Speakers"], "Power"),
    "projectors.ir": (["Projectors"], "Power"),
    "fans.ir": (["Fans"], "Power"),
    "ac.ir": (["ACs"], "Off"),
    "leds.ir": (["LED_Lighting"], "Power_on"),
}

# names a database remote gives the button that the report's universal button reaches
PRIMARY_NAMES = {
    "Power": re.compile(r"^(power|pwr|power_?toggle|power_?off|off|standby|on_?off|power_?on_?off)$", re.I),
    "Off": re.compile(r"^(off|power_?off|power)$", re.I),
    "Power_on": re.compile(r"^(on|power_?on|power)$", re.I),
}

# address and command widths of each protocol (lib/infrared/encoder_decoder/*/infrared_protocol_*.c)
PROTOCOL_BITS = {
    "Samsung32": (8, 8), "RC6": (8, 8), "RC5": (5, 6), "RC5X": (5, 6), "SIRC": (5, 7), "SIRC15": (8, 7),
    "SIRC20": (13, 7), "Kaseikyo": (26, 10), "RCA": (4, 8), "Pioneer": (8, 8),
}

RAW_FRAME_GAP_US = 10000


def order_path(library_path):
    return library_path[: -len(".ir")] + ".order"


# ---- reading .ir files ----


def parse_ir_signals(text):
    """Field dicts of every signal in any .ir file (remote or library), in file order."""
    out, cur = [], None
    for line in text.splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        key, _, value = line.partition(":")
        if key == "name":
            cur = {"name": value.strip()}
            out.append(cur)
        elif cur is not None:
            cur[key.strip()] = value.strip()
    return out


def le_hex(text):
    try:
        return int.from_bytes(bytes(int(x, 16) for x in text.split()), "little")
    except ValueError:
        return None


def raw_timings(fields):
    try:
        return [int(x) for x in fields.get("data", "").split()]
    except ValueError:
        return []


def raw_frames(timings):
    frames, start = [], 0
    for i in range(1, len(timings), 2):
        if timings[i] > RAW_FRAME_GAP_US:
            frames.append(timings[start:i])
            start = i + 1
    if start < len(timings):
        frames.append(timings[start:])
    return frames


# ---- what goes on the air (mirrors infrared_signal_key.c) ----


def parsed_key(protocol, address, command):
    """Two parsed codes have the same key exactly when they send the same bits. NEC and NECext
    (and NEC42 and NEC42ext) are one signal written two ways; bits a protocol does not send
    are ignored."""
    if protocol == "NEC":
        a, c = address & 0xFF, command & 0xFF
        return ("NEC32", a | ((~a & 0xFF) << 8) | (c << 16) | ((~c & 0xFF) << 24))
    if protocol == "NECext":
        return ("NEC32", (address & 0xFFFF) | ((command & 0xFFFF) << 16))
    if protocol == "NEC42":
        a, c = address & 0x1FFF, command & 0xFF
        return ("NEC42", a | ((~a & 0x1FFF) << 13) | (c << 26) | ((~c & 0xFF) << 34))
    if protocol == "NEC42ext":
        return ("NEC42", (address & 0x3FFFFFF) | ((command & 0xFFFF) << 26))
    bits = PROTOCOL_BITS.get(protocol)
    if bits:
        address &= (1 << bits[0]) - 1
        command &= (1 << bits[1]) - 1
    return (protocol, address, command)


def _pulse_distance(frame, lead_mark, lead_space, bits):
    """The bits of a pulse-distance frame of exactly this many bits, or None."""
    if len(frame) != 2 + 2 * bits + 1:
        return None
    if abs(frame[0] - lead_mark) > lead_mark * 20 // 100 or abs(frame[1] - lead_space) > lead_space * 25 // 100:
        return None
    value = 0
    for i in range(bits):
        mark, space = frame[2 + 2 * i], frame[3 + 2 * i]
        if not 300 <= mark <= 900 or not 300 <= space <= 2000:
            return None
        value |= (1 if space > 1000 else 0) << i
    if not 300 <= frame[-1] <= 900:
        return None
    return value


def decode_raw(timings):
    """The parsed key a raw capture of NEC or Samsung32 is (every frame that code, short repeat
    frames after it allowed), or None. The firmware also knows the other protocols."""
    key = None
    for frame in raw_frames(timings):
        nec = _pulse_distance(frame, 9000, 4500, 32)
        sam = _pulse_distance(frame, 4500, 4500, 32)
        if nec is not None:
            k = parsed_key("NECext", nec & 0xFFFF, nec >> 16)
        elif sam is not None and (sam & 0xFF) == ((sam >> 8) & 0xFF) and ((sam >> 16) & 0xFF) == (~(sam >> 24) & 0xFF):
            k = ("Samsung32", sam & 0xFF, (sam >> 16) & 0xFF)
        elif key is not None and len(frame) <= 8:
            continue  # a short repeat frame after the code
        else:
            return None
        if key is None:
            key = k
        elif k != key:
            return None
    return key


def signal_key(fields):
    """What a signal sends, so that equal keys mean the same code on the air."""
    if fields.get("type") == "raw":
        timings = raw_timings(fields)
        decoded = decode_raw(timings)
        if decoded:
            return decoded
        frequency = int(fields.get("frequency", "38000") or 38000)
        return ("raw", (frequency + 500) // 1000, tuple((t + 50) // 100 for t in timings))
    address, command = le_hex(fields.get("address", "")), le_hex(fields.get("command", ""))
    if address is None or command is None:
        return ("bad", fields.get("name"))
    return parsed_key(fields.get("protocol", ""), address, command)


# ---- database counts ----


def model_codes(irdb, folders):
    """For every remote file of the folders: its codes, by button name."""
    out = []
    for folder in folders:
        for root, _, files in os.walk(os.path.join(irdb, folder)):
            for name in sorted(files):
                if not name.endswith(".ir"):
                    continue
                with open(os.path.join(root, name), errors="replace") as f:
                    signals = parse_ir_signals(f.read())
                by_name = collections.defaultdict(set)
                for s in signals:
                    by_name[s["name"]].add(signal_key(s))
                out.append(by_name)
    return out


def popularity(models):
    """code -> number of remotes (device models) that send it, whatever they call the button."""
    count = collections.Counter()
    for by_name in models:
        count.update(set().union(*by_name.values()) if by_name else set())
    return count


# ---- order ----


def button_order(signals, counts):
    """name -> the button's signals as indices among the button's own signals (0 = its first in
    the file), most-used code first, ties in file order."""
    by_name = collections.defaultdict(list)
    for s in signals:
        by_name[s["name"]].append(s)
    order = {}
    for name, group in by_name.items():
        scores = [counts.get(signal_key(s), 0) for s in group]
        order[name] = sorted(range(len(group)), key=lambda i: (-scores[i], i))
    return order


def write_order(library_path, order):
    size = os.path.getsize(library_path)
    lines = [
        f"Filetype: {ORDER_FILETYPE}",
        f"Version: {ORDER_VERSION}",
        f"# The order the universal remote sends {os.path.basename(library_path)}'s codes in: the codes",
        "# the most device models use first, counted in the public Flipper IR database (CC0) at",
        f"# {IRDB_COMMIT[:12]}. Made by scripts/rank_ir_library.py; each button lists its codes, 0 being",
        "# its first in the library. The firmware ignores this file unless the library has this size.",
        f"Library size: {size}",
    ]
    for name, indices in order.items():
        lines.append(f"{name}: " + " ".join(str(i) for i in indices))
    with open(order_path(library_path), "w") as f:
        f.write("\n".join(lines) + "\n")


def read_order(library_path):
    """name -> indices from the library's order file, or None if the firmware would ignore it."""
    try:
        with open(order_path(library_path)) as f:
            text = f.read()
    except FileNotFoundError:
        return None
    fields = {}
    for line in text.splitlines():
        if line.startswith("#") or ":" not in line:
            continue
        key, _, value = line.partition(":")
        fields[key.strip()] = value.strip()
    if fields.get("Filetype") != ORDER_FILETYPE or fields.get("Version") != str(ORDER_VERSION):
        return None
    if fields.get("Library size") != str(os.path.getsize(library_path)):
        return None
    return {k: [int(x) for x in v.split()] for k, v in fields.items() if k not in ("Filetype", "Version", "Library size")}


def sending_order(signals, name, order=None):
    """The signals of a button in the order the firmware sends them: the order file's order (if
    it is a permutation), each code once, the parsed signal of a code in place of a raw capture."""
    group = [s for s in signals if s["name"] == name]
    indices = list(range(len(group)))
    wanted = (order or {}).get(name)
    if wanted and sorted(wanted) == indices:
        indices = wanted
    kept, slot = [], {}
    for i in indices:
        s = group[i]
        k = signal_key(s)
        if k not in slot:
            slot[k] = len(kept)
            kept.append(s)
        elif kept[slot[k]].get("type") == "raw" and s.get("type") != "raw":
            kept[slot[k]] = s
    return kept


# ---- timing model (mirrors the firmware), for the report ----

FRAME_MS = {
    "NEC": 67.5, "NECext": 67.5, "NEC42": 85, "NEC42ext": 85, "Samsung32": 62, "SIRC": 111, "SIRC15": 120,
    "SIRC20": 130, "RC5": 25, "RC5X": 25, "RC6": 23, "Kaseikyo": 67, "RCA": 56, "Pioneer": 124,
}
DEFAULT_GAP_MS = {
    "NEC": 110, "NECext": 110, "NEC42": 110, "NEC42ext": 110, "Samsung32": 145, "SIRC": 10, "SIRC15": 10,
    "SIRC20": 10, "RC5": 27, "RC5X": 27, "RC6": 27, "Kaseikyo": 130, "RCA": 8, "Pioneer": 26,
}
GAP_MS = {
    "fast": {"NEC": 40, "NECext": 40, "NEC42": 40, "NEC42ext": 40, "Samsung32": 45, "Kaseikyo": 74},
    "turbo": {"NEC": 40, "NECext": 40, "NEC42": 40, "NEC42ext": 40, "Samsung32": 25, "Kaseikyo": 20,
              "RC5": 15, "RC5X": 15, "RC6": 15},
}


def _same_frame(a, b):
    return len(a) == len(b) and all(abs(x - y) <= max(150, x // 4) for x, y in zip(a, b))


def _sirc_like(frame):
    return len(frame) >= 2 and abs(frame[0] - 2400) <= 600 and abs(frame[1] - 600) <= 200


def _nec_repeat(frame):
    return 2 <= len(frame) <= 3 and abs(frame[0] - 9000) <= 1800 and abs(frame[1] - 2250) <= 600


def raw_cut(timings, mode):
    """How many timings the firmware sends of a raw capture (mirrors infrared_raw_trim.c): Fast
    keeps up to two identical frames in a row, Turbo one and drops NEC repeat frames; both keep
    three of a Sony-like code, which devices only accept repeated. Always ends on a mark."""
    if mode == "normal":
        return len(timings)
    frames, starts, start = [], [], 0
    for i in range(1, len(timings), 2):
        if timings[i] > RAW_FRAME_GAP_US:
            frames.append(timings[start:i])
            starts.append(start)
            start = i + 1
    if start < len(timings):
        frames.append(timings[start:])
        starts.append(start)
    keep = 3 if frames and _sirc_like(frames[0]) else (2 if mode == "fast" else 1)
    same = 0
    for n, frame in enumerate(frames):
        if mode == "turbo" and n and _nec_repeat(frame):
            return starts[n] - 1
        same = same + 1 if n and _same_frame(frames[n - 1], frame) else 1
        if same > keep:
            return starts[n] - 1
    return len(timings)


def raw_turbo_gap_us(timings, cut):
    """Turbo's silence before a raw code: five times its longest mark sent, 20 to 50 ms."""
    longest = max(timings[0:cut:2] or [0])
    return min(50000, max(20000, 5 * longest))


def send_ms(fields, mode):
    """How long the firmware takes to send one code, the silence before it included."""
    if fields.get("type") == "raw":
        timings = raw_timings(fields)
        cut = raw_cut(timings, mode)
        gap = {"normal": 180, "fast": 50}.get(mode) or raw_turbo_gap_us(timings, cut) / 1000
        return gap + sum(timings[:cut]) / 1000
    p = fields.get("protocol")
    gap = DEFAULT_GAP_MS.get(p, 30) if mode == "normal" else GAP_MS[mode].get(p, DEFAULT_GAP_MS.get(p, 30))
    return gap + FRAME_MS.get(p, 60)


def schedule(sent, mode, tick_ms):
    """[(time in s when the code has been sent, key)] for codes sent one per scene tick."""
    t, out = 0.0, []
    for i, s in enumerate(sent):
        t = max(t, i * tick_ms) + send_ms(s, mode)
        out.append((t / 1000, signal_key(s)))
    return out


def report(irdb, assets=ASSETS):
    for lib, (folders, primary) in LIBRARIES.items():
        path = os.path.join(assets, lib)
        with open(path) as f:
            signals = parse_ir_signals(f.read())
        models = model_codes(irdb, folders)
        targets = []
        for by_name in models:
            keys = set()
            for name, codes in by_name.items():
                if PRIMARY_NAMES[primary].match(name):
                    keys |= codes
            if keys:
                targets.append(keys)
        before = [s for s in signals if s["name"] == primary]
        after = sending_order(signals, primary, read_order(path) or button_order(signals, popularity(models)))
        print(f"== {lib} {primary}: {len(before)} codes -> {len(after)} without repeats; "
              f"{len(targets)} database models have a {primary} button")
        for label, sent, mode, tick in (
            ("before (library order, Fast)", before, "fast", 100),
            ("now, Fast", after, "fast", 5),
            ("now, Turbo", after, "turbo", 5),
        ):
            times = schedule(sent, mode, tick)
            first = {}
            for when, k in times:
                first.setdefault(k, when)
            hits = sorted(min(first[k] for k in keys if k in first) for keys in targets if keys & first.keys())
            if hits:
                print(f"  {label:30} full run {times[-1][0]:5.1f}s | {len(hits)}/{len(targets)} models reached: "
                      f"half by {hits[len(hits) // 2]:4.1f}s, 3 in 4 by {hits[int(len(hits) * .75)]:4.1f}s")


# ---- commands ----


def fetch():
    if os.path.isdir(os.path.join(CACHE, ".git")):
        head = subprocess.run(["git", "rev-parse", "HEAD"], cwd=CACHE, capture_output=True, text=True).stdout.strip()
        if head == IRDB_COMMIT:
            return CACHE
    os.makedirs(CACHE, exist_ok=True)

    def run(*args):
        subprocess.run(["git", *args], cwd=CACHE, check=True, stdout=subprocess.DEVNULL)

    if not os.path.isdir(os.path.join(CACHE, ".git")):
        run("init", "-q")
        run("remote", "add", "origin", IRDB_REPO)
    print(f"fetching {IRDB_REPO} at {IRDB_COMMIT[:8]} ...")
    run("fetch", "-q", "--depth", "1", "origin", IRDB_COMMIT)
    run("checkout", "-q", "FETCH_HEAD")
    return CACHE


def write_orders(irdb, assets=ASSETS):
    for lib, (folders, _) in LIBRARIES.items():
        path = os.path.join(assets, lib)
        with open(path) as f:
            signals = parse_ir_signals(f.read())
        order = button_order(signals, popularity(model_codes(irdb, folders)))
        write_order(path, order)
        if read_order(path) != order:
            raise SystemExit(f"{lib}: the order file does not read back")
        moved = sum(1 for indices in order.values() for i, j in enumerate(indices) if i != j)
        print(f"{order_path(lib)}: {len(order)} buttons, {moved} of {len(signals)} codes moved")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("command", choices=("fetch", "order", "report"))
    parser.add_argument("--irdb", help="a checkout of the IR database (default: fetch the pinned one)")
    args = parser.parse_args()
    irdb = args.irdb or fetch()
    if args.command == "order":
        write_orders(irdb)
    elif args.command == "report":
        report(irdb)


if __name__ == "__main__":
    sys.exit(main())
