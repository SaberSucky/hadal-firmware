"""The first-start slideshow and the "firmware updated" slideshow: full screens, 128x64.

A small anglerfish tells the story from one side of the screen and a speech bubble from the other,
the way the stock slideshow does. The water is dark; the bubble is pale with dark text, in the
firmware's own fonts so that it matches the rest of the interface.
"""

import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "scripts"))

from flipper.assets import pack, u8g2font  # noqa: E402
from PIL import Image, ImageDraw  # noqa: E402
from pixel import BAYER4, Canvas  # noqa: E402

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


# ---------------------------------------------------------------------------
# The mascot
# ---------------------------------------------------------------------------


def _bezier(p0, p1, p2, steps=12):
    out = []
    for s in range(steps + 1):
        u = s / steps
        out.append(
            (
                round((1 - u) ** 2 * p0[0] + 2 * (1 - u) * u * p1[0] + u * u * p2[0]),
                round((1 - u) ** 2 * p0[1] + 2 * (1 - u) * u * p1[1] + u * u * p2[1]),
            )
        )
    return out


def mascot_layer(eye="open", mouth="smile", glow=2, fin=0):
    """The anglerfish's head facing right, cut off by the left edge of the screen. Returns a
    canvas where lit pixels are the pale parts, plus a silhouette canvas to dim what is behind."""
    pale = Canvas(128, 64)
    body = Canvas(128, 64)

    # head
    body.ellipse(-14, 10, 48, 62, fill=True, outline=True)
    pale.ellipse(-14, 10, 48, 62, fill=False, outline=True)
    pale.dither(0, 44, 36, 58, 3, offset=(1, 0))  # paler throat
    pale.dither(0, 14, 24, 24, 1)  # speckled brow

    # the lure
    stalk = _bezier((24, 11), (30, -6), (47, 3))
    pale.polyline(stalk)
    bx, by = 47, 3
    for y in range(by - 9, by + 10):
        for x in range(bx - 9, bx + 10):
            d = math.hypot(x - bx, y - by)
            if 3 <= d <= glow + 5 and BAYER4[y % 4][x % 4] < round(7 * (1 - d / (glow + 6))):
                pale.dot(x, y)
    pale.ellipse(bx - 2, by - 2, bx + 2, by + 2, fill=True, outline=True)

    # fin
    flap = [(10, 44), (2, 52 - fin), (14, 54)]
    body.polygon(flap)
    pale.polygon(flap, fill=False, outline=True)

    # eye
    if eye == "squint":
        pale.polyline([(23, 29), (29, 23), (35, 29)])
        pale.polyline([(24, 30), (29, 25), (34, 30)])
    elif eye == "shades":
        pale.rect(20, 20, 39, 31)  # dark lenses in a pale frame, with a glint on each
        pale.line(29, 21, 29, 30)
        pale.line(22, 28, 24, 22)
        pale.line(32, 28, 34, 22)
        pale.line(20, 22, 12, 20)  # the arm of the glasses
    else:
        pale.ellipse(23, 19, 37, 33, fill=False, outline=True)
        px = 31 if eye == "look" else 29
        pale.ellipse(px - 2, 24, px + 2, 28, fill=True, outline=False)
        body.ellipse(px - 2, 24, px + 2, 28, fill=True, outline=False)
        pale.dot(px + 1, 24, False)

    # mouth: a wide mouth with fangs that opens when it talks
    open_by = {"smile": 0, "talk": 4, "wide": 7}[mouth]
    upper = [(30, 40), (48, 37)]
    lower = [(30, 45), (51, 46 + open_by)]
    for px, py in [(x, y) for x in range(31, 53) for y in range(34, 56 + open_by)]:
        if _in_quad([upper[0], upper[1], lower[1], lower[0]], px, py):
            pale.dot(px, py, False)
    pale.polyline(upper)
    pale.polyline(lower)
    pale.polyline([lower[1], (47, 53 + open_by), (32, 56)])
    for k in range(3):
        ux = 35 + k * 5
        uy = round(40 + (37 - 40) * (ux - 30) / 18)
        pale.line(ux, uy, ux, uy + 3)
        lx = 37 + k * 5
        ly = round(45 + (46 + open_by - 45) * (lx - 30) / 21)
        pale.line(lx, ly, lx, ly - 3)
    return pale, body


def _in_quad(poly, x, y):
    inside = False
    n = len(poly)
    for i in range(n):
        x0, y0 = poly[i]
        x1, y1 = poly[(i + 1) % n]
        if (y0 > y) != (y1 > y) and x < (x1 - x0) * (y - y0) / (y1 - y0) + x0:
            inside = not inside
    return inside


def _flipped(canvas):
    c = Canvas(canvas.width, canvas.height)
    c.image = canvas.image.transpose(Image.FLIP_LEFT_RIGHT)
    c.draw = ImageDraw.Draw(c.image)
    return c


def sea(seed):
    c = Canvas(128, 64, lit=True)
    # drifting specks
    import random

    rng = random.Random(seed)
    for _ in range(26):
        c.dot(rng.randrange(128), rng.randrange(64), False)
    return c


