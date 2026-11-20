#!/usr/bin/env python3
"""Abyss as a C bitmap (XBM: rows of bytes, least significant bit first, a set bit dark) for code
that draws him itself, such as the updater's descent screen, which has no icon assets.

    python3 assets/hadal_ui/abyss_c.py SIZE [FIRST_COLUMN] [--mirror]

prints the array for the 1-bit Abyss of that size (brand.abyss_one_bit), its empty top and bottom
rows trimmed, from FIRST_COLUMN on (to show only his head and light), facing left with --mirror,
and where his light is.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import brand  # noqa: E402


def bitmap(size, first_column=0, mirror=False):
    rows = brand.PIXELS[size]
    dark = brand.abyss_one_bit(size)
    width = max(len(r) for r in rows)
    if mirror:
        dark = {(width - 1 - c, r) for c, r in dark}
        rows = [r.ljust(width, ".")[::-1] for r in rows]
    used = [r for r in range(len(rows)) if any((c, r) in dark for c in range(first_column, width))]
    top, bottom = min(used), max(used)
    w = width - first_column
    data = []
    for r in range(top, bottom + 1):
        for byte in range((w + 7) // 8):
            value = 0
            for bit in range(8):
                c = first_column + byte * 8 + bit
                if c < width and (c, r) in dark:
                    value |= 1 << bit
            data.append(value)
    light = [(c - first_column, r - top) for r, row in enumerate(rows) for c, ch in enumerate(row) if ch == "o"]
    return w, bottom - top + 1, data, light


def main(argv):
    mirror = "--mirror" in argv
    argv = [a for a in argv if a != "--mirror"]
    size = argv[1] if len(argv) > 1 else "32"
    first = int(argv[2]) if len(argv) > 2 else 0
    w, h, data, light = bitmap(size, first, mirror)
    lx = sum(c for c, _ in light) / len(light)
    ly = sum(r for _, r in light) / len(light)
    flag = " --mirror" if mirror else ""
    print(f"/* Abyss, {w}x{h}: assets/hadal_ui/abyss_c.py {size} {first}{flag}; his light at {lx:.0f},{ly:.0f} */")
    print(f"static const uint8_t abyss_bits[{len(data)}] = {{")
    per_row = (w + 7) // 8
    for i in range(0, len(data), per_row * 4):
        print("    " + ", ".join(f"0x{b:02x}" for b in data[i:i + per_row * 4]) + ",")
    print("};")


if __name__ == "__main__":
    main(sys.argv)
