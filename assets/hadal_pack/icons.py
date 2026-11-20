"""The icons the Hadal pack replaces.

Only the loading spinner: the main menu's module icons stay as they are with every pack (the
firmware does not let packs replace them), and the stock status bar icons are kept, they are
clear at their tiny sizes. The spinner keeps the built-in one's size and frame count.
"""

from pixel import Canvas

# ---------------------------------------------------------------- loading: a sonar ping


def loading_24(f, n):
    c = Canvas(24, 24)
    c.rect(11, 11, 12, 12, fill=True)
    # two rings spread out from the centre, the outer one fading
    for k in range(2):
        phase = (f + k * n / 2) % n
        r = 3 + phase * 8 / n
        box = [11.5 - r, 11.5 - r, 12.5 + r, 12.5 + r]
        if phase < n * 0.7:
            c.ellipse(*box)
        else:
            # broken ring: the ping dying out
            for a in range(0, 360, 30):
                c.arc(box, a, a + 12)
    return c


STATUS_ICONS = {}
MENU_ICONS = {}
ANIMATED_ICONS = {
    "Common/Loading_24": loading_24,
}
