#!/usr/bin/env python3
"""Hadal's logo, all on pixels: Abyss the anglerfish at 64, and the name HADAL in slanted block
letters whose A's are trenches, the first with Abyss's light glowing in it, set on a finer grid.
FIRMWARE under it is in Hadal's own pixel title font. Both words are cel-shaded as Abyss is, lit
from above. Flipper orange and Abyss's blue-greys, to sit beside Flipper's own look. The light and
dark banners are clear behind, for the README on GitHub's light and dark pages.

Writes SVGs to assets/logo/; tools/render_logo.js turns them into PNGs (headless Chromium, 2x).
"""

import functools
import json
import math
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..", "..")
sys.path.insert(0, HERE)

from title_font import GLYPHS, glyph_rows  # noqa: E402
import abyss_vector  # noqa: E402

ORANGE = "#FF8200"
INK = "#121212"
PAPER = "#F4F4F2"
NIGHT = "#0D0E12"
ABYSS = "#07080B"  # inside the mouth, the pupil
SKEW = math.tan(math.radians(-14))

# ------------------------------------------------------------------ Abyss, in pixels

# Abyss, Hadal's anglerfish: drawn as a vector (abyss_vector.py; abyss_vector_small.py for the
# 32-pixel cut, with bigger features), then set on pixel grids of 64, 48 and 32 and finished by
# hand (abyss_pixels.json). Comic style: ink outline, flat cel shading, lit by its own light.
with open(os.path.join(HERE, "abyss_pixels.json")) as _pixels:
    PIXELS = json.load(_pixels)

PALETTES = {
    # the deep: blue-grey body, Flipper orange light
    "deep": {"#": "#0B0D12", "s": "#1F2A3A", "m": "#35506B", "l": "#6E95B0", "p": "#E8F1F6", "o": "#FF8200", "c": "#FFE2B8"},
    # duotone olive, after the comic art Hadal takes its look from
    "olive": {"#": "#15180A", "s": "#55621F", "m": "#93A23B", "l": "#D2DD72", "p": "#F2F5C8", "o": "#FFB12E", "c": "#FFF1B8"},
}


def abyss(x, y, px, size="64", palette="deep", dark=False, glow_id="glow"):
    """Abyss at its top left x, y, each pixel px wide. On a dark background its outline gets a
    light edge, like a sticker, so it still shows."""
    rows = PIXELS[size]
    colours = PALETTES[palette]
    cells = {}
    for r, row in enumerate(rows):
        for c, ch in enumerate(row):
            if ch != ".":
                cells.setdefault(ch, []).append((c, r))
    out = [f'<g transform="translate({x},{y})" shape-rendering="crispEdges">']
    light = cells.get("o", []) + cells.get("c", [])
    if light:
        cx = (min(c for c, _ in light) + max(c for c, _ in light) + 1) / 2 * px
        cy = (min(r for _, r in light) + max(r for _, r in light) + 1) / 2 * px
        out.append(
            f'<circle cx="{cx}" cy="{cy}" r="{px * len(rows) / 5}" fill="url(#{glow_id})" shape-rendering="auto"/>'
        )
    if dark:
        # above the head (the lure, its glow marks) the ink turns light instead of being edged
        lure_rows = round(len(rows) * 0.24)
        # and so do the light's rays, the few ink pixels standing apart from him
        drawn = {p for ps in cells.values() for p in ps}
        rays = set()
        for p in drawn:
            group, todo = {p}, [p]
            while todo and len(group) <= 6:
                c, r = todo.pop()
                for q in ((c + dc, r + dr) for dc in (-1, 0, 1) for dr in (-1, 0, 1)):
                    if q in drawn and q not in group:
                        group.add(q)
                        todo.append(q)
            if len(group) <= 6:
                rays |= group
        above = [p for p in cells.get("#", []) if p[1] < lure_rows or p in rays]
        cells["#"] = [p for p in cells.get("#", []) if p[1] >= lure_rows and p not in rays]
        out.append(_squares(above, px, "#E9E9E6"))
        filled = {p for k, ps in cells.items() for p in ps if p[1] >= lure_rows or k in "oc"}
        edge = {
            (c + dc, r + dr)
            for c, r in filled
            for dc in (-1, 0, 1)
            for dr in (-1, 0, 1)
            if (c + dc, r + dr) not in filled
        }
        out.append(_squares(sorted(edge), px, "#E9E9E6"))
    for ch, ps in cells.items():
        out.append(_squares(ps, px, colours[ch]))
    out.append("</g>")
    return "\n".join(out)


