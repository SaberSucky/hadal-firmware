"""The first-start slideshow and the "firmware updated" slideshow: full screens, 128x64.

The anglerfish (angler.py) peeks in from one side of the screen and a speech bubble tells the story
from the other, the way the stock slideshow does. The water is pale with dark specks; the bubble is
pale with dark text, in the firmware's own fonts so that it matches the rest of the interface.
"""

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


# ---------------------------------------------------------------------------
# The mascot
# ---------------------------------------------------------------------------


def _flipped(canvas):
    c = Canvas(canvas.width, canvas.height)
    c.image = canvas.image.transpose(Image.FLIP_LEFT_RIGHT)
    c.draw = ImageDraw.Draw(c.image)
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


# the slideshow's poses, in the anglerfish's terms (angler.py)
_JAW = {"smile": 1, "talk": 3, "wide": 4}


def fish_layers(side, mouth="smile", eye="open", glow=2, fin=0):
    """The anglerfish peeking in from one side of the screen, facing the middle: (ink, mask) as
    128x64 canvases."""
    import angler

    ink, mask = angler.anglerfish(jaw=_JAW.get(mouth, 1), glow=min(3, glow), tail=fin % 3 - 1, eye=eye)
    full_ink, full_mask = Canvas(128, 64), Canvas(128, 64)
    x, y = 84, 12
    full_ink.paste(ink, x, y)
    full_mask.paste(mask, x, y)
    if side == "left":
        full_ink, full_mask = _flipped(full_ink), _flipped(full_mask)
    return full_ink, full_mask


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
    scene = pale_sea(90)
    place_fish(scene, "right", eye="open", mouth="smile", glow=3)
    big = stock_font("Primary")
    text = "HADAL"
    for dx in (0, 1):  # a doubled stroke for weight
        big.draw_text(scene.image, 4 + dx, 22, text, lit=0)
    scene.line(4, 25, 4 + big.text_width(text) + 1, 25)
    text_lines(scene, 4, 38, ["Firmware", "updated"], "Primary", 11, lit=True)
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
    text_lines(scene, 8, 12, ["Source and updates:"], "Secondary", 11, lit=True)
    scene.rect(6, 17, 84, 49, lit=False, fill=True)
    scene.rect(6, 17, 84, 49)
    scene.rect(7, 18, 83, 48)
    text_lines(scene, 12, 28, ["github.com/", "SaberSucky/", "hadal-firmware"], "Secondary", 9, lit=True)
    # a few bubbles rising on the right
    for bx, by, r in ((100, 58, 2), (108, 46, 1), (104, 36, 3), (114, 26, 1), (98, 22, 1)):
        scene.ellipse(bx - r, by - r, bx + r, by + r, lit=False, fill=True)
        scene.ellipse(bx - r, by - r, bx + r, by + r, fill=False)
    _next_button(scene)
    frames.append(scene)

    # 4: credit where it is due
    scene = pale_sea(93)
    text_lines(scene, 8, 14, ["Built on the shoulders of"], "Secondary", 11, lit=True)
    text_lines(scene, 8, 27, ["Unleashed firmware", "by DarkFlippers,", "and the Flipper team"], "Secondary", 11, lit=True)
    _next_button(scene, "OK")
    frames.append(scene)
    return frames
