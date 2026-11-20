#!/usr/bin/env python3
"""Screen previews: draw app screens with the firmware's own drawing code on a PC.

The real canvas, elements, icons, fonts (u8g2) and the app's draw callback are compiled for the
host, with sanitizers, against the small furi in screen_preview/stubs. A preview driver puts the app
in a state and saves what its draw callback draws; this script turns those into PNGs the size of
the Flipper's screen and larger, and a contact sheet.

    python3 scripts/screen_preview.py [out_dir] [--only NAME] [--scale 4]

The firmware must have been built once (./fbt), for the compiled icons.
"""

import argparse
import glob
import os
import subprocess
import sys
import tempfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
HERE = os.path.join(ROOT, "scripts", "screen_preview")

# preview driver -> the app sources it needs besides the one it includes
PREVIEWS = {
    "tracker_detector": ["applications/system/ble_tools/ble_seen.c", "applications/system/ble_tools/ble_tools.c"],
    "find_my_stuff": ["applications/system/ble_tools/ble_seen.c", "applications/system/ble_tools/ble_tools.c"],
    "crowd_meter": ["applications/system/ble_tools/ble_seen.c", "applications/system/ble_tools/ble_tools.c"],
    "battery_history": ["applications/system/battery_history/battery_stats.c"],
    "updater_descent": ["applications/system/updater/views/updater_descent.c"],
    "lock_clock": ["applications/services/desktop/helpers/desktop_lock_clock.c"],
    "keyboard": [],
    "submenu": [],
}

GUI_SOURCES = [
    "applications/services/gui/canvas.c",
    "applications/services/gui/elements.c",
    "applications/services/gui/icon.c",
    "applications/services/gui/icon_animation.c",
    "applications/services/notification/notification_messages.c",
    "applications/services/notification/notification_messages_notes.c",
    "lib/toolbox/compress.c",
    "lib/heatshrink/heatshrink_decoder.c",
    "lib/heatshrink/heatshrink_encoder.c",
    "furi/core/string.c",
]

# Flipper screen colours: backlit orange, dark pixels
BACKGROUND = (255, 130, 0)
PIXEL = (20, 12, 0)


def compiled_assets():
    found = sorted(glob.glob(os.path.join(ROOT, "build", "f7-firmware-*", "assets", "compiled", "assets_icons.c")))
    if not found:
        sys.exit("No compiled icons: build the firmware once (./fbt)")
    return os.path.dirname(found[0])


def sources():
    u8g2 = [p for p in glob.glob(os.path.join(ROOT, "lib", "u8g2", "*.c")) if not p.endswith("u8g2_glue.c")]
    return u8g2 + [os.path.join(ROOT, p) for p in GUI_SOURCES]


def build(name, out, sanitize=True):
    assets = compiled_assets()
    includes = [
        os.path.join(HERE, "stubs"),
        os.path.join(ROOT, "applications", "services"),
        os.path.join(ROOT, "lib", "u8g2"),
        os.path.join(ROOT, "lib", "mlib"),
        os.path.join(ROOT, "lib"),
        ROOT,
        os.path.join(ROOT, "furi"),
        os.path.join(ROOT, "targets", "f7", "ble_glue"),
        assets,
    ]
    command = ["gcc", "-std=gnu11", "-g", "-O1", "-w", "-DFURI_NDEBUG", "-D_ATTRIBUTE(attrs)=__attribute__(attrs)"]
    if sanitize:
        command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-fno-sanitize-recover=undefined"]
    for path in includes:
        command += ["-I", path]
    command += [os.path.join(HERE, f"{name}_preview.c"), os.path.join(HERE, "preview_host.c")]
    command += [os.path.join(ROOT, p) for p in PREVIEWS[name]]
    command += sources() + [os.path.join(assets, "assets_icons.c")]
    command += ["-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections", "-lm", "-o", out]
    subprocess.check_call(command)


def to_png(pbm, png, scale):
    from PIL import Image

    image = Image.open(pbm).convert("1")
    rgb = Image.new("RGB", image.size, BACKGROUND)
    pixels = rgb.load()
    for y in range(image.size[1]):
        for x in range(image.size[0]):
            if image.getpixel((x, y)) == 0:
                pixels[x, y] = PIXEL
    rgb.resize((image.size[0] * scale, image.size[1] * scale), Image.NEAREST).save(png)
    return rgb


def contact_sheet(images, path, scale):
    from PIL import Image, ImageDraw

    columns = 3
    cell_w, cell_h = 128 * scale + 16, 64 * scale + 28
    rows = (len(images) + columns - 1) // columns
    sheet = Image.new("RGB", (columns * cell_w + 16, rows * cell_h + 16), (32, 32, 36))
    draw = ImageDraw.Draw(sheet)
    for i, (name, image) in enumerate(images):
        x, y = 16 + (i % columns) * cell_w, 16 + (i // columns) * cell_h
        sheet.paste(image.resize((128 * scale, 64 * scale), Image.NEAREST), (x, y))
        draw.text((x, y + 64 * scale + 4), name, fill=(220, 220, 220))
    sheet.save(path)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("out", nargs="?", default=os.path.join(ROOT, "build", "screen_preview"))
    parser.add_argument("--only", action="append", choices=sorted(PREVIEWS))
    parser.add_argument("--scale", type=int, default=4)
    args = parser.parse_args()
    os.makedirs(args.out, exist_ok=True)
    images = []
    with tempfile.TemporaryDirectory() as tmp:
        for name in args.only or sorted(PREVIEWS):
            exe = os.path.join(tmp, name)
            build(name, exe)
            subprocess.check_call([exe, tmp])
        for pbm in sorted(glob.glob(os.path.join(tmp, "*.pbm"))):
            screen = os.path.splitext(os.path.basename(pbm))[0]
            images.append((screen, to_png(pbm, os.path.join(args.out, screen + ".png"), args.scale)))
    contact_sheet(images, os.path.join(args.out, "contact_sheet.png"), 2)
    print(f"{len(images)} screens in {args.out}")


if __name__ == "__main__":
    main()