def bubble(canvas, x, y, lines, slot="Secondary", leading=11, tail=None):
    """A pale speech bubble with a dark border and a one-pixel shadow, sized to its text."""
    width = max(line_width(line, slot) for line in lines) + 10
    height = leading * len(lines) + 4
    canvas.rect(x + 1, y + 1, x + width, y + height, lit=False, fill=True)  # shadow, drawn pale
    canvas.rect(x + 1, y + 1, x + width, y + height, lit=True, fill=True)
    canvas.rect(x, y, x + width - 1, y + height - 1, lit=False, fill=True)
    canvas.rect(x, y, x + width - 1, y + height - 1, lit=True)
    for dx, dy in ((0, 0), (width - 1, 0), (0, height - 1), (width - 1, height - 1)):
        canvas.dot(x + dx, y + dy, False)  # rounded corners
    text_lines(canvas, x + 5, y + 8, lines, slot, leading, lit=True)
    return width, height


def story_frame(index, side, lines, **pose):
    scene = sea(index + 40)
    pale, body = mascot_layer(**pose)
    if side == "right":
        pale, body = _flipped(pale), _flipped(body)
    for y in range(64):
        for x in range(128):
            if body.get(x, y):
                scene.dot(x, y, True)
            if pale.get(x, y):
                scene.dot(x, y, False)
    width = max(line_width(line) for line in lines) + 10
    height = 11 * len(lines) + 4
    x = 127 - width - 1 if side == "left" else 1
    y = max(2, (64 - height) // 2 - 2)
    bubble(scene, x, y, lines)
    return scene


FIRST_START = [
    ("left", ["Welcome to", "the deep. Press", "> to continue"], dict(eye="open", mouth="smile", glow=3)),
    ("right", ["Allow me", "to introduce", "myself >"], dict(eye="squint", mouth="talk", glow=2)),
    ("left", ["I am Hadal, a", "cyberfish living", "in your pocket >"], dict(eye="open", mouth="talk", glow=4)),
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
    font = stock_font("Secondary")
    width = font.text_width(label) + 14
    x, y = 126 - width, 52
    canvas.rect(x, y, x + width, y + 11, lit=False, fill=True)
    canvas.rect(x, y, x + width, y + 11, lit=True)
    canvas.rect(x + 1, y + 1, x + width - 1, y + 10, lit=True)
    canvas.rect(x + 2, y + 2, x + width - 2, y + 9, lit=False, fill=True)
    font.draw_text(canvas.image, x + 5, y + 8, label, lit=0)
    # the arrow head, as on the stock buttons
    ax = x + width - 7
    for k in range(4):
        canvas.line(ax + k, y + 3 + k, ax + k, y + 8 - k, lit=True)


def update_frames():
    frames = []

    # 1: headline and the mascot
    scene = sea(90)
    pale, body = mascot_layer(eye="open", mouth="smile", glow=3)
    for y in range(64):
        for x in range(128):
            if body.get(x, y):
                scene.dot(x, y, True)
            if pale.get(x, y):
                scene.dot(x, y, False)
    big = stock_font("Primary")
    text = "HADAL"
    x = 126 - big.text_width(text) - 2
    for dx in (0, 1):  # a doubled stroke for weight
        big.draw_text(scene.image, x + dx, 22, text, lit=1)
    text_lines(scene, 70, 36, ["Firmware", "Updated"], "Primary", 11, lit=False)
    _next_button(scene)
    frames.append(scene)

    # 2: what it is
    scene = sea(91)
    pale, body = mascot_layer(eye="look", mouth="talk", glow=2)
    pale, body = _flipped(pale), _flipped(body)
    for y in range(64):
        for x in range(128):
            if body.get(x, y):
                scene.dot(x, y, True)
            if pale.get(x, y):
                scene.dot(x, y, False)
    bubble(scene, 2, 2, ["Hadal is free", "and open source", "(GPL). Beware", "of scammers"])
    _next_button(scene)
    frames.append(scene)

    # 3: where to get it
    scene = sea(92)
    text_lines(scene, 8, 12, ["Source and updates:"], "Secondary", 11, lit=False)
    scene.rect(6, 17, 84, 49, lit=False)
    scene.rect(7, 18, 83, 48, lit=False)
    text_lines(scene, 12, 28, ["github.com/", "SaberSucky/", "hadal-firmware"], "Secondary", 9, lit=False)
    # a few bubbles rising on the right
    for bx, by, r in ((100, 58, 2), (108, 46, 1), (104, 36, 3), (114, 26, 1), (98, 22, 1)):
        scene.ellipse(bx - r, by - r, bx + r, by + r, lit=False, fill=False, outline=True)
    _next_button(scene)
    frames.append(scene)

    # 4: credit where it is due
    scene = sea(93)
    text_lines(scene, 8, 14, ["Built on the shoulders of"], "Secondary", 11, lit=False)
    text_lines(scene, 8, 27, ["Unleashed firmware", "by DarkFlippers,", "and the Flipper team"], "Secondary", 11, lit=False)
    _next_button(scene, "OK")
    frames.append(scene)
    return frames