def abyss_vector_art(x, y, size, palette="deep", dark=False, ids=""):
    """Abyss as the vector drawing (abyss_vector.py), size pixels square at x, y: the mascot for
    chat avatars. On a dark page he gets a light edge, like a sticker."""
    p = "abyss" if palette == "deep" else "olive"
    edge = "#E9E9E6" if dark else None
    return (
        f'<g transform="translate({x},{y}) scale({size / 400})">'
        + abyss_vector.body(p, False, ids, edge)
        + "</g>"
    )


def abyss_one_bit(size="48"):
    """Abyss as the Flipper's screen shows it, the way Flipper's own art is drawn: the ink, the
    shadow as a checker dither, everything else the lit screen; its light is solid"""
    rows = PIXELS[size]
    dark = set()
    for r, row in enumerate(rows):
        for c, ch in enumerate(row):
            if ch in "#o" or (ch == "s" and (c + r) % 2 == 0):
                dark.add((c, r))
    return dark


def _squares(cells, px, colour):
    d = "".join(f"M{c * px} {r * px}h{px}v{px}h-{px}z" for c, r in cells)
    return f'<path fill="{colour}" d="{d}"/>'


def defs(paper):
    return f"""<defs>
<radialGradient id="glow"><stop offset="0" stop-color="{ORANGE}" stop-opacity="0.8"/><stop offset="0.45" stop-color="{ORANGE}" stop-opacity="0.25"/><stop offset="1" stop-color="{ORANGE}" stop-opacity="0"/></radialGradient>
</defs>"""


# ------------------------------------------------------------------ HADAL, drawn at cap height 100

LETTERS = [
    # (path, advance)
    ("M10 0h16v37h36v-37h26v100h-26v-37h-36v37h-26v-90z", 88 + 14),
    ("M0 100 L34 0 H64 L98 100 H70 L49 36 L28 100 Z", 98 + 8),
    ("M0 0 H62 L90 28 V72 L62 100 H0 Z M26 26 V74 H52 L64 62 V38 L52 26 Z", 90 + 12),
    ("M0 100 L34 0 H64 L98 100 H70 L49 36 L28 100 Z", 98 + 10),
    ("M0 0h26v74h54v16l-10 10h-70z", 80),
]
FIRST_TRENCH = 1  # the A the light hangs in
TRENCH_X = 49  # the middle of an A's trench

# ...then set on pixels, so it sits with Abyss and FIRMWARE: 27 tall, fine enough to stay the
# drawn letters, just stepped on its slopes and corners. Abyss's light glows in the first A's
# trench: 5 pixels round, as wide as the drawn one, where the trench leaves a pixel either side.
# (A pixel-wide lure down a trench this narrow reads as two slivers, so on pixels it has none.)
WORD_ROWS = 27
LIGHT_ROW = 21  # the light's middle row
LIGHT = (".###.", "#####", "#####", "#####", ".###.")
LIGHT_CORE = (1, 1)  # its brightest pixel, up and to the left, as on Abyss's light
WORD_SHADOW = 8  # its bottom rows in shadow, as Abyss's belly is
TEXT_SHADOW = 2  # FIRMWARE's, of its 9


