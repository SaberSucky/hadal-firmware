"""Idle animations of the Hadal pack: generated, frame by frame.

Scenes are 128x64. The desktop's status bar covers the top 11 rows, so nothing important lives
there. The anglerfish is a dark silhouette in pale water (large dark areas look muddy and smear on
the Flipper's screen when they move).
"""

import math
import random

from pixel import Canvas


# ---------------------------------------------------------------------------
# Marine snow: tiles that repeat vertically, so a loop of frames is seamless
# ---------------------------------------------------------------------------


def snow_layer(tile_height, flakes, seed, width=128, height=64, shift=0):
    """Specks drifting down. The same pattern every tile_height rows, so moving it by a
    whole number of tiles over a loop returns it to where it started."""
    rng = random.Random(seed)
    tile = [(rng.randrange(width), rng.randrange(tile_height)) for _ in range(flakes)]
    c = Canvas(width, height, lit=False)
    for ty in range(-tile_height, height + tile_height, tile_height):
        for x, y in tile:
            c.dot(x, ty + (y + shift) % tile_height + 0)
    return c


# ---------------------------------------------------------------------------
# Anglerfish (the other creatures are in creatures.py)
# ---------------------------------------------------------------------------


def dark_snow(scene, phase, loop_frames, top=12):
    """Marine snow as dark specks drifting down through pale water: two layers, the near one
    faster, both back where they started after loop_frames."""
    far = snow_layer(16, 12, 7, shift=phase * (16 // loop_frames))
    near = snow_layer(32, 7, 11, shift=phase * (32 // loop_frames))
    for layer in (far, near):
        for y in range(top, 64):
            for x in range(128):
                if layer.get(x, y):
                    scene.dot(x, y, True)


def anglerfish_frame(phase, loop=8, jaw=0, talking_bubbles=0):
    """The anglerfish (angler.py) hangs in pale water, its lure pulsing; phase 0..loop-1 drives
    its drift, tail, lure and the snow. jaw 0..5 opens the mouth (talking)."""
    import angler

    t = 2 * math.pi * phase / loop
    scene = Canvas(128, 64)
    dark_snow(scene, phase, loop)
    glow = [0, 1, 2, 3, 3, 2, 1, 0][(phase % loop) * 8 // loop]
    ink, mask = angler.anglerfish(
        jaw=min(4, jaw),
        glow=glow,
        tail=round(math.sin(2 * t) * 2),
        lure_sway=round(math.sin(t) * 2),
        blink=(jaw == 0 and phase == loop - 1),
    )
    x, y = 46, 12 + round(math.sin(t) * 1.4)
    scene.paste(mask, x, y, mode="erase")
    scene.paste(ink, x, y, mode="over")

    # bubbles rising from the open mouth while it talks
    for b in range(talking_bubbles):
        bx = x + 4 - b * 2 + (phase % 2)
        by = y + 26 - b * 7
        if by > 13:
            scene.ellipse(bx - 1, by - 1, bx + 1, by + 1, fill=False)
    return scene
