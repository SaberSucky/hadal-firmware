"""The anglerfish, Hadal's mascot: a solid black silhouette lit by its own lure.

anglerfish() returns two canvases the size of the fish: ink (what the Flipper darkens) and mask
(everything the fish covers, its mouth and eye included), so that a scene can clear the water
behind the fish before drawing it. The fish faces left; its lure hangs in front of its mouth.
"""

import math

from pixel import BAYER4, Canvas

WIDTH, HEIGHT = 80, 52


def _bezier(p0, p1, p2, steps=24):
    pts = []
    for s in range(steps + 1):
        u = s / steps
        pts.append(
            (
                round((1 - u) ** 2 * p0[0] + 2 * (1 - u) * u * p1[0] + u * u * p2[0]),
                round((1 - u) ** 2 * p0[1] + 2 * (1 - u) * u * p1[1] + u * u * p2[1]),
            )
        )
    return pts


def _edge(canvas, x, y, distance):
    """Whether a lit pixel is within distance of the unlit outside."""
    for dy in range(-distance, distance + 1):
        for dx in range(-distance, distance + 1):
            if abs(dx) + abs(dy) <= distance and not canvas.get(x + dx, y + dy):
                return True
    return False


def anglerfish(jaw=0, glow=0, tail=0, blink=False, lure_sway=0, eye="open"):
    """jaw: 0 (closed a little) to 4 (wide open); glow: 0..3, the lure's shine; tail: -2..2;
    lure_sway: -2..2, how far the lure swings; eye: open, squint, look (up at the lure) or shades"""
    if blink:
        eye = "squint"
    body = Canvas(WIDTH, HEIGHT)
    oy = 8  # room above for the lure

    # ---- silhouette ----
    # the round body, the tail stalk, and the big lower jaw
    body.ellipse(16, oy + 6, 62, oy + 42, fill=True)
    body.polygon([(54, oy + 14), (68, oy + 21), (68, oy + 29), (54, oy + 38)])
    # tail fin: a fan that sways
    t = tail
    body.polygon([(66, oy + 25), (79, oy + 12 + t), (76, oy + 25 + t // 2), (79, oy + 38 + t)])
    # the chin juts forward and drops as the jaw opens
    j = jaw
    body.polygon([(34, oy + 24), (5, oy + 30 + j), (6, oy + 35 + j), (14, oy + 41 + j // 2), (34, oy + 42)])
    # forehead slopes down to the snout
    body.polygon([(22, oy + 9), (9, oy + 18), (34, oy + 24)])
    # dorsal spines
    for x, h in ((41, 4), (47, 5), (53, 3)):
        body.polygon([(x - 2, oy + 8), (x, oy + 8 - h), (x + 1, oy + 8)])

    mask = body.copy()
    ink = body.copy()

    # ---- the mouth: a wide gap with a few long fangs ----
    corner = (35, oy + 24)
    upper_tip = (9, oy + 18)
    lower_tip = (5, oy + 30 + j)
    ink.polygon([corner, upper_tip, (6, oy + 22 + j // 2), lower_tip], lit=False)
    # fangs, longest at the front: upper ones hang down, lower ones stand up
    for u, length in ((0.10, 6), (0.30, 5), (0.52, 4), (0.74, 2)):
        x = round(upper_tip[0] + (corner[0] - upper_tip[0]) * u)
        y = round(upper_tip[1] + (corner[1] - upper_tip[1]) * u)
        ink.polygon([(x - 1, y), (x + 1, y), (x + 1, y + length)])
    for u, length in ((0.06, 7 + j // 2), (0.26, 6 + j // 2), (0.48, 4 + j // 3), (0.70, 2)):
        x = round(lower_tip[0] + (corner[0] - lower_tip[0]) * u)
        y = round(lower_tip[1] + (corner[1] - lower_tip[1]) * u)
        ink.polygon([(x - 1, y), (x + 1, y), (x, y - length)])

    # ---- the eye: small, pale, with a dark pupil ----
    ex, ey = 27, oy + 14
    if eye == "squint":
        ink.line(ex - 2, ey, ex + 2, ey, lit=False)
        ink.dot(ex - 3, ey - 1, lit=False)
    elif eye == "shades":
        # dark glasses: pale rims around dark lenses, and a bar across
        ink.rect(ex - 6, ey - 3, ex + 4, ey - 3, lit=False)
        ink.rect(ex - 5, ey - 2, ex - 1, ey + 2, lit=False)
        ink.rect(ex + 1, ey - 2, ex + 4, ey + 2, lit=False)
        ink.dot(ex - 3, ey - 1, lit=False)
        ink.dot(ex + 2, ey - 1, lit=False)
    else:
        ink.ellipse(ex - 2, ey - 2, ex + 2, ey + 2, lit=False, fill=True)
        px, py = (ex - 1, ey - 1) if eye == "look" else (ex - 1, ey)
        ink.dot(px, py)
        ink.dot(px + 1, py)
        ink.dot(px, py + 1)

    # ---- pale strokes on the dark body: the jaw's hinge, the gill, a fin, the side's pores ----
    ink.polyline([(35, oy + 25), (36, oy + 31), (33, oy + 37), (27, oy + 41)], lit=False)
    ink.arc((36, oy + 15, 44, oy + 31), 300, 60, lit=False)
    # the pectoral fin: a small pale-edged fan behind the gill
    ink.polygon([(48, oy + 25), (55, oy + 20), (58, oy + 25), (55, oy + 31)], lit=False, fill=False)
    ink.line(48, oy + 25, 56, oy + 25, lit=False)
    for x in range(60, 68, 3):
        ink.dot(x, oy + 21 + round(math.sin(x / 3)), lit=False)
    # the back catches the lure's light: a thin pale line along the top of the head
    for x in range(18, 37):
        for y in range(oy + 6, oy + 14):
            if body.get(x, y) and not body.get(x, y - 1):
                ink.dot(x, y + 1, lit=False)
                break

    lure_root = (30, oy + 7)
    esca = (8 + lure_sway, 10)

    # ---- the lure: a stalk from the forehead to a glowing bulb ----
    stalk = _bezier(lure_root, (24 + lure_sway // 2, -4), esca)
    ink.polyline(stalk)
    mask.polyline(stalk)
    bx, by = esca
    ink.ellipse(bx - 3, by - 3, bx + 3, by + 3, fill=True)
    mask.ellipse(bx - 5, by - 5, bx + 5, by + 5, fill=True)
    ink.ellipse(bx - 2, by - 2, bx + 2, by + 2, lit=False, fill=True)
    ink.dot(bx, by)
    # rays: longer on the beat
    for k in range(8):
        a = math.radians(k * 45 + 22.5 * (glow % 2))
        r0 = 5
        r1 = 5 + (1 if k % 2 else 2) + (glow // 2)
        x0, y0 = bx + math.cos(a) * r0, by + math.sin(a) * r0
        x1, y1 = bx + math.cos(a) * r1, by + math.sin(a) * r1
        ink.line(round(x0), round(y0), round(x1), round(y1))
    return ink, mask


def preview(path, scale=4):
    from PIL import Image

    frames = [
        anglerfish(jaw=0, glow=0),
        anglerfish(jaw=2, glow=1, tail=1, lure_sway=1),
        anglerfish(jaw=4, glow=2, tail=2, blink=True, lure_sway=2),
    ]
    sheet = Image.new("RGB", ((WIDTH * scale + 8) * len(frames) + 8, HEIGHT * scale + 16), (40, 40, 40))
    for i, (ink, _) in enumerate(frames):
        img = Image.new("RGB", (WIDTH, HEIGHT), (255, 130, 0))
        for y in range(HEIGHT):
            for x in range(WIDTH):
                if ink.get(x, y):
                    img.putpixel((x, y), (20, 12, 0))
        sheet.paste(img.resize((WIDTH * scale, HEIGHT * scale), Image.NEAREST), (8 + i * (WIDTH * scale + 8), 8))
    sheet.save(path)


if __name__ == "__main__":
    import sys

    preview(sys.argv[1])
