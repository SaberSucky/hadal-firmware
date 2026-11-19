#!/usr/bin/env python3
"""Generate the Hadal asset pack and the slideshow frames, from the scripts next to this file.

    python3 assets/hadal_pack/build.py --install

writes the pack to applications/main/hadal_resources/resources/asset_packs/Hadal (it reaches the SD
card with the firmware's resources), the first-start / firmware-updated frames to assets/slideshow,
and the icon of the on-device update screen to assets/icons/Update. Without --install it builds into --out (default: a temporary folder) and only
reports. --previews DIR also writes contact sheets of everything for a look.
"""

import argparse
import os
import shutil
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(REPO, "scripts"))

import anims  # noqa: E402
import boot  # noqa: E402
import fontstyle  # noqa: E402
import icons  # noqa: E402
import updater_art  # noqa: E402
from flipper.assets import pack  # noqa: E402
from pixel import contact_sheet  # noqa: E402

PACK_NAME = "Hadal"
PACK_DIR = os.path.join(REPO, "applications", "main", "hadal_resources", "resources", "asset_packs", PACK_NAME)
SLIDESHOW_DIR = os.path.join(REPO, "assets", "slideshow")

LOOP = 8

# name, frame function, passive frames, active frames, active cycles, frame rate, extra
ANIMATIONS = [
    ("L1_Anglerfish_128x64", "angler"),
    ("L1_Jellyfish_128x64", "jelly"),
    ("L2_Bathyscaphe_128x64", "bathy"),
    ("L1_Vent_128x64", "vent"),
]

ANGLER_BUBBLES = [
    # slot, x, y, text, horizontal / vertical alignment of the bubble's corner, start, end frame
    (0, 50, 41, "The deep is\\nquiet today.", "Right", "Bottom", 8, 15),
    (0, 50, 41, "Stay a while.", "Right", "Bottom", 16, 23),
    (1, 50, 41, "Nice lure,\\nright?", "Right", "Bottom", 8, 15),
    (1, 50, 41, "Do not touch.", "Right", "Bottom", 16, 23),
]


def animation_frames(kind):
    """The frames of an animation: one canvas each, in file order."""
    if kind == "angler":
        passive = [anims.anglerfish_frame(p, LOOP) for p in range(LOOP)]
        # eight more: the same drift, but the fish talks (jaw opening and closing, bubbles)
        jaws = [1, 3, 5, 4, 2, 4, 5, 2]
        active = [anims.anglerfish_frame(p, LOOP, jaw=jaws[p], talking_bubbles=1 + p % 3) for p in range(LOOP)]
        return passive + active
    fn = {"jelly": anims.jellyfish_frame, "bathy": anims.bathyscaphe_frame, "vent": anims.vent_frame}[kind]
    return [fn(p, LOOP) for p in range(LOOP)]


def meta_text(kind, frame_count):
    passive = LOOP
    active = frame_count - LOOP
    lines = [
        "Filetype: Flipper Animation",
        "Version: 1",
        "",
        "Width: 128",
        "Height: 64",
        f"Passive frames: {passive}",
        f"Active frames: {active}",
        "Frames order: " + " ".join(str(i) for i in range(frame_count)),
        f"Active cycles: {2 if active else 0}",
        "Frame rate: 3",
        "Duration: 900",
        f"Active cooldown: {6 if active else 0}",
        "",
    ]
    if kind == "angler":
        slots = sorted({b[0] for b in ANGLER_BUBBLES})
        lines.append(f"Bubble slots: {len(slots)}")
        for slot, x, y, text, align_h, align_v, start, end in ANGLER_BUBBLES:
            # two cycles of eight active frames: the bubble shows in the second cycle as well
            lines += [
                "",
                f"Slot: {slot}",
                f"X: {x}",
                f"Y: {y}",
                f"Text: {text}",
                f"AlignH: {align_h}",
                f"AlignV: {align_v}",
                f"StartFrame: {start}",
                f"EndFrame: {end}",
            ]
    else:
        lines.append("Bubble slots: 0")
    return "\n".join(lines) + "\n"


def write_sources(src):
    """Write the pack's sources (PNGs, metadata, fonts) the way scripts/asset_packer.py reads them."""
    os.makedirs(os.path.join(src, "Anims"))
    manifest = ["Filetype: Flipper Animation Manifest", "Version: 1", ""]
    sheets = {}
    for name, kind in ANIMATIONS:
        frames = animation_frames(kind)
        folder = os.path.join(src, "Anims", name)
        os.makedirs(folder)
        for i, frame in enumerate(frames):
            frame.save(os.path.join(folder, f"frame_{i}.png"))
        with open(os.path.join(folder, "meta.txt"), "w") as f:
            f.write(meta_text(kind, len(frames)))
        weight = 2 if kind == "vent" else 3
        min_level = 2 if kind == "bathy" else 1
        manifest += [
            f"Name: {name}",
            "Min butthurt: 0",
            "Max butthurt: 14",
            f"Min level: {min_level}",
            "Max level: 3",
            f"Weight: {weight}",
            "",
        ]
        sheets[name] = frames
    with open(os.path.join(src, "Anims", "manifest.txt"), "w") as f:
        f.write("\n".join(manifest).rstrip("\n") + "\n")

    sheets_icons = {}
    for path, make in icons.STATUS_ICONS.items():
        canvas = make()
        dest = os.path.join(src, "Icons", path + ".png")
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        canvas.save(dest)
        sheets_icons[path] = [canvas]
    for path, make in icons.MENU_ICONS.items():
        write_animated_icon(src, path, make, sheets_icons)
    for path, make in icons.ANIMATED_ICONS.items():
        write_animated_icon(src, path, make, sheets_icons)

    fonts_dir = os.path.join(src, "Fonts")
    os.makedirs(fonts_dir)
    for slot, data in fontstyle.build_fonts().items():
        with open(os.path.join(fonts_dir, slot + ".u8f"), "wb") as f:
            f.write(data)
    return sheets, sheets_icons


