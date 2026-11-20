"""The first-start slideshow and the "firmware updated" slideshow: full screens, 128x64.

Abyss, Hadal's anglerfish (assets/hadal_ui/brand.py), swims in from one side of the screen and a
speech bubble tells the story from the other, the way the stock slideshow does. The water is pale with dark specks; the bubble is
pale with dark text, in the firmware's own fonts so that it matches the rest of the interface.
"""

import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "scripts"))

from flipper.assets import pack, u8g2font  # noqa: E402
from PIL import Image, ImageDraw  # noqa: E402
from pixel import Canvas  # noqa: E402

_FONTS = {}


def stock_font(slot):
    """The firmware's own font for a slot, decoded from lib/u8g2/u8g2_fonts.c."""
    if slot not in _FONTS:
        path = os.path.join(HERE, "..", "..", "lib", "u8g2", "u8g2_fonts.c")
        with open(path, errors="replace") as f:
            text = f.read()
        name = pack.STOCK_FONTS[slot]
        _FONTS[slot] = u8g2font.parse_font(pack.extract_u8g2_font(pack.find_u8g2_font_source(text, name), name))
    return _FONTS[slot]


def text_lines(canvas, x, baseline, lines, slot="Secondary", leading=11, lit=True):
    font = stock_font(slot)
    for i, line in enumerate(lines):
        font.draw_text(canvas.image, x, baseline + i * leading, line, lit=0 if lit else 1)


def line_width(line, slot="Secondary"):
    return stock_font(slot).text_width(line)


# Padding: every text keeps at least this many clear pixels from the edge of its box (and from the
# screen's edges); a frame's own line does not count as room
PAD = 1


def text_bounds(x, baseline, lines, slot="Secondary", leading=11):
    """The box text_lines() inks: (x0, y0, x1, y1), inclusive"""
    probe = Canvas(256, 128)
    text_lines(probe, x + 64, baseline + 32, lines, slot, leading, lit=True)
    px = probe.image.load()
    ink = [(cx, cy) for cy in range(128) for cx in range(256) if px[cx, cy] == 0]
    xs, ys = [p[0] - 64 for p in ink], [p[1] - 32 for p in ink]
    return min(xs), min(ys), max(xs), max(ys)


def open_text(canvas, x, baseline, lines, slot="Secondary", leading=11):
    """Text on open water: the specks around it are cleared first, so none touches a letter"""
    x0, y0, x1, y1 = text_bounds(x, baseline, lines, slot, leading)
    canvas.rect(x0 - PAD, y0 - PAD, x1 + PAD, y1 + PAD, lit=False, fill=True)
    text_lines(canvas, x, baseline, lines, slot, leading, lit=True)
    check_on_screen(f"text {lines[0]!r}", (x0, y0, x1, y1), margin=PAD)


def check_room(what, text, inside):
    """Fail the build when text comes nearer than PAD to the inside edges of its box"""
    tx0, ty0, tx1, ty1 = text
    ix0, iy0, ix1, iy1 = inside
    if tx0 - ix0 < PAD or ty0 - iy0 < PAD or ix1 - tx1 < PAD or iy1 - ty1 < PAD:
        raise ValueError(f"{what}: text {text} has no room inside {inside}")


def check_on_screen(what, box, margin=0):
    x0, y0, x1, y1 = box
    if x0 < margin or y0 < margin or x1 > 127 - margin or y1 > 63 - margin:
        raise ValueError(f"{what}: {box} leaves the screen")


# ---------------------------------------------------------------------------
# The mascot
# ---------------------------------------------------------------------------


def _flipped(canvas):
    c = Canvas(canvas.width, canvas.height)
    c.image = canvas.image.transpose(Image.FLIP_LEFT_RIGHT)
    c.draw = ImageDraw.Draw(c.image)
    return c


def bubble_size(lines, slot="Secondary", leading=11):
    """Width and height of the bubble for these lines: 4 pixels either side of the text, 2 above
    the capitals and 2 below the descenders, inside the border"""
    return max(line_width(line, slot) for line in lines) + 10, leading * len(lines) + 3


