#!/usr/bin/env python3
"""Hadal's title font: bold square capitals, 9 pixels tall, for the headers of lists and pages.

Every glyph is drawn here by hand ('#' lit). Lower case letters are the capitals again, so any
header text can be drawn with it. Run to write applications/services/gui/hadal_title_font.c, and
with --preview PNG to see it.
"""

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..", "..")
sys.path.insert(0, os.path.join(ROOT, "scripts"))

from flipper.assets import u8g2font  # noqa: E402

HEIGHT = 9

GLYPHS = {
    "A": """
.#####.
##...##
##...##
##...##
#######
##...##
##...##
##...##
##...##
""",
    "B": """
######.
##...##
##...##
######.
##...##
##...##
##...##
##...##
######.
""",
    "C": """
.######
##.....
##.....
##.....
##.....
##.....
##.....
##.....
.######
""",
    "D": """
######.
##...##
##...##
##...##
##...##
##...##
##...##
##...##
######.
""",
    "E": """
#######
##.....
##.....
######.
##.....
##.....
##.....
##.....
#######
""",
    "F": """
#######
##.....
##.....
######.
##.....
##.....
##.....
##.....
##.....
""",
    "G": """
.######
##.....
##.....
##.....
##..###
##...##
##...##
##...##
.######
""",
    "H": """
##...##
##...##
##...##
##...##
#######
##...##
##...##
##...##
##...##
""",
    "I": """
##
##
##
##
##
##
##
##
##
""",
    "J": """
....##
....##
....##
....##
....##
....##
##..##
##..##
.####.
""",
    "K": """
##...##
##..##.
##.##..
####...
###....
####...
##.##..
##..##.
##...##
""",
    "L": """
##....
##....
##....
##....
##....
##....
##....
##....
######
""",
    "M": """
##.....##
###...###
####.####
##.###.##
##..#..##
##.....##
##.....##
##.....##
##.....##
""",
    "N": """
##...##
###..##
####.##
##.####
##..###
##...##
##...##
##...##
##...##
""",
    "O": """
.#####.
##...##
##...##
##...##
##...##
##...##
##...##
##...##
.#####.
""",
    "P": """
######.
##...##
##...##
##...##
######.
##.....
##.....
##.....
##.....
""",
    "Q": """
.#####.
##...##
##...##
##...##
##...##
##...##
##.#.##
##..##.
.####.#
""",
    "R": """
######.
##...##
##...##
##...##
######.
##.##..
##..##.
##...##
##...##
""",
    "S": """
.######
##.....
##.....
##.....
.#####.
.....##
.....##
.....##
######.
""",
    "T": """
########
...##...
...##...
...##...
...##...
...##...
...##...
...##...
...##...
""",
    "U": """
##...##
##...##
##...##
##...##
##...##
##...##
##...##
##...##
.#####.
""",
    "V": """
##...##
##...##
##...##
##...##
##...##
.##.##.
.##.##.
..###..
...#...
""",
    "W": """
##.....##
##.....##
##.....##
##..#..##
##.###.##
####.####
###...###
##.....##
#.......#
""",
    "X": """
##...##
##...##
.##.##.
..###..
..###..
..###..
.##.##.
##...##
##...##
""",
    "Y": """
##..##
##..##
##..##
.####.
..##..
..##..
..##..
..##..
..##..
""",
    "Z": """
#######
.....##
....##.
...##..
..##...
.##....
##.....
##.....
#######
""",
    "0": """
.####.
##..##
##..##
##.###
###.##
##..##
##..##
##..##
.####.
""",
    "1": """
.##
###
.##
.##
.##
.##
.##
.##
.##
""",
    "2": """
.####.
##..##
....##
....##
...##.
..##..
.##...
##....
######
""",
    "3": """
#####.
....##
....##
....##
.####.
....##
....##
....##
#####.
""",
    "4": """
##..##
##..##
##..##
##..##
######
....##
....##
....##
....##
""",
    "5": """
######
##....
##....
#####.
....##
....##
....##
....##
#####.
""",
    "6": """
.#####
##....
##....
#####.
##..##
##..##
##..##
##..##
.####.
""",
    "7": """
######
....##
....##
...##.
...##.
..##..
..##..
..##..
..##..
""",
    "8": """
.####.
##..##
##..##
##..##
.####.
##..##
##..##
##..##
.####.
""",
    "9": """
.####.
##..##
##..##
##..##
.#####
....##
....##
....##
#####.
""",
    "-": """
....
....
....
....
####
####
....
....
....
""",
    ".": """
..
..
..
..
..
..
..
##
##
""",
    ",": """
..
..
..
..
..
..
..
##
#.
""",
    ":": """
..
..
##
##
..
..
..
##
##
""",
    "!": """
##
##
##
##
##
##
..
##
##
""",
    "?": """
.####.
##..##
....##
...##.
..##..
..##..
......
..##..
..##..
""",
    "'": """
##
##
#.
..
..
..
..
..
..
""",
    "/": """
....##
....##
...##.
...##.
..##..
.##...
.##...
##....
##....
""",
    "+": """
......
......
..##..
..##..
######
######
..##..
..##..
......
""",
    "(": """
.##
##.
##.
##.
##.
##.
##.
##.
.##
""",
    ")": """
##.
.##
.##
.##
.##
.##
.##
.##
##.
""",
    "&": """
.###...
##.##..
##.##..
.###...
.###.##
##.####
##..##.
##..##.
.###.##
""",
    "_": """
......
......
......
......
......
......
......
......
######
""",
    " ": """
...
...
...
...
...
...
...
...
...
""",
}


