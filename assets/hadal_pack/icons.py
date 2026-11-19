"""Status bar and main menu icons of the Hadal pack.

Every icon keeps the exact size, and the main menu icons the exact frame count, of the one they
replace: the firmware lays everything out with the built-in sizes. Ink (a pixel the Flipper
darkens) is drawn as lit.
"""

import math

from pixel import Canvas, from_art


def art(rows):
    return from_art(rows)


# ---------------------------------------------------------------------------
# Status bar
# ---------------------------------------------------------------------------


def battery_26x8():
    # The firmware draws the charge inside, from (1,1) to (22,6): the frame must leave that free.
    c = Canvas(26, 8)
    c.rect(0, 0, 23, 7)
    for x, y in ((0, 0), (23, 0), (0, 7), (23, 7)):
        c.dot(x, y, False)  # rounded corners
    c.rect(24, 2, 25, 5, fill=True)
    c.dot(25, 2, False)
    c.dot(25, 5, False)
    return c


def bluetooth_idle_5x8():
    return art([
        "..#..",
        "..##.",
        "#.#.#",
        ".###.",
        ".###.",
        "#.#.#",
        "..##.",
        "..#..",
    ])


def bluetooth_connected_16x8():
    c = Canvas(16, 8)
    c.paste(bluetooth_idle_5x8(), 0, 0)
    for x in (7, 9, 11):
        c.dot(x, 3)
        c.dot(x, 4)
    c.rect(13, 1, 15, 6, fill=False)
    c.dot(14, 3)
    return c


def ble_beacon_7x8():
    return art([
        ".#.#.#.",
        "#..#..#",
        "#.###.#",
        "#..#..#",
        ".#.#.#.",
        "...#...",
        "...#...",
        "..###..",
    ])


def lightning_9x10():
    return art([
        "......##.",
        ".....##..",
        "....##...",
        "...#####.",
        "..#####..",
        ".....##..",
        "....##...",
        "...##....",
        "..##.....",
        "..#......",
    ])


def lightning_mask_9x10():
    """The bolt grown by one pixel in every direction, drawn behind it to keep it legible."""
    bolt = lightning_9x10()
    c = Canvas(9, 10)
    for y in range(10):
        for x in range(9):
            if any(bolt.get(x + dx, y + dy) for dx in (-1, 0, 1) for dy in (-1, 0, 1)):
                c.dot(x, y)
    return c


def exp_module_12x8():
    return art([
        ".#.#.#.#.#..",
        ".#########..",
        "##.......##.",
        "#..##.##..#.",
        "#..##.##..#.",
        "##.......##.",
        ".#########..",
        ".#.#.#.#.#..",
    ])


def game_mode_11x8():
    return art([
        "..#######..",
        ".#########.",
        "##.#####.##",
        "#.###.#.#.#",
        "##.#####.##",
        "#.#.....#.#",
        "#.#.....#.#",
        ".##.....##.",
    ])


def hidden_window_9x8():
    # an eye, crossed out
    return art([
        "........#",
        "..###..#.",
        ".#...#.#.",
        "#..#..#..",
        ".#...#.#.",
        "..###.#..",
        "......#..",
        ".....#...",
    ])


def muted_8x8():
    return art([
        "...#..#.",
        "..##...#",
        "####.#.#",
        "####..#.",
        "####.#.#",
        "..##...#",
        "...#..#.",
        "........",
    ])


def rpc_active_7x8():
    return art([
        "..###..",
        "..#.#..",
        "..###..",
        ".#####.",
        "#######",
        "#.###.#",
        "#.....#",
        ".#####.",
    ])


def sd_mounted_11x8():
    return art([
        ".#########.",
        "#.#.#.#.#.#",
        "#.#.#.#.#.#",
        "#.........#",
        "#.........#",
        "#.#######.#",
        "#.........#",
        ".#########.",
    ])


def sd_fail_11x8():
    c = sd_mounted_11x8()
    # cross out the card
    for i in range(6):
        c.dot(3 + i, 1 + i)
        c.dot(7 - i, 1 + i)
    return c


def lock_7x8():
    return art([
        "..###..",
        ".#...#.",
        ".#...#.",
        "#######",
        "###.###",
        "###.###",
        "#######",
        "#######",
    ])


