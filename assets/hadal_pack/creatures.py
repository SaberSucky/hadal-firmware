"""The other creatures of the Hadal pack, in the anglerfish's style: dark shapes in pale water,
with pale details. Each *_frame(phase, loop) returns a 128x64 scene."""

import math

from pixel import Canvas
from anims import dark_snow


def _wave(x0, y0, length, amplitude, wavelength, phase, step=1):
    """Points down a wavy line from (x0, y0)."""
    pts = []
    for d in range(0, length + 1, step):
        sway = amplitude * (d / max(1, length)) ** 0.7
        pts.append((round(x0 + math.sin(phase + d / wavelength) * sway), y0 + d))
    return pts


def jelly(scene, cx, top, width, height, phase, tentacles, length, solid=True):
    """A jellyfish: bell centred on cx with its top at `top`, phase in radians (bell pulse and
    tentacle wave)."""
    x0, x1 = cx - width // 2, cx + width // 2
    rim = top + height
    # the bell: a dome, flat-bottomed
    bell = Canvas(128, 64)
    bell.ellipse(x0, top, x1, rim + height // 2, fill=True)
    for y in range(rim + 1, 64):
        for x in range(128):
            bell.dot(x, y, False)
    # clear the water behind it, then draw it
    scene.paste(bell, 0, 0, mode="erase")
    if solid:
        scene.paste(bell, 0, 0, mode="over")
        # pale radial canals from the top, a gloss on the left, a scalloped rim
        for k in (-2, -1, 1, 2):
            scene.line(cx + k * width // 7, top + 4, cx + k * width // 4, rim - 2, lit=False)
        scene.arc((x0 + 3, top + 2, x0 + width // 2, rim + 2), 190, 250, lit=False)
        for x in range(x0 + 2, x1 - 1, 3):
            scene.dot(x, rim, lit=False)
    else:
        scene.paste(bell, 0, 0, mode="erase")
        scene.arc((x0, top, x1, rim + height // 2), 180, 360)
        scene.line(x0, rim, x1, rim)
        for k in (-1, 1):
            scene.line(cx + k * width // 6, top + 3, cx + k * width // 4, rim - 1)

    # oral arms: frilly ribbons under the middle
    if solid:
        for k, offset in ((-1, 0.0), (0, 1.3), (1, 2.6)):
            pts = _wave(cx + k * 4, rim + 1, length // 2, 3, 4.5, phase + offset)
            for i, (x, y) in enumerate(pts):
                frill = 1 + (i % 4 == 0)
                scene.line(x - frill, y, x + 1, y)
    # tentacles: thin and long, from the rim
    for i in range(tentacles):
        u = i / max(1, tentacles - 1)
        x = round(x0 + 2 + u * (width - 4))
        pts = _wave(x, rim + 1, length, 4, 6.0, phase + i * 0.9, step=2)
        scene.polyline(pts)


def jellyfish_frame(phase, loop=8):
    t = 2 * math.pi * phase / loop
    scene = Canvas(128, 64)
    dark_snow(scene, phase, loop)
    # the small one far behind, out of step
    jelly(scene, 104, 22 + round(math.sin(t + 2) * 2), 14, 6, t + 2, 4, 22, solid=False)
    # the big one: the bell squeezes (narrower, taller) and the jelly rises a little
    squeeze = (math.sin(t) + 1) / 2
    width = round(36 - 6 * squeeze)
    height = round(14 + 3 * squeeze)
    jelly(scene, 52, 16 - round(squeeze * 2), width, height, t, 7, 44, solid=True)
    return scene


def _seafloor(scene, phase=0, top=57, seed=3):
    """A dark seabed along the bottom, its edge uneven, with a few pale pebbles."""
    import random

    rng = random.Random(seed)
    heights = []
    h = top
    for x in range(128):
        if x % 6 == 0:
            h = max(top - 2, min(top + 3, h + rng.choice((-1, 0, 0, 1))))
        heights.append(h)
    for x, y0 in enumerate(heights):
        scene.line(x, y0, x, 63)
    for _ in range(10):
        x = rng.randrange(2, 126)
        scene.dot(x, heights[x] + 2 + rng.randrange(3), lit=False)
    return heights


def bathyscaphe_frame(phase, loop=8):
    """A bathyscaphe on its way down: float, sphere, searchlight, propeller, ballast bubbles."""
    t = 2 * math.pi * phase / loop
    scene = Canvas(128, 64)
    dark_snow(scene, phase, loop)
    _seafloor(scene, top=60)
    oy = round(math.sin(t) * 1.5)
    sub = Canvas(128, 64)
    # the float: a long hull with a conning tower and a rail on top
    sub.ellipse(30, 20 + oy, 100, 33 + oy, fill=True)
    sub.rect(58, 15 + oy, 70, 21 + oy, fill=True)
    sub.line(56, 14 + oy, 72, 14 + oy)
    for x in (56, 64, 72):
        sub.line(x, 14 + oy, x, 16 + oy)
    # the crew's sphere, slung under the float
    sub.rect(60, 32 + oy, 68, 35 + oy, fill=True)
    sub.ellipse(55, 34 + oy, 73, 50 + oy, fill=True)
    # the propeller at the back, turning
    blade = [(2, 0), (0, 2), (-2, 0), (0, -2)][phase % 4]
    sub.line(100, 26 + oy, 104, 26 + oy)
    sub.line(104 - blade[0], 26 + oy - 3 - blade[1], 104 + blade[0], 26 + oy + 3 + blade[1])
    scene.paste(sub, 0, 0, mode="erase")
    scene.paste(sub, 0, 0, mode="over")
    # pale details: the hull's seams and rivets, the porthole, a stripe
    scene.line(36, 27 + oy, 94, 27 + oy, lit=False)
    for x in range(40, 92, 6):
        scene.dot(x, 23 + oy, lit=False)
    scene.ellipse(58, 38 + oy, 64, 44 + oy, lit=False, fill=True)
    scene.ellipse(59, 39 + oy, 63, 43 + oy, fill=False)
    scene.dot(60, 40 + oy, lit=False)
    # the searchlight under the sphere, and its beam reaching down and forward
    scene.rect(52, 45 + oy, 55, 47 + oy, fill=True)
    for k in range(0, 30, 2):
        scene.dot(51 - k, 47 + oy + k // 3)
        scene.dot(51 - k, 47 + oy + k)
    # ballast bubbles rising off the float
    for b in range(3):
        by = 18 + oy - ((phase * 12 // loop + b * 4) % 12)
        bx = 86 + b * 3 + (b + phase) % 2
        if by > 11:
            scene.ellipse(bx - 1, by - 1, bx + 1, by + 1, fill=False)
    return scene


def vent_frame(phase, loop=8):
    """A black smoker: a chimney pouring dark smoke, tube worms and a crab around it."""
    t = 2 * math.pi * phase / loop
    scene = Canvas(128, 64)
    dark_snow(scene, phase, loop)
    heights = _seafloor(scene, top=56)
    # the plume: dark puffs that rise, spread and thin out, back where they were after a loop
    from pixel import BAYER4

    for k in range(8):
        rise = (k * 6 + phase * 48 // loop) % 48
        r = 3 + rise // 4
        cx = 70 + round(math.sin(rise / 8 + k) * 2) + rise // 3
        cy = 29 - rise // 2
        density = max(1, 12 - rise // 4)
        for y in range(cy - r, cy + r + 1):
            for x in range(cx - r - 2, cx + r + 3):
                inside = ((x - cx) / 1.3) ** 2 + (y - cy) ** 2 <= r * r
                if inside and y > 11 and BAYER4[y % 4][x % 4] < density:
                    scene.dot(x, y)
    # the chimney: a lumpy dark column, wider at the bottom, with pale cracks
    chimney = [(60, 57), (62, 46), (61, 40), (64, 33), (66, 30), (74, 30), (75, 34), (78, 40),
               (77, 47), (81, 57)]
    scene.polygon(chimney)
    scene.line(66, 30, 74, 30)
    scene.polyline([(68, 36), (67, 41), (69, 45), (68, 50)], lit=False)
    scene.polyline([(74, 40), (75, 46), (73, 52)], lit=False)
    # hot water shimmering at the mouth
    for x in range(66, 75, 2):
        scene.dot(x + phase % 2, 28)
    # tube worms: pale tubes with dark rims, red (dark) plumes that sway
    worms = ((42, 10), (45, 14), (48, 11), (51, 8), (89, 9), (92, 13), (95, 10), (98, 7))
    for i, (x, h) in enumerate(worms):
        base = heights[x]
        sway = round(math.sin(t + i * 1.7))
        scene.line(x - 1, base - h, x - 1, base)
        scene.line(x + 1, base - h, x + 1, base)
        scene.line(x, base - h, x, base, lit=False)
        top = base - h - 1
        scene.line(x - 1 + sway, top - 2, x + 1 + sway, top - 2)
        scene.line(x - 2 + sway, top - 1, x + 2 + sway, top - 1)
        scene.line(x - 1, top, x + 1, top)
    # a crab walking along the bottom, back and forth over the loop
    cx = 20 + round(math.sin(t) * 6)
    cy = heights[cx] - 3
    scene.ellipse(cx - 4, cy - 2, cx + 4, cy + 2, fill=True)
    for side in (-1, 1):
        for leg in range(3):
            lx = cx + side * (2 + leg * 2)
            step = (phase + leg) % 2
            scene.line(lx, cy + 1, lx + side * 2, cy + 3 + step)
        scene.line(cx + side * 3, cy - 2, cx + side * 5, cy - 5)
        scene.dot(cx + side * 6, cy - 5)
        scene.dot(cx + side * 5, cy - 6)
    scene.dot(cx - 1, cy - 3, lit=True)
    scene.dot(cx + 1, cy - 3, lit=True)
    return scene