def _outlines(d):
    """The closed outlines of an SVG path of M L H V C Z (either case), curves as short lines"""
    tokens = re.findall(r"[MLHVCZmlhvcz]|-?(?:\d+\.?\d*|\.\d+)", d)
    polys, poly = [], []
    x = y = sx = sy = 0.0
    cmd, i = None, 0
    while i < len(tokens):
        if tokens[i].isalpha():
            cmd = tokens[i]
            i += 1
            if cmd in "Zz":
                polys.append(poly)
                poly, x, y = [], sx, sy
            continue
        rel = cmd.islower()
        n = {"m": 2, "l": 2, "h": 1, "v": 1, "c": 6}[cmd.lower()]
        v = [float(t) for t in tokens[i : i + n]]
        i += n
        ox, oy = (x, y) if rel else (0.0, 0.0)
        if cmd in "Mm":
            x, y = ox + v[0], oy + v[1]
            sx, sy = x, y
            poly = [(x, y)]
        elif cmd in "Ll":
            x, y = ox + v[0], oy + v[1]
            poly.append((x, y))
        elif cmd in "Hh":
            x = ox + v[0]
            poly.append((x, y))
        elif cmd in "Vv":
            y = oy + v[0]
            poly.append((x, y))
        else:
            p0 = (x, y)
            p1, p2, p3 = [(ox + v[k], oy + v[k + 1]) for k in (0, 2, 4)]
            for s in range(1, 13):
                t = s / 12
                poly.append(
                    tuple(
                        (1 - t) ** 3 * a + 3 * (1 - t) ** 2 * t * b + 3 * (1 - t) * t * t * c + t**3 * d
                        for a, b, c, d in zip(p0, p1, p2, p3)
                    )
                )
            x, y = p3
    return [p for p in polys if p]


def _inside(polys, px, py):
    """Even-odd, as the letters are filled"""
    inside = False
    for poly in polys:
        for (x1, y1), (x2, y2) in zip(poly, poly[1:] + poly[:1]):
            if (y1 > py) != (y2 > py) and px < x1 + (py - y1) * (x2 - x1) / (y2 - y1):
                inside = not inside
    return inside


def _pixels(polys, unit, rows, samples=6):
    """The cells, unit wide, that the outlines cover at least half of"""
    cols = math.ceil(max(x for p in polys for x, _ in p) / unit) + 1
    need = samples * samples / 2
    on = set()
    for r in range(rows):
        for c in range(cols):
            hits = sum(
                _inside(polys, (c + (i + 0.5) / samples) * unit, (r + (j + 0.5) / samples) * unit)
                for i in range(samples)
                for j in range(samples)
            )
            if hits >= need:
                on.add((c, r))
    return on


def cel_shade(cells, shadow_from):
    """Lit from above, as Abyss is: each edge that faces up catches the light, the rows from
    shadow_from down are in shadow. Returns (lit, body, shadow)."""
    shadow = {p for p in cells if p[1] >= shadow_from}
    lit = {(c, r) for c, r in cells if (c, r - 1) not in cells} - shadow
    return lit, cells - lit - shadow, shadow


def _runs(cells, px, x0=0.0):
    """The cells as one path, a rectangle for each run along a row"""
    d = []
    for r in sorted({r for _, r in cells}):
        cs = sorted(c for c, rr in cells if rr == r)
        start = prev = cs[0]
        for c in cs[1:] + [None]:
            if c != prev + 1:
                d.append(f"M{x0 + start * px:g} {r * px:g}h{(prev - start + 1) * px:g}v{px:g}h-{(prev - start + 1) * px:g}z")
                if c is not None:
                    start = c
            if c is not None:
                prev = c
    return "".join(d)


