"""Artwork for the on-device update screen: the 32 x 40 icon beside the progress bar.

The updater draws it with the firmware's own icon table, so it is a plain replacement of
assets/icons/Update/Updating_32x40.png (written by build.py --install), not part of the asset pack.
Ink (what the Flipper darkens) is drawn on a pale ground.
"""

import math

from pixel import Canvas


def _bezier(p0, p1, p2, steps=14):
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


def updating_32x40():
    c = Canvas(32, 40)
    # the lure and its glow: rays around a solid bulb
    stalk = _bezier((13, 16), (13, 3), (26, 6))
    c.polyline(stalk)
    bx, by = 26, 6
    c.ellipse(bx - 2, by - 2, bx + 2, by + 2, fill=True)
    for a in range(0, 360, 45):
        r0, r1 = 4, 5 if a % 90 == 0 else 4
        c.dot(bx + round(math.cos(math.radians(a)) * r1), by + round(math.sin(math.radians(a)) * r1))
    # tail, body, dorsal fin
    c.polygon([(6, 27), (0, 19), (0, 35)], fill=False)
    c.ellipse(4, 16, 27, 38, fill=False)
    c.polygon([(9, 17), (12, 12), (16, 17)], fill=False)
    # eye
    c.ellipse(14, 20, 20, 26, fill=False)
    c.ellipse(16, 22, 18, 24, fill=True)
    # a wide open mouth: two lips and a few fangs that do not touch
    c.polyline([(19, 29), (31, 28)])
    c.polyline([(19, 36), (31, 39)])
    for x, y in ((23, 29), (28, 28)):
        c.line(x, y, x, y + 3)
    for x, y in ((21, 36), (26, 37)):
        c.line(x, y, x, y - 3)
    # gills
    c.line(9, 26, 9, 31)
    c.line(11, 27, 11, 30)
    return c
