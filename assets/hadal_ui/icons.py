#!/usr/bin/env python3
"""Hadal's interface icons, drawn by hand as text: '#' is a lit (black) pixel.

Run to write them as PNGs into assets/icons/Hadal/ (the build compiles those), and with
--preview DIR to also write an enlarged sheet for checking them by eye.
"""

import os
import sys

from PIL import Image

ICONS = {
    # ---- control centre tiles, 12x12 ----
    "cc_lock_12x12": """
....####....
...##..##...
..##....##..
..#......#..
..#......#..
.##########.
.##########.
.####..####.
.####..####.
.####..####.
.##########.
.##########.
""",
    "cc_pin_12x12": """
.##..##..##.
.##..##..##.
............
.##..##..##.
.##..##..##.
............
.##..##..##.
.##..##..##.
............
.....##.....
.....##.....
............
""",
    "cc_bluetooth_12x12": """
.....#......
.....##.....
.....#.#....
..#..#..#...
...#.#.#....
....###.....
...#.#.#....
..#..#..#...
.....#.#....
.....##.....
.....#......
............
""",
    "cc_mute_12x12": """
.....#......
....##......
...#.#......
###..#.#...#
#....#..#.#.
#....#...#..
#....#..#.#.
###..#.#...#
...#.#......
....##......
.....#......
............
""",
    "cc_dark_12x12": """
....####....
..##....##..
.####.....#.
.####.....#.
#####......#
#####......#
#####......#
#####......#
.####.....#.
.####.....#.
..##....##..
....####....
""",
    # left-handed: a turn arrow, the screen goes round
    "cc_rotate_12x12": """
............
...######.##
..##....####
..#.....####
.##.......#.
.##.........
.##......##.
..#......##.
..##....##..
...######...
............
............
""",
    # Abyss, Hadal's anglerfish: the light on its lure, a big eye, a toothy grin
    "cc_hadal_14x14": """
.....####.....
....#....#....
....#.....#...
..#####...#...
.#.....#.####.
#..###..######
#.#.#.#.######
#.#...#..####.
#..###........
#......#####..
#.....#.#.#...
#.....#.......
.#.....#.#.#..
..##....####..
""",
    "cc_dummy_12x12": """
............
............
..########..
.#........#.
#..#....#..#
#.###..#.#.#
#..#....#..#
#..........#
#...####...#
.###....###.
............
............
""",
    "cc_add_12x12": """
............
.....##.....
.....##.....
.....##.....
.....##.....
.##########.
.##########.
.....##.....
.....##.....
.....##.....
.....##.....
............
""",
    # the app picker's "no app"
    "cc_none_12x12": """
....####....
..##....##..
.#........#.
.#.......##.
#.......#..#
#......#...#
#.....#....#
#....#.....#
.#..#.....#.
.###......#.
..##....##..
....####....
""",
    # ---- hint bar glyphs, 7 tall (OK is the stock ButtonCenter_7x7) ----
    "hint_back_8x7": """
..#.....
.##.....
#######.
.##....#
..#....#
.......#
..#####.
""",
    "hint_lr_9x7": """
...#.#...
..##.##..
.###.###.
####.####
.###.###.
..##.##..
...#.#...
""",
    "hint_ud_7x7": """
...#...
..###..
.#####.
.......
.#####.
..###..
...#...
""",
    # ---- control centre sliders and header, 7x7 ----
    "cc_sun_7x7": """
...#...
.#...#.
..###..
#.###.#
..###..
.#...#.
...#...
""",
    "cc_speaker_9x7": """
...#..#..
..##...#.
####.#..#
####.#..#
####.#..#
..##...#.
...#..#..
""",
    "cc_bolt_5x7": """
..##.
.##..
####.
.####
..##.
.##..
.#...
""",
}


def parse(name, art):
    rows = [r for r in art.strip("\n").split("\n")]
    size = name.rsplit("_", 1)[1]
    width, height = (int(v) for v in size.split("x"))
    if len(rows) != height or any(len(r) != width for r in rows):
        raise ValueError(f"{name}: drawing is not {width}x{height}")
    return width, height, rows


def to_image(name, art):
    width, height, rows = parse(name, art)
    image = Image.new("1", (width, height), 1)
    for y, row in enumerate(rows):
        for x, c in enumerate(row):
            if c == "#":
                image.putpixel((x, y), 0)
    return image


def main(argv):
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    out = os.path.join(root, "icons", "Hadal")
    os.makedirs(out, exist_ok=True)
    for name, art in ICONS.items():
        to_image(name, art).save(os.path.join(out, name + ".png"))
    if len(argv) > 2 and argv[1] == "--preview":
        scale, pad = 8, 4
        sheet = Image.new("RGB", (len(ICONS) * (12 * scale + pad) + pad, 12 * scale + 2 * pad), "white")
        x = pad
        for name, art in ICONS.items():
            im = to_image(name, art).convert("RGB")
            im = im.resize((im.width * scale, im.height * scale), Image.NEAREST)
            sheet.paste(im, (x, pad))
            x += 12 * scale + pad
        sheet.save(argv[2])


if __name__ == "__main__":
    main(sys.argv)