@functools.lru_cache(maxsize=None)
def hadal_pixels():
    """HADAL on its pixels: the letters' cells and the light's (its core apart), in pixels"""
    unit = 100 / WORD_ROWS
    cells, cursor, mid = set(), 0, None
    for i, (path, advance) in enumerate(LETTERS):
        at = round(cursor / unit)
        polys = _outlines(path)
        if path == LETTERS[FIRST_TRENCH][0]:
            # an A's trench centred on a column, so it steps the same on both sides
            column = round(TRENCH_X / unit - 0.5)
            shift = (column + 0.5) * unit - TRENCH_X
            polys = [[(x + shift, y) for x, y in p] for p in polys]
            if i == FIRST_TRENCH:
                mid = at + column
        cells |= {(at + c, r) for c, r in _pixels(polys, unit, WORD_ROWS)}
        cursor += advance
    top = LIGHT_ROW - len(LIGHT) // 2
    light = {
        (mid - len(row) // 2 + c, top + r) for r, row in enumerate(LIGHT) for c, on in enumerate(row) if on == "#"
    }
    core = {(mid - len(LIGHT[0]) // 2 + LIGHT_CORE[0], top + LIGHT_CORE[1])}
    return cells - light, light - core, core, (mid + 0.5, LIGHT_ROW + 0.5)


def skew_point(x, y):
    return x + y * SKEW, y


def wordmark(x, y, px, shades, palette="deep", glow_id="glow"):
    """HADAL on its pixels, px each, slanted and cel-shaded; shades are (lit, body, shadow), the
    light in the palette's colours.
    Returns the SVG and its width (to the L's foot, unslanted)."""
    cells, light, core, (lx, ly) = hadal_pixels()
    out = [f'<g transform="translate({x},{y})">']
    gx, gy = skew_point(lx * px, ly * px)
    out.append(f'<circle cx="{gx:.1f}" cy="{gy:.1f}" r="{px * 11}" fill="url(#{glow_id})"/>')
    out.append('<g transform="skewX(-14)">')
    for colour, part in zip(shades, cel_shade(cells, WORD_ROWS - WORD_SHADOW)):
        out.append(f'<path fill="{colour}" d="{_runs(part, px)}"/>')
    out.append(f'<path fill="{PALETTES[palette]["o"]}" d="{_runs(light, px)}"/>')
    out.append(f'<path fill="{PALETTES[palette]["c"]}" d="{_runs(core, px)}"/>')
    out.append("</g></g>")
    return "\n".join(out), (max(c for c, _ in cells) + 1) * px


def pixel_text(text, x, y, px, colour, spacing=2):
    """Text in Hadal's title font, as squares, slanted like the name"""
    rects = []
    cursor = 0
    for ch in text:
        rows = glyph_rows(GLYPHS[ch])
        for r, row in enumerate(rows):
            for c, on in enumerate(row):
                if on == "#":
                    rects.append(f"M{(cursor + c) * px} {r * px}h{px}v{px}h-{px}z")
        cursor += len(rows[0]) + spacing
    width = (cursor - spacing) * px
    return (
        f'<g transform="translate({x},{y}) skewX(-14)"><path fill="{colour}" d="{"".join(rects)}"/></g>',
        width,
    )


def pixel_text_gaps(text, x, y, px, shades, gap):
    """Like pixel_text, with a gap (in pixels, may be fractional) between letters, and cel-shaded
    as HADAL is; shades are (lit, body, shadow)"""
    parts = [[], [], []]
    cursor = 0.0
    for ch in text:
        rows = glyph_rows(GLYPHS[ch])
        cells = {(c, r) for r, row in enumerate(rows) for c, on in enumerate(row) if on == "#"}
        for k, part in enumerate(cel_shade(cells, len(rows) - TEXT_SHADOW)):
            if part:
                parts[k].append(_runs(part, px, cursor * px))
        cursor += len(rows[0]) + gap
    paths = "".join(f'<path fill="{colour}" d="{"".join(d)}"/>' for colour, d in zip(shades, parts))
    return f'<g transform="translate({x:.1f},{y}) skewX(-14)">{paths}</g>'


ORANGES = ("#FFA647", ORANGE, "#D66400")  # FIRMWARE: lit, body, shadow

THEMES = {
    # paper (None: clear, for GitHub's light and dark pages alike), HADAL and FIRMWARE as
    # (lit, body, shadow), the fish's palette, the fish on dark
    # light: HADAL in Abyss's own blue-greys, so the name and the fish are one piece
    "light": (None, ("#4A6A88", "#1F2A3A", "#0B0D12"), ORANGES, "deep", False),
    "dark": (None, ("#FFFFFF", "#E8F1F6", "#BCCCD8"), ORANGES, "deep", True),
    "olive": ("#1D210C", ("#F7F9DC", "#E3E9A6", "#B5BE63"), ("#FFCB6B", "#FFB12E", "#D18A12"), "olive", True),
}

FISH_PX = 6  # Abyss at 64 pixels, each 6 wide: FIRMWARE's pixel
WORD_PX = 4  # HADAL's finer pixel
TEXT_PX = 6


def banner(theme="light"):
    paper, word_shades, text_shades, palette, dark = THEMES[theme]
    w, h = 1280, 400
    parts = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" viewBox="0 0 {w} {h}">']
    parts.append(defs(paper))
    if paper:
        parts.append(f'<rect width="{w}" height="{h}" rx="28" fill="{paper}"/>')
    # what is drawn of Abyss: columns 2-63, rows 6-57 of his 64; the name sits beside his body
    fish_w, fish_top, fish_h = 62 * FISH_PX, 6 * FISH_PX, 52 * FISH_PX
    word_h = WORD_ROWS * WORD_PX
    text_gap = 24
    block_h = word_h + text_gap + 9 * TEXT_PX
    word_w = (max(c for c, _ in hadal_pixels()[0]) + 1) * WORD_PX
    gap = 72
    # centred: from Abyss's tail to the top of HADAL's last letter (the slant pulls its foot left)
    total = fish_w + gap + word_w
    fish_x = (w - total) / 2 - 2 * FISH_PX
    fish_y = (h - fish_h) / 2 - fish_top
    word_x = fish_x + 2 * FISH_PX + fish_w + gap
    word_y = round(fish_y + 36 * FISH_PX - block_h / 2)
    parts.append(abyss(round(fish_x), round(fish_y), FISH_PX, "64", palette, dark=dark))
    mark, word_w = wordmark(round(word_x), word_y, WORD_PX, word_shades, palette)
    parts.append(mark)
    # FIRMWARE spans HADAL exactly, its edges carrying on HADAL's slant
    top = word_y + word_h + text_gap
    left = round(word_x) + (top - word_y) * SKEW
    right = left + word_w
    widths = [len(glyph_rows(GLYPHS[ch])[0]) for ch in "FIRMWARE"]
    spacing = ((right - left) / TEXT_PX - sum(widths)) / (len(widths) - 1)
    parts.append(pixel_text_gaps("FIRMWARE", left, top, TEXT_PX, text_shades, spacing))
    parts.append("</svg>")
    return "\n".join(parts) + "\n"


def avatar(dark=True):
    paper, ink = (NIGHT, "#F4F4F2") if dark else (PAPER, INK)
    body = "#1C1E26" if dark else "#FFFFFF"
    size = 512
    parts = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{size}" height="{size}" viewBox="0 0 {size} {size}">']
    parts.append(defs(paper))
    parts.append(f'<rect width="{size}" height="{size}" rx="112" fill="{paper}"/>')
    # centred on what is drawn (x 38-396, y 30-340 of his 400 box), with room all round
    art = 430
    scale = art / 400
    parts.append(
        abyss_vector_art(size / 2 - 217 * scale, size / 2 - 185 * scale, art, "deep", dark=dark, ids="a")
    )
    parts.append("</svg>")
    return "\n".join(parts) + "\n"


def main():
    out = os.path.join(ROOT, "assets", "logo")
    os.makedirs(out, exist_ok=True)
    files = {
        "hadal-logo.svg": banner("light"),
        "hadal-logo-dark.svg": banner("dark"),
        "hadal-logo-olive.svg": banner("olive"),
        "hadal-avatar.svg": avatar(True),
        "hadal-avatar-light.svg": avatar(False),
    }
    for name, text in files.items():
        with open(os.path.join(out, name), "w") as f:
            f.write(text)
    print("\n".join(files))


if __name__ == "__main__":
    main()
