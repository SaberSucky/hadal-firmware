#!/usr/bin/env python3
"""Build, check and preview Hadal asset packs (see flipper/assets/pack.py)."""

import os
import sys

from flipper.app import App
from flipper.assets import pack

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_ICONS = os.path.join(REPO_ROOT, "assets", "icons")


class Main(App):
    def init(self):
        self.subparsers = self.parser.add_subparsers(help="sub-command help")

        p = self.subparsers.add_parser("pack", help="Build a pack from a source tree")
        p.add_argument("source", help="folder with Anims/, Icons/ and/or Fonts/")
        p.add_argument("output", help="pack folder to write")
        p.set_defaults(func=self.pack)

        p = self.subparsers.add_parser("validate", help="Check a pack like the firmware will")
        p.add_argument("pack", help="pack folder")
        p.add_argument("--icons", default=DEFAULT_ICONS, help="built-in icon sources")
        p.set_defaults(func=self.validate)

        p = self.subparsers.add_parser(
            "icon-list",
            help="List the built-in icons a firmware build uses (the ones a pack can replace)",
        )
        p.add_argument("elf", help="built firmware.elf")
        p.add_argument("output", help="list file to write (assets/icon_override_list.txt)")
        p.add_argument("--icons", default=DEFAULT_ICONS, help="built-in icon sources")
        p.add_argument("--nm", default="arm-none-eabi-nm", help="nm of the toolchain")
        p.set_defaults(func=self.icon_list)

        p = self.subparsers.add_parser("preview", help="Render a pack's icons to a PNG sheet")
        p.add_argument("pack", help="pack folder")
        p.add_argument("output", help="PNG to write")
        p.add_argument("--scale", type=int, default=3)
        p.set_defaults(func=self.preview)

    def pack(self):
        result = pack.pack_tree(self.args.source, self.args.output, self.logger.info)
        self.logger.info(
            f"packed {result['anims']} animations, {result['icons']} icons, {result['fonts']} fonts"
        )
        return 0

    def validate(self):
        index = pack.source_icon_index(self.args.icons) if self.args.icons else None
        report = pack.validate_pack(self.args.pack, index, REPO_ROOT)
        for w in report["warnings"]:
            self.logger.warning(w)
        for e in report["errors"]:
            self.logger.error(e)
        s = report["stats"]
        self.logger.info(
            f"{s['anims']} animations, {s['icons']} icons (~{s['icon_ram']} B of RAM), "
            f"{s['font_bytes']} B of fonts: "
            f"{len(report['errors'])} errors, {len(report['warnings'])} warnings"
        )
        return 1 if report["errors"] else 0

    def icon_list(self):
        import subprocess

        # Apps on the SD card draw their own copies of icons, so only icons the firmware
        # itself links can be replaced by a pack. Unused icons are dropped by the linker.
        out = subprocess.check_output([self.args.nm, self.args.elf], text=True)
        defined = {
            parts[2]
            for parts in (line.split() for line in out.splitlines())
            if len(parts) == 3 and parts[1] in "dDrR" and parts[2][:2] in ("I_", "A_")
        }
        by_symbol = {i["symbol"]: i["path"] for i in pack.source_icons(self.args.icons)}
        paths = sorted(by_symbol[s] for s in defined if s in by_symbol)
        unknown = sorted(s for s in defined if s not in by_symbol)
        for s in unknown:
            self.logger.warning(f"{s}: defined in the firmware but not in {self.args.icons}")
        with open(self.args.output, "w", newline="\n") as f:
            f.write(
                "# Built-in icons an asset pack can replace: the icons the firmware itself draws.\n"
                "# Apps on the SD card use their own copies, so they are not listed. The icon index\n"
                "# (assets_icon_index.c) is generated from this list. Every listed icon is kept in\n"
                "# the firmware, so only add icons the firmware uses. Regenerate with:\n"
                "#   python3 scripts/asset_packer.py icon-list build/f7-firmware-C/firmware.elf \\\n"
                "#       assets/icon_override_list.txt --nm toolchain/current/bin/arm-none-eabi-nm\n"
            )
            f.write("\n".join(paths) + "\n")
        self.logger.info(f"{len(paths)} icons written to {self.args.output}")
        return 0

    def preview(self):
        from PIL import Image, ImageDraw

        scale = self.args.scale
        cells = []
        for path, w, h, blobs, _rate in pack.iter_pack_icons(self.args.pack):
            frames = [pack.raw_to_image(pack.decode_blob(b, w, h), w, h) for b in blobs]
            cells.append((path, frames))
        if not cells:
            self.logger.error("no icons in pack")
            return 1
        pad, label_h, cols = 6, 12, 4
        cell_w = max(f[0].width for _, f in cells) * scale + pad * 2
        cell_w = max(cell_w, 150)
        # simple grid: fixed columns, row height = tallest in the row
        grid = [cells[i : i + cols] for i in range(0, len(cells), cols)]
        row_heights = [
            max(f[0].height * scale for _, f in row) + label_h + pad * 2 for row in grid
        ]
        sheet = Image.new("RGB", (cell_w * cols, sum(row_heights)), (30, 34, 40))
        draw = ImageDraw.Draw(sheet)
        y = 0
        for row, rh in zip(grid, row_heights):
            for c, (path, frames) in enumerate(row):
                x = c * cell_w + pad
                draw.text((x, y + 2), path.split("/", 1)[1][:24], fill=(170, 180, 190))
                im = frames[0].resize((frames[0].width * scale, frames[0].height * scale), Image.NEAREST)
                tint = Image.new("RGB", im.size, (255, 140, 0))
                bg = Image.new("RGB", im.size, (25, 25, 25))
                sheet.paste(Image.composite(bg, tint, im.convert("L")), (x, y + label_h))
            y += rh
        sheet.save(self.args.output)
        self.logger.info(f"wrote {self.args.output} ({len(cells)} icons)")
        return 0


if __name__ == "__main__":
    Main()()
