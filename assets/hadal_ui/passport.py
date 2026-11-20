#!/usr/bin/env python3
"""The passport's background, 128x64, drawn here: '#' lit.

The passport (Settings > Passport) draws this whole, then the portrait and the text over it, in
the layout asset packs' passport backgrounds are drawn for: the portrait (46x49) at 11,2, the
name, mood and level from x 60 on (baselines 9, 21 and 41), the experience bar inside 60,44 to
122,50. A pack's Passport/passport_128x64 takes this one's place.

Run to write assets/icons/Passport/passport_128x64.png, with --preview PNG to see it with a
portrait and text in place.
"""

import os
import sys

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..", "..")
sys.path.insert(0, HERE)

from title_font import GLYPHS, glyph_rows  # noqa: E402

W, H = 128, 64


def title(draw, x, baseline, text):
    """Hadal's title font, as the firmware draws it"""
    for ch in text:
        if ch == " ":
            x += 4
            continue
        rows = glyph_rows(GLYPHS[ch.upper()])
        top = baseline - len(rows) + 1
        for y, row in enumerate(rows):
            for dx, c in enumerate(row):
                if c == "#":
                    draw.point((x + dx, top + y), 0)
        x += len(rows[0]) + 1
    return x


def dotted(draw, x0, x1, y):
    for x in range(x0, x1 + 1, 2):
        draw.point((x, y), 0)


def background():
    """Rows and columns are chosen so that every text keeps a clear pixel from every line: the
    name's capitals on rows 2-8 (baseline 9), the mood's on 14-20 (21), the level's on 34-40 (41),
    each with its descenders a row lower, all from column 60."""
    image = Image.new("1", (W, H), 1)
    d = ImageDraw.Draw(image)
    # the card, with the portrait's frame (11,2 46x49) inside it from top to bottom
    d.rounded_rectangle((0, 0, W - 1, 52), radius=4, outline=0)
    d.line((9, 0, 9, 52), fill=0)
    d.line((58, 0, 58, 52), fill=0)
    # rules under the name and the mood, and over the level
    dotted(d, 60, 124, 12)
    dotted(d, 60, 124, 24)
    dotted(d, 60, 124, 32)
    # a swell between them, for the depths
    for x in range(60, 125):
        phase = (x // 3) % 4
        d.point((x, 27 + (0 if phase in (0, 3) else 1 if phase == 1 else 2)), 0)
    # the experience bar's well
    d.rectangle((60, 44, 122, 50), outline=0)
    # the document's name under the card, centred, a clear row above and below
    text = "HADAL PASSPORT"
    width = sum(len(glyph_rows(GLYPHS[c])[0]) + 1 for c in text if c != " ") + 4 * text.count(" ") - 1
    title(d, (W - width) // 2, 62, text)
    return image


def main(argv):
    out = os.path.join(ROOT, "assets", "icons", "Passport", "passport_128x64.png")
    image = background()
    image.save(out)
    if len(argv) > 2 and argv[1] == "--preview":
        preview = image.convert("RGB")
        portrait = Image.open(os.path.join(ROOT, "assets", "icons", "Passport", "passport_okay2_46x49.png")).convert("1")
        preview.paste(portrait.convert("RGB"), (11, 2))
        d = ImageDraw.Draw(preview)
        d.rectangle((61, 45, 61 + 40, 49), fill=(0, 0, 0))
        scale = 6
        big = preview.resize((W * scale, H * scale), Image.NEAREST)
        px = big.load()
        for y in range(big.size[1]):
            for x in range(big.size[0]):
                px[x, y] = (20, 12, 0) if px[x, y] == (0, 0, 0) else (255, 130, 0)
        big.save(argv[2])


if __name__ == "__main__":
    main(sys.argv)