def glyph_rows(art):
    rows = art.strip("\n").split("\n")
    if len(rows) != HEIGHT or len({len(r) for r in rows}) != 1:
        raise ValueError(f"a glyph must be {HEIGHT} rows of one width:\n{art}")
    return rows


def build_font():
    glyphs = {}
    for ch, art in GLYPHS.items():
        rows = glyph_rows(art)
        pixels = [[1 if c == "#" else 0 for c in row] for row in rows]
        width = len(rows[0])
        glyphs[ord(ch)] = u8g2font.Glyph(ord(ch), pixels, 0, 0, width + 1)
    for ch in "abcdefghijklmnopqrstuvwxyz":
        g = glyphs[ord(ch.upper())].copy()
        g.encoding = ord(ch)
        glyphs[ord(ch)] = g
    max_width = max(g.width for g in glyphs.values())
    header = bytearray(u8g2font.HEADER_SIZE)
    header[9] = max_width  # font box
    header[10] = HEIGHT
    header[11] = 0
    header[12] = 0
    header[13] = HEIGHT  # ascent of A
    header[14] = 0  # descent of g: none, capitals only
    header[15] = HEIGHT  # ascent of (
    header[16] = 0
    return u8g2font.Font(header, glyphs)


C_TEMPLATE = """/* Generated by assets/hadal_ui/title_font.py: edit that, not this. */
#include "hadal_title_font.h"

/* Hadal's title font (u8g2): bold square capitals, {height} pixels tall, lower case drawn as upper */
const uint8_t hadal_title_font[{size}] = {{
{data}
}};
"""


def write_c(data, path):
    lines = []
    for i in range(0, len(data), 16):
        lines.append("    " + " ".join(f"0x{b:02x}," for b in data[i : i + 16]))
    with open(path, "w") as f:
        f.write(C_TEMPLATE.format(height=HEIGHT, size=len(data), data="\n".join(lines)))


def main(argv):
    font = build_font()
    data = u8g2font.serialize_font(font)
    # what the firmware reads back is what was drawn
    again = u8g2font.parse_font(data)
    for e, g in font.glyphs.items():
        assert again.glyphs[e].rows() == g.rows(), chr(e)
    write_c(data, os.path.join(ROOT, "applications", "services", "gui", "hadal_title_font.c"))
    if len(argv) > 2 and argv[1] == "--preview":
        from PIL import Image

        lines = ["HADAL SETTINGS", "CONTROL CENTRE 12:05", "ABCDEFGHIJKLMNOPQRSTUVWXYZ", "0123456789 -.,:!?'/+()&_", "Sub-GHz Remote / NFC"]
        image = Image.new("1", (300, 14 * len(lines) + 4), 1)
        for i, text in enumerate(lines):
            again.draw_text(image, 2, 12 + 14 * i, text, lit=0)
        image = image.resize((image.width * 3, image.height * 3), Image.NEAREST)
        image.save(argv[2])
    print(f"title font: {len(font.glyphs)} glyphs, {len(data)} bytes")


if __name__ == "__main__":
    main(sys.argv)