def write_animated_icon(src, path, make, sheets_icons):
    """An animated icon takes its frame count and rate from the built-in icon it replaces."""
    index = pack.source_icon_index(os.path.join(REPO, "assets", "icons"))
    width, height, frames = index[path]
    rate_file = os.path.join(REPO, "assets", "icons", path, "frame_rate")
    with open(rate_file) as f:
        rate = f.read().strip()
    folder = os.path.join(src, "Icons", path)
    os.makedirs(folder)
    canvases = []
    for i in range(frames):
        canvas = make(i, frames)
        assert (canvas.width, canvas.height) == (width, height), (path, canvas.width, canvas.height)
        canvas.save(os.path.join(folder, f"frame_{i + 1:02d}.png"))
        canvases.append(canvas)
    with open(os.path.join(folder, "frame_rate"), "w") as f:
        f.write(rate)
    sheets_icons[path] = canvases


def build(out, previews=None):
    src = tempfile.mkdtemp(prefix="hadal_src_")
    try:
        sheets, icon_sheets = write_sources(src)
        if os.path.exists(out):
            shutil.rmtree(out)
        result = pack.pack_tree(src, out)
    finally:
        shutil.rmtree(src, True)
    index = pack.source_icon_index(os.path.join(REPO, "assets", "icons"))
    report = pack.validate_pack(out, index, REPO)
    # an icon the firmware does not keep in flash cannot be replaced: do not ship one
    with open(os.path.join(REPO, "assets", "icon_override_list.txt")) as f:
        replaceable = set(pack.parse_icon_list(f.read()))
    for path, *_ in pack.iter_pack_icons(out):
        if path not in replaceable:
            report["errors"].append(f"Icons/{path}: the firmware does not draw this icon, it would do nothing")
    print(f"packed {result}; {report.get('stats')}")
    for line in report.get("errors", []):
        print("ERROR", line)
    for line in report.get("warnings", []):
        print("warning", line)
    if report.get("errors"):
        raise SystemExit(1)
    if previews:
        write_previews(previews, sheets, icon_sheets)
    return result


def slideshow_frames():
    return boot.first_start_frames(), boot.update_frames()


UPDATER_ICON = os.path.join(REPO, "assets", "icons", "Update", "Updating_32x40.png")


def install_updater_icon():
    """The icon on the on-device update screen. The updater draws it from the firmware's own
    icon table, so it replaces the source icon instead of living in the asset pack."""
    updater_art.updating_32x40().save(UPDATER_ICON)
    print("updater icon written")


def install_slideshow():
    first, update = slideshow_frames()
    for folder, frames in (("first_start", first), ("update_default", update)):
        target = os.path.join(SLIDESHOW_DIR, folder)
        for old in os.listdir(target):
            os.remove(os.path.join(target, old))
        for i, frame in enumerate(frames):
            frame.save(os.path.join(target, f"frame_{i:02d}.png"))
    print(f"slideshow: {len(first)} first-start and {len(update)} update frames")


def write_previews(out_dir, sheets, icon_sheets):
    os.makedirs(out_dir, exist_ok=True)
    for name, frames in sheets.items():
        contact_sheet(frames, os.path.join(out_dir, name + ".png"), columns=4, scale=2)
    status = [c[0] for p, c in icon_sheets.items() if p.startswith(("StatusBar/", "SubGhz/"))]
    contact_sheet(status, os.path.join(out_dir, "icons_status.png"), columns=7, scale=8, gap=10)
    menu = [c for p, c in icon_sheets.items() if p.startswith("MainMenu/")]
    contact_sheet([frames[0] for frames in menu], os.path.join(out_dir, "icons_menu.png"), columns=7, scale=8, gap=10)
    first, update = slideshow_frames()
    contact_sheet(first, os.path.join(out_dir, "slideshow_first_start.png"), columns=2, scale=3)
    contact_sheet(update, os.path.join(out_dir, "slideshow_update.png"), columns=2, scale=3)
    # the fonts, as the firmware will draw them next to the built-in ones
    from flipper.assets import u8g2font
    from pixel import Canvas

    rows = []
    for slot, data in fontstyle.build_fonts().items():
        text = "0123456789:" if slot == "BigNumbers" else "Hadal NFC Sub-GHz 0123 Remote"
        for font in (boot.stock_font(slot), u8g2font.parse_font(data)):
            c = Canvas(128, 26 if slot == "BigNumbers" else 16)
            font.draw_text(c.image, 1, 20 if slot == "BigNumbers" else 11, text, lit=0)
            rows.append(c)
    contact_sheet(rows, os.path.join(out_dir, "fonts.png"), columns=1, scale=4)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--install", action="store_true", help="write the pack and the slideshow into the source tree")
    parser.add_argument("--out", help="where to build the pack (default: a temporary folder)")
    parser.add_argument("--previews", help="folder for contact sheets")
    args = parser.parse_args()

    if args.install:
        build(PACK_DIR, args.previews)
        install_slideshow()
        install_updater_icon()
    else:
        out = args.out or os.path.join(tempfile.mkdtemp(prefix="hadal_pack_"), PACK_NAME)
        build(out, args.previews)
        print("built in", out)


if __name__ == "__main__":
    main()