STATUS_ICONS = {
    "StatusBar/Battery_26x8": battery_26x8,
    "StatusBar/Bluetooth_Idle_5x8": bluetooth_idle_5x8,
    "StatusBar/Bluetooth_Connected_16x8": bluetooth_connected_16x8,
    "StatusBar/BLE_beacon_7x8": ble_beacon_7x8,
    "StatusBar/Charging-lightning_9x10": lightning_9x10,
    "StatusBar/Charging-lightning_mask_9x10": lightning_mask_9x10,
    "StatusBar/Exp_module_connected_12x8": exp_module_12x8,
    "StatusBar/GameMode_11x8": game_mode_11x8,
    "StatusBar/Hidden_window_9x8": hidden_window_9x8,
    "StatusBar/Muted_8x8": muted_8x8,
    "StatusBar/Rpc_active_7x8": rpc_active_7x8,
    "StatusBar/SDcardMounted_11x8": sd_mounted_11x8,
    "StatusBar/SDcardFail_11x8": sd_fail_11x8,
    "SubGhz/Lock_7x8": lock_7x8,
}


# ---------------------------------------------------------------------------
# Main menu: 14x14, animated while selected
# ---------------------------------------------------------------------------


def _waves(c, cx, cy, radii, start, end, shown):
    """Arcs around (cx, cy); shown says how many of the radii to draw (outwards, like a pulse)."""
    for i, r in enumerate(radii):
        if i < shown:
            c.arc([cx - r, cy - r, cx + r, cy + r], start, end)


def _bubbles(c, frame, count, x0=12):
    """A couple of bubbles rising along the right edge; moves with the frame."""
    for k in range(count):
        y = 13 - (frame * 3 + k * 6) % 14
        c.dot(x0 + (k % 2), y)


def _pulse(frame, n, steps=4):
    return frame % steps


def menu_125khz(f, n):
    c = Canvas(14, 14)
    c.ellipse(1, 8, 5, 12, fill=False)
    c.ellipse(2, 9, 4, 11, fill=True)
    _waves(c, 3, 10, (5, 8, 11), 270, 360, 1 + f % 3)
    return c


def menu_nfc(f, n):
    c = Canvas(14, 14)
    c.rect(1, 3, 5, 12)
    c.line(2, 10, 4, 10)
    _waves(c, 5, 7, (3, 5, 7), -50, 50, 1 + f % 3)
    return c


def menu_sub1ghz(f, n):
    c = Canvas(14, 14)
    # a lattice mast with a lit tip, and waves leaving it to both sides
    c.line(7, 5, 3, 13)
    c.line(7, 5, 11, 13)
    c.line(5, 9, 9, 9)
    c.line(4, 11, 10, 11)
    c.line(6, 7, 8, 7)
    c.ellipse(6, 3, 8, 5, fill=True)
    shown = 1 + f % 3
    for i, r in enumerate((3, 5, 7)):
        if i < shown:
            c.arc([7 - r, 4 - r, 7 + r, 4 + r], 160, 200)
            c.arc([7 - r, 4 - r, 7 + r, 4 + r], -20, 20)
    return c


def menu_infrared(f, n):
    c = Canvas(14, 14)
    c.rect(4, 6, 9, 13)
    c.dot(6, 8)
    c.dot(7, 8)
    c.dot(6, 10)
    c.dot(7, 10)
    c.dot(6, 12)
    c.dot(7, 12)
    c.dot(6, 5)
    c.dot(7, 5)
    for i, r in enumerate((2, 4, 6)):
        if i < 1 + f % 3:
            c.arc([7 - r - 0, 5 - r - 0, 7 + r + 1, 5 + r + 1], 215, 325)
    return c


def menu_ibutton(f, n):
    c = Canvas(14, 14)
    c.ellipse(1, 3, 12, 14 - 1)
    c.ellipse(4, 6, 9, 11)
    c.dot(6, 8)
    c.dot(7, 8)
    c.dot(6, 9)
    c.dot(7, 9)
    c.line(1, 6, 3, 6)
    _bubbles(c, f, 1, 11)
    return c


def menu_gpio(f, n):
    c = Canvas(14, 14)
    c.rect(3, 3, 10, 10)
    c.rect(5, 5, 8, 8, fill=(f % 2 == 0))
    for i in (4, 6, 8):
        c.line(i, 1, i, 2)
        c.line(i, 11, i, 12)
        c.line(1, i, 2, i)
        c.line(11, i, 12, i)
    # a signal travels along the pins
    c.dot(4 + (f * 2) % 7, 0)
    return c


def menu_badusb(f, n):
    c = Canvas(14, 14)
    c.rect(4, 1, 9, 6)
    c.dot(5, 3)
    c.dot(8, 3)
    c.rect(3, 7, 10, 12, fill=True)
    # the lit pixel sweeps over the body like data going through
    c.dot(4 + f % 6, 10, False)
    c.dot(4 + (f + 3) % 6, 8, False)
    return c