def bubble(canvas, x, y, lines, slot="Secondary", leading=11, tail=None):
    """A pale speech bubble with a dark border and a one-pixel shadow, sized to its text."""
    width, height = bubble_size(lines, slot, leading)
    canvas.rect(x + 1, y + 1, x + width, y + height, lit=False, fill=True)  # shadow, drawn pale
    canvas.rect(x + 1, y + 1, x + width, y + height, lit=True, fill=True)
    canvas.rect(x, y, x + width - 1, y + height - 1, lit=False, fill=True)
    canvas.rect(x, y, x + width - 1, y + height - 1, lit=True)
    for dx, dy in ((0, 0), (width - 1, 0), (0, height - 1), (width - 1, height - 1)):
        canvas.dot(x + dx, y + dy, False)  # rounded corners
    text_lines(canvas, x + 5, y + 10, lines, slot, leading, lit=True)
    # inside the border, and with its shadow on the screen
    check_room(f"bubble {lines[0]!r}", text_bounds(x + 5, y + 10, lines, slot, leading), (x + 1, y + 1, x + width - 2, y + height - 2))
    check_on_screen(f"bubble {lines[0]!r}", (x, y, x + width, y + height))
    return width, height


def abyss_layers(size="48", glow=2):
    """Abyss, Hadal's anglerfish, facing right: (ink, mask) canvases the size of his grid, his
    shape filled in the mask so the water's specks do not show through him. glow puts a ring of
    dots around his light, wider for more."""
    sys.path.insert(0, os.path.join(HERE, "..", "hadal_ui"))
    import brand

    rows = brand.PIXELS[size]
    width, height = max(len(r) for r in rows), len(rows)
    ink, mask = Canvas(width, height), Canvas(width, height)
    for x, y in brand.abyss_one_bit(size):
        ink.dot(x, y)
    for y, row in enumerate(rows):
        for x, ch in enumerate(row):
            if ch != ".":
                mask.dot(x, y)
    light = [(x, y) for y, row in enumerate(rows) for x, ch in enumerate(row) if ch == "o"]
    lx = round(sum(x for x, _ in light) / len(light))
    ly = round(sum(y for _, y in light) / len(light))
    radius = 2 + glow
    for k in range(12):
        if k % 2 and glow < 3:
            continue
        angle = k * math.pi / 6
        x, y = lx + round(radius * math.cos(angle)), ly + round(radius * math.sin(angle))
        if not mask.get(x, y):
            ink.dot(x, y)
    return ink, mask


def _bounds(mask):
    xs = [x for x in range(mask.width) for y in range(mask.height) if mask.get(x, y)]
    ys = [y for y in range(mask.height) for x in range(mask.width) if mask.get(x, y)]
    return min(xs), min(ys), max(xs), max(ys)


def fish_layers(side, glow=2, **_):
    """Abyss at one side of the screen, facing the middle: (ink, mask) as 128x64 canvases."""
    ink, mask = abyss_layers("48", glow)
    x0, y0, x1, y1 = _bounds(mask)
    full_ink, full_mask = Canvas(128, 64), Canvas(128, 64)
    y = (64 - (y1 - y0 + 1)) // 2 - y0
    if side == "left":
        # he faces right already
        x = 1 - x0
        full_ink.paste(ink, x, y)
        full_mask.paste(mask, x, y)
        return full_ink, full_mask
    # on the right he turns round, to face the middle
    x = 126 - x1
    full_ink.paste(ink, x, y)
    full_mask.paste(mask, x, y)
    return (
        _mirror_in_place(full_ink, x + x0, x + x1),
        _mirror_in_place(full_mask, x + x0, x + x1),
    )


def _mirror_in_place(canvas, left, right):
    """Mirror the columns left..right in place, so a figure turns round where it stands."""
    c = Canvas(canvas.width, canvas.height)
    for y in range(canvas.height):
        for x in range(canvas.width):
            if canvas.get(x, y):
                c.dot(left + right - x if left <= x <= right else x, y)
    return c


def pale_sea(seed):
    """Pale water with dark specks of marine snow."""
    import random

    c = Canvas(128, 64)
    rng = random.Random(seed)
    for _ in range(30):
        c.dot(rng.randrange(128), rng.randrange(64))
    return c


def place_fish(scene, side, **pose):
    ink, mask = fish_layers(side, **pose)
    scene.paste(mask, 0, 0, mode="erase")
    scene.paste(ink, 0, 0, mode="over")


def story_frame(index, side, lines, **pose):
    scene = pale_sea(index + 40)
    place_fish(scene, side, **pose)
    width, height = bubble_size(lines)
    x = 126 - width if side == "left" else 1
    y = (64 - height) // 2 - 1
    # clear of Abyss, by a pixel at least
    _, mask = fish_layers(side, **pose)
    fx0, fy0, fx1, fy1 = _bounds(mask)
    if side == "left" and x <= fx1 + 1 or side == "right" and x + width >= fx0 - 1:
        raise ValueError(f"bubble {lines[0]!r} runs into Abyss")
    bubble(scene, x, y, lines)
    return scene


