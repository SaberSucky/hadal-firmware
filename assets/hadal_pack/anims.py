"""Idle animations of the Hadal pack: generated, frame by frame.

Scenes are 128x64. The desktop's status bar covers the top 11 rows, so nothing important lives
there. The water is lit (dark on the screen) and the creatures and light are unlit (pale), like
the night sleep animation of the stock dolphin.
"""

import math
import random

from pixel import Canvas


# ---------------------------------------------------------------------------
# Marine snow: tiles that repeat vertically, so a loop of frames is seamless
# ---------------------------------------------------------------------------


def snow_layer(tile_height, flakes, seed, width=128, height=64, shift=0):
    """Pale specks drifting down. The same pattern every tile_height rows, so moving it by a
    whole number of tiles over a loop returns it to where it started."""
    rng = random.Random(seed)
    tile = [(rng.randrange(width), rng.randrange(tile_height)) for _ in range(flakes)]
    c = Canvas(width, height, lit=False)  # pale specks are drawn as lit here, then inverted by the caller
    for ty in range(-tile_height, height + tile_height, tile_height):
        for x, y in tile:
            c.dot(x, ty + (y + shift) % tile_height + 0)
    return c


def deep_water(top_glow=True):
    """Black water, a little lighter towards the surface."""
    c = Canvas(128, 64, lit=True)
    if top_glow:
        for y in range(11, 30):
            c.dither(0, y, 127, y, max(0, (30 - y) // 3), lit=False)
    return c


def add_snow(scene, phase, loop_frames):
    """Two layers of drifting snow; phase counts frames within a loop of loop_frames."""
    far = snow_layer(16, 14, 7, shift=phase * (16 // loop_frames))
    near = snow_layer(32, 9, 11, shift=phase * (32 // loop_frames))
    for layer in (far, near):
        for y in range(11, 64):
            for x in range(128):
                if layer.get(x, y):
                    scene.dot(x, y, False)


# ---------------------------------------------------------------------------
# Anglerfish
# ---------------------------------------------------------------------------


def _polygon_mask(points, width=128, height=64):
    m = Canvas(width, height)
    m.polygon(points, lit=True, fill=True, outline=True)
    return m


def _flip(canvas):
    from PIL import ImageDraw, Image

    c = Canvas(canvas.width, canvas.height)
    c.image = canvas.image.transpose(Image.FLIP_LEFT_RIGHT)
    c.draw = ImageDraw.Draw(c.image)
    return c


def anglerfish_frame(phase, loop=8, jaw=0, talking_bubbles=0):
    """The fish drifts and swishes its tail; phase 0..loop-1 drives that, the lure and the snow."""
    t = 2 * math.pi * phase / loop
    scene = deep_water(top_glow=False)
    add_snow(scene, phase, loop)

    # Drawn facing right with `pale` marks and a `body` silhouette that hides the snow behind it,
    # then mirrored so the fish looks left, towards the speech bubble.
    pale = Canvas(128, 64)
    body = Canvas(128, 64)
    cx, cy = 46 + round(math.sin(t) * 1.0), 41 + round(math.sin(t + 1.2) * 1.4)

    # tail: a fan on the left that swings with the phase
    sway = round(math.sin(t - 0.8) * 3)
    tail = [(cx - 19, cy - 2), (cx - 36, cy - 12 + sway), (cx - 32, cy + 1 + sway // 2), (cx - 37, cy + 13 + sway), (cx - 19, cy + 4)]
    body.polygon(tail)
    pale.polygon(tail, fill=False, outline=True)
    for k in range(3):  # fin rays
        pale.line(cx - 21, cy + 1, cx - 34 + k * 2, cy - 8 + k * 9 + sway)

    # body
    body.ellipse(cx - 23, cy - 15, cx + 22, cy + 15, fill=True, outline=True)
    pale.ellipse(cx - 23, cy - 15, cx + 22, cy + 15, fill=False, outline=True)
    pale.dither(cx - 20, cy + 4, cx + 6, cy + 13, 3, offset=(1, 0))  # paler belly
    pale.dither(cx - 20, cy - 13, cx + 2, cy - 5, 1)  # speckled back

    # dorsal and side fins
    dorsal = [(cx - 10, cy - 14), (cx - 5, cy - 21 + (phase % 2)), (cx + 2, cy - 15)]
    body.polygon(dorsal)
    pale.polygon(dorsal, fill=False, outline=True)
    flap = round(math.sin(t * 2) * 2)
    side = [(cx - 6, cy + 3), (cx - 15, cy + 9 + flap), (cx - 3, cy + 9)]
    body.polygon(side)
    pale.polygon(side, fill=False, outline=True)

    # jaws: a gaping mouth with a lower jaw that juts out and opens with `jaw`
    upper_tip = (cx + 26, cy - 4)
    lower_tip = (cx + 28, cy + 7 + jaw)
    hinge_up, hinge_low = (cx + 7, cy - 1), (cx + 7, cy + 4)
    mouth = [hinge_up, upper_tip, lower_tip, hinge_low]
    # open the mouth in the body outline, then draw the lips over the gap
    for px, py in [(x, y) for x in range(cx + 8, cx + 31) for y in range(cy - 8, cy + 14 + jaw)]:
        if _inside(mouth, px, py):
            pale.dot(px, py, False)
    pale.polyline([hinge_up, upper_tip])
    pale.polyline([hinge_low, lower_tip])
    # the lower jaw below its lip, so it reads as a jaw and not just a line
    chin = [lower_tip, (cx + 23, cy + 12 + jaw), (cx + 8, cy + 15)]
    body.polygon(chin + [hinge_low])
    pale.polyline(chin)
    for k in range(3):  # a few fangs, upper ones down and lower ones up, interleaved
        ux = cx + 11 + k * 6
        uy = hinge_up[1] + (upper_tip[1] - hinge_up[1]) * (ux - hinge_up[0]) // (upper_tip[0] - hinge_up[0])
        pale.line(ux, uy, ux, uy + 3)
        lx = cx + 14 + k * 6
        ly = hinge_low[1] + (lower_tip[1] - hinge_low[1]) * (lx - hinge_low[0]) // (lower_tip[0] - hinge_low[0])
        pale.line(lx, ly, lx, ly - 3)

    # eye
    body.ellipse(cx + 9, cy - 10, cx + 15, cy - 4, fill=True, outline=True)
    pale.ellipse(cx + 9, cy - 10, cx + 15, cy - 4, fill=False, outline=True)
    pale.dot(cx + 13, cy - 7)
    pale.dot(cx + 13, cy - 8)

    # the lure: a stalk that arches over the face to a glowing bulb
    base = (cx + 6, cy - 14)
    tip = (cx + 25 + round(math.sin(t * 2)), cy - 25)
    ctrl = (cx + 8, cy - 29)
    points = []
    for s_ in range(0, 11):
        u = s_ / 10
        points.append(
            (
                round((1 - u) ** 2 * base[0] + 2 * (1 - u) * u * ctrl[0] + u * u * tip[0]),
                round((1 - u) ** 2 * base[1] + 2 * (1 - u) * u * ctrl[1] + u * u * tip[1]),
            )
        )
    pale.polyline(points)

    # compose: silhouette hides the snow, then the pale marks
    for y in range(64):
        for x in range(128):
            if body.get(x, y):
                scene.dot(x, y, True)
    scene.paste(_pale_to_unlit(_flip(pale)), 0, 0, mode="erase")

    # the bulb and its halo are drawn after the flip (they are not mirrored artwork, just light)
    gx, gy = 127 - tip[0], tip[1]
    glow = [5, 7, 9, 7][phase % 4]
    for y in range(gy - glow, gy + glow + 1):
        for x in range(gx - glow, gx + glow + 1):
            d = math.hypot(x - gx, y - gy)
            if 3 <= d <= glow and y >= 11 and scene.get(x, y):
                scene_dot_dither(scene, x, y, round(8 * (1 - d / (glow + 0.5))))
    scene.ellipse(gx - 2, gy - 2, gx + 2, gy + 2, lit=False, fill=True, outline=True)

    # bubbles rising from the open mouth while it talks
    for b in range(talking_bubbles):
        bx = 127 - (cx + 31 + b * 3 + (phase % 2))
        by = cy + 2 + jaw - b * 7
        if by > 12:
            scene.ellipse(bx - 1, by - 1, bx + 1, by + 1, lit=False, fill=False, outline=True)
    return scene


def _pale_to_unlit(layer):
    """A layer where lit marks mean 'pale on the scene': 'erase' mode clears exactly those."""
    return layer


def _inside(polygon, x, y):
    inside = False
    n = len(polygon)
    for i in range(n):
        x0, y0 = polygon[i]
        x1, y1 = polygon[(i + 1) % n]
        if (y0 > y) != (y1 > y) and x < (x1 - x0) * (y - y0) / (y1 - y0) + x0:
            inside = not inside
    return inside


def scene_dot_dither(scene, x, y, level):
    from pixel import BAYER4

    if BAYER4[y % 4][x % 4] < level:
        scene.dot(x, y, False)


# ---------------------------------------------------------------------------
# Jellyfish
# ---------------------------------------------------------------------------


def _jelly(scene, cx, top, bell_w, bell_h, phase, loop, tentacles, length, glow, seed):
    """One jellyfish: a dome that pulses, a skirt, and tentacles that ripple down. Returns nothing,
    draws pale pixels on the scene. phase/loop drive the ripple so the loop is seamless."""
    t = 2 * math.pi * phase / loop
    half = bell_w // 2
    # dark fill so snow behind it is hidden
    scene.ellipse(cx - half, top, cx + half, top + bell_h * 2, lit=True, fill=True, outline=False)
    scene.rect(cx - half, top + bell_h, cx + half, top + bell_h + length, lit=True, fill=True)
    # dome: top half of an ellipse, outline plus an inner dither that glows
    scene.arc([cx - half, top, cx + half, top + bell_h * 2], 180, 360, lit=False)
    scene.line(cx - half, top + bell_h, cx + half, top + bell_h, lit=False)
    scene.dither(cx - half + 2, top + 2, cx + half - 2, top + bell_h - 1, glow, lit=False)
    # scalloped skirt along the bottom of the dome
    for k in range(0, bell_w + 1, 4):
        scene.dot(cx - half + k, top + bell_h + 1, False)
    # oral arms and tentacles: sine ripples that travel down with the phase
    rng = random.Random(seed)
    for i in range(tentacles):
        x0 = cx - half + 2 + round(i * (bell_w - 4) / max(1, tentacles - 1))
        amp = 1 + (i % 2)
        for y in range(length):
            off = round(math.sin(t - y * 0.45 + i) * amp * min(1, y / 4))
            if y % (3 if i % 2 else 2) != 2:
                scene.dot(x0 + off, top + bell_h + 2 + y, False)


def jellyfish_frame(phase, loop=8):
    t = 2 * math.pi * phase / loop
    scene = deep_water(top_glow=False)
    add_snow(scene, phase, loop)
    # a small, dim one far behind, then the big one in front
    _jelly(scene, 98, 28 + round(math.sin(t + 2) * 1), 14, 5, phase, loop, 3, 10, 1, 3)
    pulse = round(math.sin(t * 2) * 2)
    _jelly(scene, 50, 15 + round(math.sin(t) * 2), 34 + pulse, 14 - pulse // 2, phase, loop, 7, 30, 3, 5)
    return scene


# ---------------------------------------------------------------------------
# Bathyscaphe
# ---------------------------------------------------------------------------


def bathyscaphe_frame(phase, loop=8):
    t = 2 * math.pi * phase / loop
    scene = deep_water(top_glow=False)
    # a fast fall of snow past the vehicle gives the sense of descending
    far = snow_layer(16, 16, 21, shift=phase * (16 // loop) * 2)
    near = snow_layer(32, 11, 23, shift=phase * (32 // loop))
    for layer in (far, near):
        for y in range(11, 64):
            for x in range(128):
                if layer.get(x, y):
                    scene.dot(x, y, False)

    cx, cy = 44 + round(math.sin(t) * 1), 36 + round(math.sin(t + 1) * 1)

    # headlight beam, a cone of paler water to the lower right that flickers
    reach = 52 + (phase % 2) * 2
    density = 3 if phase % 4 else 4
    for x in range(cx + 12, cx + 12 + reach):
        spread = (x - (cx + 12)) * 5 // 11
        for y in range(cy + 4 - spread // 3, cy + 4 + spread + 1):
            if 11 <= y < 64 and 0 <= x < 128:
                from pixel import BAYER4

                if BAYER4[y % 4][x % 4] < max(0, density - (x - cx - 12) * density // reach) and not scene.get(x, y) is False:
                    scene.dot(x, y, False)

    # float tank above (a long capsule with rungs), tied to the sphere with two struts
    scene.rect(cx - 26, cy - 14, cx + 20, cy - 8, lit=True, fill=True)
    scene.rect(cx - 26, cy - 14, cx + 20, cy - 8, lit=False)
    for x in range(cx - 20, cx + 20, 8):
        scene.line(x, cy - 13, x, cy - 9, lit=False)
    scene.line(cx - 12, cy - 7, cx - 8, cy - 3, lit=False)
    scene.line(cx + 12, cy - 7, cx + 8, cy - 3, lit=False)

    # the crew sphere with a porthole and a rivet ring
    scene.ellipse(cx - 12, cy - 4, cx + 12, cy + 20, lit=True, fill=True, outline=False)
    scene.ellipse(cx - 12, cy - 4, cx + 12, cy + 20, lit=False)
    scene.ellipse(cx - 4, cy + 2, cx + 6, cy + 12, lit=False)
    scene.ellipse(cx - 2, cy + 4, cx + 4, cy + 10, lit=False, fill=True, outline=False)
    scene.dot(cx, cy + 6, True)
    scene.dot(cx + 1, cy + 6, True)
    for a in range(0, 360, 45):
        scene.dot(cx + round(math.cos(math.radians(a)) * 9), cy + 8 + round(math.sin(math.radians(a)) * 9), False)
    # headlight lamp
    scene.rect(cx + 12, cy + 2, cx + 16, cy + 6, lit=False)
    scene.dot(cx + 14, cy + 4, False)
    # thruster at the back with bubbles that drift away
    scene.rect(cx - 18, cy + 5, cx - 12, cy + 9, lit=False)
    for k in range(4):
        by = cy + 11 - ((phase * 3 + k * 3) % 12)  # a rise of 3 a frame: back where it began after a loop
        bx = cx - 21 - k * 3 + (k % 2)
        if 11 < by < 64:
            scene.ellipse(bx - 1, by - 1, bx + 1, by + 1, lit=False, fill=False, outline=True)
    # tether rising out of the picture
    for y in range(11, cy - 14, 2):
        scene.dot(cx - 3 + (1 if (y // 2) % 2 else 0), y, False)
    return scene


# ---------------------------------------------------------------------------
# Hydrothermal vent
# ---------------------------------------------------------------------------


def vent_frame(phase, loop=8):
    from pixel import BAYER4

    t = 2 * math.pi * phase / loop
    scene = Canvas(128, 64, lit=True)
    far = snow_layer(16, 10, 31, shift=phase * (16 // loop))
    for y in range(11, 64):
        for x in range(128):
            if far.get(x, y):
                scene.dot(x, y, False)

    # seafloor: a rough mound that rises to a chimney, with a pale textured foot
    floor = [(0, 60), (24, 58), (46, 56), (60, 50), (66, 44), (78, 44), (84, 50), (100, 57), (127, 60)]
    scene.polygon(floor + [(127, 63), (0, 63)], lit=True, fill=True, outline=False)
    scene.dither(0, 61, 127, 63, 5, lit=False)
    scene.polyline(floor, lit=False)
    # chimney: a black smoker
    chim = [(63, 50), (66, 38), (68, 34), (77, 34), (79, 38), (82, 52)]
    scene.polygon(chim, lit=True, fill=True, outline=False)
    scene.polyline(chim, lit=False)
    for k in range(6):
        scene.dot(66 + k * 2 + (k % 2), 40 + (k * 5) % 9, False)
    scene.line(69, 34, 76, 34, True)  # the dark mouth
    # plume: puffs that rise, spread and thin out; each loop of frames moves them one slot on
    for k in range(4):
        rise = (phase * 3 + k * 6) % 24
        y = 32 - rise
        if y < 12:
            continue
        spread = rise // 5
        x = 73 + round(math.sin(k * 1.9 + rise * 0.25) * (1 + spread))
        r = 1 + spread // 2
        scene.ellipse(x - r - 1, y - r, x + r + 1, y + r, lit=False, fill=(rise < 12), outline=True)
    # glow at the mouth
    for y in range(24, 36):
        for x in range(60, 86):
            d = math.hypot(x - 73, y - 35)
            if d < 10 and scene.get(x, y) and BAYER4[y % 4][x % 4] < round(5 * (1 - d / 10)):
                scene.dot(x, y, False)
    # tube worms: pale stalks with a little crown, swaying out of step
    for i, bx in enumerate((14, 20, 27, 98, 105, 112)):
        base_y = 59 if bx < 60 else 58
        height = 7 + (i * 5) % 6
        sway = round(math.sin(t + i) * 1.4)
        for y in range(height):
            scene.dot(bx + round(sway * y / height), base_y - y, False)
        top_x = bx + sway
        scene.dot(top_x - 1, base_y - height, False)
        scene.dot(top_x + 1, base_y - height, False)
        scene.dot(top_x, base_y - height - 1, False)
    return scene