def menu_u2f(f, n):
    c = Canvas(14, 14)
    c.ellipse(1, 3, 6, 8)
    c.dot(3, 5, True)
    c.line(6, 6, 13, 6)
    c.line(10, 6, 10, 9)
    c.line(13, 6, 13, 8)
    if f % 2:
        c.dot(3, 5, False)
        c.dot(4, 6, False)
    _bubbles(c, f, 1, 1)
    return c


def menu_clock(f, n):
    c = Canvas(14, 14)
    c.ellipse(1, 1, 12, 12)
    for a in range(0, 360, 90):
        c.dot(6 + round(math.cos(math.radians(a)) * 4.5), 6 + round(math.sin(math.radians(a)) * 4.5))
    angle = -90 + 360 * f / max(1, n)
    c.line(6, 6, 6 + round(math.cos(math.radians(angle)) * 4), 6 + round(math.sin(math.radians(angle)) * 4))
    c.line(6, 6, 6, 3)
    return c


def menu_files(f, n):
    c = Canvas(14, 14)
    c.polyline([(1, 12), (1, 2), (5, 2), (6, 4), (12, 4), (12, 12), (1, 12)])
    c.line(1, 5, 12, 5)
    c.dot(2 + f % 9, 8)
    c.dot(3 + (f * 2) % 8, 10)
    return c


def menu_plugins(f, n):
    c = Canvas(14, 14)
    c.rect(2, 5, 9, 12)
    c.line(4, 2, 4, 4)
    c.line(7, 2, 7, 4)
    c.line(5, 13, 5, 13)
    c.line(10, 8, 12, 8)
    c.line(10, 10, 12, 10)
    c.dot(4 + f % 4, 8)
    c.dot(5, 11 - f % 2)
    return c


def menu_settings(f, n):
    c = Canvas(14, 14)
    c.ellipse(4, 4, 9, 9)
    c.ellipse(6, 6, 7, 7, fill=True)
    turn = f * 45 / max(1, n) * 1.0
    for k in range(8):
        a = math.radians(k * 45 + turn)
        x0, y0 = 6.5 + math.cos(a) * 4, 6.5 + math.sin(a) * 4
        x1, y1 = 6.5 + math.cos(a) * 6, 6.5 + math.sin(a) * 6
        c.line(round(x0), round(y0), round(x1), round(y1))
    return c


def menu_subghz_remote(f, n):
    c = Canvas(14, 14)
    c.rect(3, 6, 7, 13)
    c.dot(5, 8)
    c.dot(5, 10)
    c.dot(5, 12)
    c.dot(4, 5)
    c.dot(6, 5)
    for i, r in enumerate((3, 5, 7)):
        if i < 1 + f % 3:
            c.arc([5 - r, 5 - r, 5 + r, 5 + r], 280, 360)
    return c


MENU_ICONS = {
    "MainMenu/125khz_14": menu_125khz,
    "MainMenu/NFC_14": menu_nfc,
    "MainMenu/Sub1ghz_14": menu_sub1ghz,
    "MainMenu/Infrared_14": menu_infrared,
    "MainMenu/iButton_14": menu_ibutton,
    "MainMenu/GPIO_14": menu_gpio,
    "MainMenu/BadUsb_14": menu_badusb,
    "MainMenu/U2F_14": menu_u2f,
    "MainMenu/Clock_14": menu_clock,
    "MainMenu/FileManager_14": menu_files,
    "MainMenu/Plugins_14": menu_plugins,
    "MainMenu/Settings_14": menu_settings,
    "MainMenu/SubGHzRemote_14": menu_subghz_remote,
}


# ---------------------------------------------------------------------------
# The loading spinner: a bubble ring that opens and closes
# ---------------------------------------------------------------------------


def loading_24(f, n):
    c = Canvas(24, 24)
    cx = cy = 11.5
    for k in range(8):
        a = math.radians(k * 45 - 90)
        phase = (k - f) % 8  # how long ago the lit one passed
        r = 2 if phase == 0 else 1.5 if phase < 3 else 1
        x, y = cx + math.cos(a) * 9, cy + math.sin(a) * 9
        c.ellipse(round(x - r + 0.5), round(y - r + 0.5), round(x + r - 0.5), round(y + r - 0.5), fill=phase < 3)
    return c


ANIMATED_ICONS = {
    "Common/Loading_24": loading_24,
}