FIRST_START = [
    ("left", ["Welcome to", "the deep. Press", "> to continue"], dict(eye="open", mouth="smile", glow=3)),
    ("right", ["Allow me", "to introduce", "myself >"], dict(eye="squint", mouth="talk", glow=2)),
    ("left", ["I'm Abyss, the", "anglerfish in", "your pocket >"], dict(eye="open", mouth="talk", glow=4)),
    ("right", ["I grow stronger", "the more you", "use me >"], dict(eye="shades", mouth="smile", glow=3, fin=4)),
    ("left", ["As long as you", "read, write", "and emulate >"], dict(eye="look", mouth="talk", glow=2)),
    ("right", ["Check my level", "and mood in the", "Passport menu"], dict(eye="squint", mouth="wide", glow=4, fin=6)),
]


def first_start_frames():
    return [story_frame(i, side, lines, **pose) for i, (side, lines, pose) in enumerate(FIRST_START)]


# ---------------------------------------------------------------------------
# "Firmware updated"
# ---------------------------------------------------------------------------


def _next_button(canvas, label="next"):
    """A button in the bottom right corner, as the stock slideshow has: framed, a shadow, the
    label and an arrow head with room all round"""
    font = stock_font("Secondary")
    width = font.text_width(label) + 15
    x0, y0, x1, y1 = 125 - width, 51, 125, 62
    canvas.rect(x0 + 1, y0 + 1, x1 + 1, y1 + 1, lit=True, fill=True)  # shadow
    canvas.rect(x0, y0, x1, y1, lit=False, fill=True)
    canvas.rect(x0, y0, x1, y1, lit=True)
    font.draw_text(canvas.image, x0 + 4, y0 + 9, label, lit=0)
    check_room(f"button {label!r}", text_bounds(x0 + 4, y0 + 9, [label]), (x0 + 1, y0 + 1, x1 - 1, y1 - 1))
    # the arrow head, as on the stock buttons
    ax = x1 - 7
    for k in range(4):
        canvas.line(ax + k, y0 + 3 + k, ax + k, y0 + 8 - k, lit=True)


def update_frames():
    frames = []

    # 1: headline and the mascot
    scene = pale_sea(90)
    place_fish(scene, "right", eye="open", mouth="smile", glow=3)
    big = stock_font("Primary")
    text = "HADAL"
    x0, y0, x1, y1 = text_bounds(4, 22, [text], "Primary")
    scene.rect(x0 - PAD, y0 - PAD, x1 + 1 + PAD, 25 + PAD, lit=False, fill=True)  # with its rule
    for dx in (0, 1):  # a doubled stroke for weight
        big.draw_text(scene.image, 4 + dx, 22, text, lit=0)
    scene.line(4, 25, 4 + big.text_width(text) + 1, 25)
    open_text(scene, 4, 38, ["Firmware", "updated"], "Primary", 11)
    _next_button(scene)
    frames.append(scene)

    # 2: what it is
    scene = pale_sea(91)
    place_fish(scene, "right", eye="look", mouth="talk", glow=2)
    bubble(scene, 2, 2, ["Hadal is free", "and open source", "(GPL). Beware", "of scammers"])
    _next_button(scene)
    frames.append(scene)

    # 3: where to get it
    scene = pale_sea(92)
    open_text(scene, 8, 11, ["Source and updates:"], "Secondary", 11)
    link = ["github.com/", "SaberSucky/", "hadal-firmware"]
    box = (6, 15, 12 + max(line_width(line) for line in link) + 5, 50)
    scene.rect(*box, lit=False, fill=True)
    scene.rect(*box)
    scene.rect(box[0] + 1, box[1] + 1, box[2] - 1, box[3] - 1)
    text_lines(scene, 12, 26, link, "Secondary", 10, lit=True)
    check_room("link", text_bounds(12, 26, link, "Secondary", 10), (box[0] + 2, box[1] + 2, box[2] - 2, box[3] - 2))
    # a few bubbles rising on the right
    for bx, by, r in ((100, 58, 2), (108, 46, 1), (104, 36, 3), (114, 26, 1), (98, 22, 1)):
        scene.ellipse(bx - r, by - r, bx + r, by + r, lit=False, fill=True)
        scene.ellipse(bx - r, by - r, bx + r, by + r, fill=False)
    _next_button(scene)
    frames.append(scene)

    # 4: credit where it is due
    scene = pale_sea(93)
    open_text(scene, 8, 14, ["Built on the shoulders of"], "Secondary", 11)
    open_text(scene, 8, 27, ["Unleashed firmware", "by DarkFlippers,", "and the Flipper team"], "Secondary", 11)
    _next_button(scene, "OK")
    frames.append(scene)
    return frames
