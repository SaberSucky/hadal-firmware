"""Small 1-bit pixel-art toolkit for generating the Hadal pack.

A Canvas is a PIL '1' image where a lit pixel (one the Flipper darkens) is 0, as in every PNG in
assets/. All the drawing helpers take lit=True to draw and lit=False to erase (draw background).
"""

import math
import random

from PIL import Image, ImageDraw

BAYER4 = [
    [0, 8, 2, 10],
    [12, 4, 14, 6],
    [3, 11, 1, 9],
    [15, 7, 13, 5],
]


class Canvas:
    def __init__(self, width, height, lit=False):
        self.image = Image.new("1", (width, height), 0 if lit else 1)
        self.draw = ImageDraw.Draw(self.image)
        self.width, self.height = width, height

    # ---- pixels ----
    def get(self, x, y):
        if 0 <= x < self.width and 0 <= y < self.height:
            return self.image.getpixel((x, y)) == 0
        return False

    def dot(self, x, y, lit=True):
        if 0 <= x < self.width and 0 <= y < self.height:
            self.image.putpixel((int(x), int(y)), 0 if lit else 1)

    def line(self, x0, y0, x1, y1, lit=True):
        self.draw.line([(x0, y0), (x1, y1)], fill=0 if lit else 1)

    def polyline(self, points, lit=True):
        for a, b in zip(points, points[1:]):
            self.line(a[0], a[1], b[0], b[1], lit)

    def rect(self, x0, y0, x1, y1, lit=True, fill=False):
        """Corners inclusive."""
        if fill:
            self.draw.rectangle([x0, y0, x1, y1], fill=0 if lit else 1)
        else:
            self.draw.rectangle([x0, y0, x1, y1], outline=0 if lit else 1)

    def ellipse(self, x0, y0, x1, y1, lit=True, fill=False, outline=True):
        """Bounding box inclusive."""
        self.draw.ellipse(
            [x0, y0, x1, y1],
            fill=(0 if lit else 1) if fill else None,
            outline=(0 if lit else 1) if outline else None,
        )

    def polygon(self, points, lit=True, fill=True, outline=True):
        self.draw.polygon(
            points,
            fill=(0 if lit else 1) if fill else None,
            outline=(0 if lit else 1) if outline else None,
        )

    def arc(self, box, start, end, lit=True):
        self.draw.arc(box, start, end, fill=0 if lit else 1)

    # ---- patterns ----
    def dither(self, x0, y0, x1, y1, density, lit=True, offset=(0, 0)):
        """Fill a box with an ordered dither; density 0..16 is how many of 16 pixels are drawn."""
        ox, oy = offset
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                if BAYER4[(y + oy) % 4][(x + ox) % 4] < density:
                    self.dot(x, y, lit)

    def sprinkle(self, count, seed, box=None, lit=True):
        rng = random.Random(seed)
        x0, y0, x1, y1 = box or (0, 0, self.width - 1, self.height - 1)
        for _ in range(count):
            self.dot(rng.randint(x0, x1), rng.randint(y0, y1), lit)

    # ---- composition ----
    def paste(self, other, x, y, mode="over", lit_key=True):
        """Place another canvas. 'over' copies only its lit pixels (transparent elsewhere),
        'replace' copies everything, 'erase' clears pixels where it is lit."""
        for sy in range(other.height):
            for sx in range(other.width):
                on = other.get(sx, sy)
                if mode == "replace":
                    self.dot(x + sx, y + sy, on)
                elif mode == "over" and on:
                    self.dot(x + sx, y + sy, True)
                elif mode == "erase" and on:
                    self.dot(x + sx, y + sy, False)

    def crop(self, x0, y0, x1, y1):
        c = Canvas(x1 - x0, y1 - y0)
        c.image = self.image.crop((x0, y0, x1, y1))
        c.draw = ImageDraw.Draw(c.image)
        return c

    def invert(self):
        from PIL import ImageOps

        self.image = ImageOps.invert(self.image.convert("L")).convert("1")
        self.draw = ImageDraw.Draw(self.image)

    def copy(self):
        c = Canvas(self.width, self.height)
        c.image = self.image.copy()
        c.draw = ImageDraw.Draw(c.image)
        return c

    def shifted(self, dx, dy, wrap_y=False):
        """A copy moved by dx, dy; with wrap_y the rows that leave at the bottom come back at the top."""
        c = Canvas(self.width, self.height)
        for y in range(self.height):
            for x in range(self.width):
                if self.get(x, y):
                    ny = (y + dy) % self.height if wrap_y else y + dy
                    c.dot(x + dx, ny)
        return c

    def save(self, path):
        self.image.save(path)


def from_art(rows, lit="#"):
    """A canvas from rows of text: '#' lit, anything else not."""
    width = max(len(r) for r in rows)
    c = Canvas(width, len(rows))
    for y, row in enumerate(rows):
        for x, ch in enumerate(row):
            if ch == lit:
                c.dot(x, y)
    return c


def contact_sheet(canvases, path, columns=4, scale=3, labels=None, gap=6):
    """Render canvases side by side, enlarged, on a grey ground."""
    w = max(c.width for c in canvases)
    h = max(c.height for c in canvases)
    rows = (len(canvases) + columns - 1) // columns
    sheet = Image.new("L", (columns * (w * scale + gap) + gap, rows * (h * scale + gap) + gap), 110)
    for i, c in enumerate(canvases):
        im = c.image.convert("L").resize((c.width * scale, c.height * scale), Image.NEAREST)
        sheet.paste(im, (gap + (i % columns) * (w * scale + gap), gap + (i // columns) * (h * scale + gap)))
    sheet.save(path)
