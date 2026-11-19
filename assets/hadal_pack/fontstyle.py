"""The fonts of the Hadal pack, restyled from the firmware's own fonts.

A pack font must have exactly the layout metrics of the font it replaces, and here every glyph also
keeps its box and its advance, so text takes the same room and no screen changes its layout.
Two fonts are replaced:

  Primary      the bold interface font, lightened: every stroke of two or more pixels loses one
  BigNumbers   the large digits (clock and the like), redrawn as instrument-panel numerals

The other three (Secondary, Keyboard, BatteryPercent) are left as the firmware has them.
"""

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "scripts"))

from flipper.assets import pack, u8g2font  # noqa: E402
from boot import stock_font  # noqa: E402


def thin(glyph):
    """Drop the last pixel of every horizontal run of two or more: heavy strokes become single."""
    out = [[0] * glyph.width for _ in range(glyph.height)]
    for y, row in enumerate(glyph.pixels):
        x = 0
        while x < len(row):
            if row[x]:
                start = x
                while x < len(row) and row[x]:
                    x += 1
                length = x - start
                for k in range(length - 1 if length >= 2 else length):
                    out[y][start + k] = 1
            else:
                x += 1
    g = glyph.copy()
    g.pixels = out
    return g


def light_primary():
    font = stock_font("Primary")
    return u8g2font.Font(font.header, {e: thin(g) for e, g in font.glyphs.items()})


# ---- BigNumbers ----

_SEGMENTS = {
    "0": "abcdef", "1": "bc", "2": "abged", "3": "abgcd", "4": "fgbc",
    "5": "afgcd", "6": "afgedc", "7": "abc", "8": "abcdefg", "9": "abcdfg",
}


def _segment_pixels(name):
    """Pixels of one segment in the 10 x 14 digit box; ends are chamfered like a display's."""
    px = set()
    if name == "a":
        px |= {(x, 0) for x in range(2, 8)} | {(x, 1) for x in range(1, 9)}
    elif name == "d":
        px |= {(x, 12) for x in range(1, 9)} | {(x, 13) for x in range(2, 8)}
    elif name == "g":
        px |= {(x, y) for x in range(1, 9) for y in (6, 7)}
    elif name == "f":
        px |= {(x, y) for x in (0, 1) for y in range(1, 7)}
    elif name == "b":
        px |= {(x, y) for x in (8, 9) for y in range(1, 7)}
    elif name == "e":
        px |= {(x, y) for x in (0, 1) for y in range(7, 13)}
    elif name == "c":
        px |= {(x, y) for x in (8, 9) for y in range(7, 13)}
    return px


def _digit(ch):
    rows = [[0] * 10 for _ in range(14)]
    if ch == "1":
        # a stem with a flag and a foot, so that it cannot be mistaken for a bar
        for y in range(14):
            rows[y][4] = rows[y][5] = 1
        for x, y in ((2, 3), (3, 2), (3, 3), (2, 4)):
            rows[y][x] = 1
        for x in range(2, 8):
            rows[12][x] = rows[13][x] = 1
    else:
        for seg in _SEGMENTS[ch]:
            for x, y in _segment_pixels(seg):
                rows[y][x] = 1
        # soften the inner corners where two segments meet, so the digit reads as one stroke
        for x, y in ((1, 2), (8, 2), (1, 11), (8, 11)):
            if rows[y][x] and ch not in "1":
                pass
    return rows


def instrument_numerals():
    font = stock_font("BigNumbers")
    glyphs = {e: g.copy() for e, g in font.glyphs.items()}
    for ch in "0123456789":
        g = glyphs[ord(ch)]
        g.pixels = _digit(ch)
    return u8g2font.Font(font.header, glyphs)


FONTS = {
    "Primary": light_primary,
    "BigNumbers": instrument_numerals,
}


def build_fonts():
    """slot -> u8g2 font bytes, each checked the way the firmware will check it."""
    out = {}
    for slot, make in FONTS.items():
        data = u8g2font.serialize_font(make())
        stock = pack.stock_font_info(os.path.join(HERE, "..", ".."), slot)
        problem = pack.font_problem(data, stock)
        if problem:
            raise SystemExit(f"{slot}: {problem}")
        out[slot] = data
    return out
