#!/usr/bin/env python3
"""Hadal's logo, drawn as vectors: Abyss the anglerfish, and the name HADAL in slanted block
letters whose A's are trenches, the first with Abyss's light hanging in it. FIRMWARE under it is in
Hadal's own pixel title font. Flipper orange and ink, to sit beside Flipper's own look.

Writes SVGs to assets/logo/; tools/render_logo.js turns them into PNGs (headless Chromium).
"""

import json
import math
import os
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
        above = [p for p in cells.get("#", []) if p[1] < lure_rows]
        cells["#"] = [p for p in cells.get("#", []) if p[1] >= lure_rows]
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
    the web, the README and chat avatars. On a dark page he gets a light edge, like a sticker."""
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


# ------------------------------------------------------------------ HADAL, cap height 100

LETTERS = [
    # (path, advance)
    ("M10 0h16v37h36v-37h26v100h-26v-37h-36v37h-26v-90z", 88 + 14),
    ("M0 100 L34 0 H64 L98 100 H70 L49 36 L28 100 Z", 98 + 8),
    ("M0 0 H62 L90 28 V72 L62 100 H0 Z M26 26 V74 H52 L64 62 V38 L52 26 Z", 90 + 12),
    ("M0 100 L34 0 H64 L98 100 H70 L49 36 L28 100 Z", 98 + 10),
    ("M0 0h26v74h54v16l-10 10h-70z", 80),
]
FIRST_TRENCH = 1  # the A the light hangs in


def skew_point(x, y):
    return x + y * SKEW, y


def wordmark(x, y, scale, ink=INK, glow_id="glow"):
    out = [f'<g transform="translate({x},{y}) scale({scale})">']
    out.append(f'<g transform="skewX(-14)" fill="{ink}" fill-rule="evenodd">')
    cursor = 0
    trench_x = 0
    for i, (path, advance) in enumerate(LETTERS):
        out.append(f'<path transform="translate({cursor},0)" d="{path}"/>')
        if i == FIRST_TRENCH:
            trench_x = cursor
        # the lure grows out of the trench's top: part of the letter, its colour and its slant
        if i == FIRST_TRENCH:
            out.append(
                f'<path transform="translate({cursor},0)" '
                'd="M45 36 H53 C52 44 51.2 52 50.6 62 H47.4 C46.8 52 46 44 45 36 Z"/>'
            )
        cursor += advance
    out.append("</g>")
    # the light hangs at the end of it, round (drawn unslanted)
    ox, oy = skew_point(trench_x + 49, 70)
    out.append(f'<circle cx="{ox:.1f}" cy="{oy:.1f}" r="40" fill="url(#{glow_id})"/>')
    out.append(f'<circle cx="{ox:.1f}" cy="{oy:.1f}" r="9.5" fill="{ORANGE}"/>')
    out.append(f'<circle cx="{ox - 3:.1f}" cy="{oy - 3:.1f}" r="2.6" fill="#FFFFFF"/>')
    out.append("</g>")
    return "\n".join(out), cursor


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


def pixel_text_gaps(text, x, y, px, colour, gap):
    """Like pixel_text, with a gap (in pixels, may be fractional) between letters"""
    rects = []
    cursor = 0.0
    for ch in text:
        rows = glyph_rows(GLYPHS[ch])
        for r, row in enumerate(rows):
            for c, on in enumerate(row):
                if on == "#":
                    rects.append(f"M{(cursor + c) * px:.1f} {r * px}h{px}v{px}h-{px}z")
        cursor += len(rows[0]) + gap
    return f'<g transform="translate({x:.1f},{y}) skewX(-14)"><path fill="{colour}" d="{"".join(rects)}"/></g>'


THEMES = {
    # paper, ink, FIRMWARE, the fish's palette, the fish on dark
    "light": (PAPER, INK, ORANGE, "deep", False),
    "dark": (NIGHT, "#F4F4F2", ORANGE, "deep", True),
    "olive": ("#1D210C", "#E3E9A6", "#FFB12E", "olive", True),
}


def banner(theme="light"):
    paper, ink, accent, palette, dark = THEMES[theme]
    w, h = 1280, 420
    parts = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" viewBox="0 0 {w} {h}">']
    parts.append(defs(paper))
    parts.append(f'<rect width="{w}" height="{h}" rx="28" fill="{paper}"/>')
    # the vector Abyss: the professional face, where the screen's pixels are not the point
    parts.append(abyss_vector_art(44, 24, 380, palette, dark=dark, ids="b"))
    word_x, word_y, word_scale = 480, 130, 1.08
    mark, word_w = wordmark(word_x, word_y, word_scale, ink=ink)
    parts.append(mark)
    # FIRMWARE spans HADAL exactly, its edges carrying on HADAL's slant
    top = 262
    left = word_x + (top - word_y) * SKEW
    right = word_x + (word_w * word_scale) + (top - word_y) * SKEW
    widths = [len(glyph_rows(GLYPHS[ch])[0]) for ch in "FIRMWARE"]
    px = 6
    gap = ((right - left) / px - sum(widths)) / (len(widths) - 1)
    parts.append(pixel_text_gaps("FIRMWARE", left, top, px, accent, gap))
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
