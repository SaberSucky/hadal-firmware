#!/usr/bin/env python3
"""Host tests for the asset pack format. Run: python3 -m unittest scripts/asset_packer_test.py
(from the repo root). Needs Pillow and heatshrink2."""

import os
import random
import shutil
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from PIL import Image  # noqa: E402

from flipper.assets import pack  # noqa: E402

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def reference_raw(png_path):
    """Independent XBM encoding straight from the pixels: bit x%8 of byte x//8 is set
    where the pixel is black. Not shared with the packer."""
    with Image.open(png_path) as im:
        im = im.convert("1")
        w, h = im.size
        px = im.load()
        row_bytes = (w + 7) // 8
        raw = bytearray(row_bytes * h)
        for y in range(h):
            for x in range(w):
                if px[x, y] == 0:
                    raw[y * row_bytes + (x >> 3)] |= 1 << (x & 7)
        return w, h, bytes(raw)


def read_u8g2_fonts():
    with open(os.path.join(REPO_ROOT, "lib", "u8g2", "u8g2_fonts.c"), errors="replace") as f:
        return f.read()


def make_png(path, w, h, seed, density=0.5):
    rng = random.Random(seed)
    im = Image.new("1", (w, h), 1)
    px = im.load()
    for y in range(h):
        for x in range(w):
            if rng.random() < density:
                px[x, y] = 0
    im.save(path)


class TempDirCase(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="packtest_")
        self.addCleanup(shutil.rmtree, self.tmp, True)

    def p(self, *parts):
        return os.path.join(self.tmp, *parts)


class BlobTests(TempDirCase):
    def test_roundtrip_many_sizes(self):
        # widths around byte boundaries, tiny to full screen, sparse and dense
        sizes = [(1, 1), (7, 3), (8, 8), (9, 9), (15, 15), (16, 16), (26, 8), (64, 64), (128, 64)]
        kinds = set()
        for i, (w, h) in enumerate(sizes):
            for density in (0.0, 0.1, 0.5, 1.0):
                png = self.p(f"t_{w}x{h}_{density}.png")
                make_png(png, w, h, seed=i, density=density)
                width, height, blob = pack.encode_png(png)
                self.assertEqual((width, height), (w, h))
                kinds.add(blob[0])
                _, _, want = reference_raw(png)
                self.assertEqual(pack.decode_blob(blob, w, h), want, f"{w}x{h} d={density}")
        # both on-disk forms must have been exercised
        self.assertEqual(kinds, {0, 1})

    def test_decode_rejects_bad_blobs(self):
        with self.assertRaises(pack.PackError):
            pack.decode_blob(b"", 8, 8)
        with self.assertRaises(pack.PackError):
            pack.decode_blob(b"\x07" + b"\x00" * 8, 8, 8)  # unknown type
        with self.assertRaises(pack.PackError):
            pack.decode_blob(b"\x00" + b"\x00" * 7, 8, 8)  # too short
        with self.assertRaises(pack.PackError):
            pack.decode_blob(b"\x00" + b"\x00" * 9, 8, 8)  # too long
        with self.assertRaises(pack.PackError):
            pack.decode_blob(b"\x01\x00\x05\x00abc", 8, 8)  # length lies
        with self.assertRaises(pack.PackError):
            pack.decode_blob(b"\x01\x01\x00\x00", 8, 8)  # reserved byte set

    def test_bmx_and_meta(self):
        blob = b"\x00" + b"\xaa" * 8
        data = pack.bmx_pack(8, 8, blob)
        self.assertEqual(pack.bmx_parse(data), (8, 8, blob))
        meta = pack.anim_meta_pack(26, 8, 4, 12)
        self.assertEqual(len(meta), 16)
        self.assertEqual(pack.anim_meta_parse(meta), (26, 8, 4, 12))
        with self.assertRaises(pack.PackError):
            pack.anim_meta_parse(meta[:15])

    def test_hash_is_fnv1a(self):
        # published FNV-1a 32 test vectors
        self.assertEqual(pack.icon_path_hash(""), 0x811C9DC5)
        self.assertEqual(pack.icon_path_hash("a"), 0xE40C292C)
        self.assertEqual(pack.icon_path_hash("foobar"), 0xBF9CF968)


class FontTests(unittest.TestCase):
    def test_extract_matches_declared_size(self):
        # every stock slot: extracted length must equal the size the source declares
        text = read_u8g2_fonts()
        import re

        for slot, name in pack.STOCK_FONTS.items():
            decl = pack.find_u8g2_font_source(text, name)
            declared = int(re.search(r"\[(\d+)\]", decl[:120]).group(1))
            data = pack.extract_u8g2_font(decl, name)
            self.assertEqual(len(data), declared, slot)
            info = pack.font_info(data)
            self.assertGreater(info["max_char_height"], 0, slot)
            self.assertEqual(data[-1], 0)

    def test_escapes(self):
        src = 'const uint8_t f[4] U8G2_FONT_SECTION("f") = "\\1\\10\\x41\\377";'
        self.assertEqual(pack.extract_u8g2_font(src), b"\x01\x08A\xff\x00")


class PackTreeTests(TempDirCase):
    def build_source(self):
        icons = self.p("src", "Icons")
        os.makedirs(os.path.join(icons, "StatusBar"))
        os.makedirs(os.path.join(icons, "MainMenu", "NFC_14"))
        make_png(os.path.join(icons, "StatusBar", "Battery_26x8.png"), 26, 8, 1)
        make_png(os.path.join(icons, "StatusBar", "Muted_8x8.png"), 8, 8, 2)
        anim = os.path.join(icons, "MainMenu", "NFC_14")
        for i in range(1, 5):
            make_png(os.path.join(anim, f"frame_{i:02d}.png"), 14, 14, 10 + i)
        with open(os.path.join(anim, "frame_rate"), "w") as f:
            f.write("3")
        return self.p("src")

    def test_pack_and_read_back(self):
        src = self.build_source()
        out = self.p("pack")
        result = pack.pack_tree(src, out)
        self.assertEqual(result, {"anims": 0, "icons": 3, "fonts": 0})
        self.assertTrue(os.path.isfile(os.path.join(out, "Icons", "StatusBar", "Battery_26x8.bmx")))
        self.assertTrue(os.path.isfile(os.path.join(out, "Icons", "MainMenu", "NFC_14", "meta")))
        self.assertTrue(os.path.isfile(os.path.join(out, "Icons", "MainMenu", "NFC_14", "frame_03.bm")))

        got = {path: (w, h, blobs, rate) for path, w, h, blobs, rate in pack.iter_pack_icons(out)}
        self.assertEqual(set(got), {"StatusBar/Battery_26x8", "StatusBar/Muted_8x8", "MainMenu/NFC_14"})
        w, h, blobs, rate = got["StatusBar/Battery_26x8"]
        _, _, want = reference_raw(os.path.join(src, "Icons", "StatusBar", "Battery_26x8.png"))
        self.assertEqual((w, h, rate), (26, 8, 0))
        self.assertEqual(pack.decode_blob(blobs[0], w, h), want)
        w, h, blobs, rate = got["MainMenu/NFC_14"]
        self.assertEqual((w, h, len(blobs), rate), (14, 14, 4, 3))
        for i, blob in enumerate(blobs, start=1):
            _, _, want = reference_raw(os.path.join(src, "Icons", "MainMenu", "NFC_14", f"frame_{i:02d}.png"))
            self.assertEqual(pack.decode_blob(blob, 14, 14), want)

    def test_validate_matches_index_and_flags_mismatch(self):
        src = self.build_source()
        out = self.p("pack")
        pack.pack_tree(src, out)
        good = {
            "StatusBar/Battery_26x8": (26, 8, 1),
            "StatusBar/Muted_8x8": (8, 8, 1),
            "MainMenu/NFC_14": (14, 14, 4),
        }
        report = pack.validate_pack(out, good)
        self.assertEqual(report["errors"], [])
        self.assertEqual(report["stats"]["icons"], 3)
        self.assertGreater(report["stats"]["icon_ram"], 0)

        bad = dict(good, **{"StatusBar/Battery_26x8": (27, 8, 1), "MainMenu/NFC_14": (14, 14, 5)})
        report = pack.validate_pack(out, bad)
        self.assertEqual(len(report["errors"]), 2)

        report = pack.validate_pack(out, {"Other/Thing_1x1": (1, 1, 1)})
        self.assertEqual(report["errors"], [])
        self.assertEqual(len(report["warnings"]), 3)  # none match a built-in icon

    def test_validate_catches_corruption(self):
        src = self.build_source()
        out = self.p("pack")
        pack.pack_tree(src, out)
        path = os.path.join(out, "Icons", "StatusBar", "Muted_8x8.bmx")
        with open(path, "rb") as f:
            data = f.read()
        with open(path, "wb") as f:
            f.write(data[:-3])  # truncate the bitmap
        report = pack.validate_pack(out, None)
        self.assertTrue(any("Muted_8x8" in e for e in report["errors"]))

    def test_ram_budget_enforced(self):
        out = self.p("pack")
        cat = os.path.join(out, "Icons", "Big")
        os.makedirs(cat)
        # incompressible 128x64 icons: ~1 KB each, 30 of them blows the 24 KB budget
        rng = random.Random(7)
        for i in range(30):
            raw = bytes(rng.randrange(256) for _ in range(16 * 64))
            with open(os.path.join(cat, f"Noise{i}_128x64.bmx"), "wb") as f:
                f.write(pack.bmx_pack(128, 64, b"\x00" + raw))
        report = pack.validate_pack(out, None)
        self.assertTrue(any("RAM" in e for e in report["errors"]))

    def test_mask_twin_names(self):
        self.assertEqual(pack.mask_twin("StatusBar/Bolt_8x8"), "StatusBar/Bolt_mask_8x8")
        self.assertEqual(pack.mask_twin("StatusBar/Bolt_mask_8x8"), "StatusBar/Bolt_8x8")
        self.assertIsNone(pack.mask_twin("MainMenu/NFC"))  # no size suffix: must not crash
        self.assertIsNone(pack.mask_twin("Dolphin/NoSize_"))
        self.assertTrue(pack.is_mask("StatusBar/Bolt_mask_8x8"))
        self.assertFalse(pack.is_mask("StatusBar/Bolt_8x8"))

    def test_icon_without_size_in_name_validates(self):
        out = self.p("pack")
        cat = os.path.join(out, "Icons", "Misc")
        os.makedirs(cat)
        with open(os.path.join(cat, "Plain.bmx"), "wb") as f:
            f.write(pack.bmx_pack(8, 8, b"\x00" + b"\x00" * 8))
        report = pack.validate_pack(out, {"Misc/Plain": (8, 8, 1)})
        self.assertEqual(report["errors"], [])
        self.assertEqual(report["warnings"], [])

    def test_icon_replaced_without_mask_warns(self):
        out = self.p("pack")
        cat = os.path.join(out, "Icons", "StatusBar")
        os.makedirs(cat)
        with open(os.path.join(cat, "Bolt_8x8.bmx"), "wb") as f:
            f.write(pack.bmx_pack(8, 8, b"\x00" + b"\x00" * 8))
        index = {"StatusBar/Bolt_8x8": (8, 8, 1), "StatusBar/Bolt_mask_8x8": (8, 8, 1)}
        report = pack.validate_pack(out, index)
        self.assertTrue(any("without its mask" in w for w in report["warnings"]))

    def test_mask_pairing_warning(self):
        out = self.p("pack")
        cat = os.path.join(out, "Icons", "StatusBar")
        os.makedirs(cat)
        blob = b"\x00" + b"\x00" * 8
        with open(os.path.join(cat, "Bolt_mask_8x8.bmx"), "wb") as f:
            f.write(pack.bmx_pack(8, 8, blob))
        report = pack.validate_pack(out, None)
        self.assertTrue(any("mask replaced without its icon" in w for w in report["warnings"]))

    def test_fonts_packed_and_metric_checked(self):
        text = read_u8g2_fonts()
        src_c = pack.find_u8g2_font_source(text, pack.STOCK_FONTS["Secondary"])
        fonts = self.p("src", "Fonts")
        os.makedirs(fonts)
        with open(os.path.join(fonts, "Secondary.c"), "w") as f:
            f.write(src_c)
        out = self.p("pack")
        result = pack.pack_tree(self.p("src"), out)
        self.assertEqual(result["fonts"], 1)
        report = pack.validate_pack(out, None, REPO_ROOT)
        self.assertEqual(report["errors"], [])  # the stock font matches its own slot

        # the same font in the wrong slot changes layout: must be flagged
        shutil.move(os.path.join(out, "Fonts", "Secondary.u8f"), os.path.join(out, "Fonts", "Primary.u8f"))
        report = pack.validate_pack(out, None, REPO_ROOT)
        self.assertTrue(any("would be rejected" in e for e in report["errors"]))


class NestedTests(TempDirCase):
    def test_nested_category_roundtrip_and_index(self):
        icons = self.p("src", "Icons")
        os.makedirs(os.path.join(icons, "BLE", "BLE_HID"))
        os.makedirs(os.path.join(icons, "BLE", "Anim_8", ""), exist_ok=True)
        make_png(os.path.join(icons, "BLE", "BLE_HID", "Ok_btn_9x9.png"), 9, 9, 3)
        make_png(os.path.join(icons, "BLE", "Top_5x5.png"), 5, 5, 4)
        for i in (1, 2):
            make_png(os.path.join(icons, "BLE", "Anim_8", f"frame_{i:02d}.png"), 8, 8, 20 + i)
        with open(os.path.join(icons, "BLE", "Anim_8", "frame_rate"), "w") as f:
            f.write("2")
        out = self.p("pack")
        self.assertEqual(pack.pack_tree(self.p("src"), out)["icons"], 3)
        got = {path: (w, h, len(b)) for path, w, h, b, _ in pack.iter_pack_icons(out)}
        self.assertEqual(
            got,
            {"BLE/BLE_HID/Ok_btn_9x9": (9, 9, 1), "BLE/Top_5x5": (5, 5, 1), "BLE/Anim_8": (8, 8, 2)},
        )
        # the validator's view of the built-in icons agrees with the packer's view of paths
        self.assertEqual(pack.source_icon_index(icons), got)
        self.assertEqual(pack.validate_pack(out, got)["errors"], [])

    def test_png_at_icons_root_is_skipped_not_fatal(self):
        icons = self.p("src", "Icons")
        os.makedirs(icons)
        make_png(os.path.join(icons, "Loose_8x8.png"), 8, 8, 1)
        messages = []
        self.assertEqual(pack.pack_icons(icons, self.p("o"), messages.append), 0)
        self.assertTrue(any("category folder" in m for m in messages))


class AnimTests(TempDirCase):
    def make_anim_source(self, frames=4):
        src = self.p("src", "Anims")
        anim = os.path.join(src, "L1_Test_64x32")
        os.makedirs(anim)
        with open(os.path.join(src, "manifest.txt"), "w") as f:
            f.write("Filetype: Flipper Animation Manifest\nVersion: 1\n\n"
                    "Name: L1_Test_64x32\nMin butthurt: 0\nMax butthurt: 5\n"
                    "Min level: 1\nMax level: 3\nWeight: 3\n")
        with open(os.path.join(anim, "meta.txt"), "w") as f:
            f.write("Filetype: Flipper Animation\nVersion: 1\n\nWidth: 64\nHeight: 32\n"
                    f"Passive frames: {frames}\nActive frames: 0\n"
                    f"Frames order: {' '.join(str(i) for i in range(frames))}\n"
                    "Active cycles: 0\nFrame rate: 2\nDuration: 3600\nActive cooldown: 0\n\n"
                    "Bubble slots: 0\n")
        for i in range(frames):
            make_png(os.path.join(anim, f"frame_{i}.png"), 64, 32, 100 + i)
        return src

    def test_anims_pack_and_validate(self):
        self.make_anim_source()
        out = self.p("pack")
        result = pack.pack_tree(self.p("src"), out)
        self.assertEqual(result["anims"], 1)
        report = pack.validate_pack(out, None)
        self.assertEqual(report["errors"], [])
        self.assertEqual(report["stats"]["anims"], 1)
        # frames really decode to the source pixels
        with open(os.path.join(out, "Anims", "L1_Test_64x32", "frame_2.bm"), "rb") as f:
            blob = f.read()
        _, _, want = reference_raw(self.p("src", "Anims", "L1_Test_64x32", "frame_2.png"))
        self.assertEqual(pack.decode_blob(blob, 64, 32), want)

    def test_missing_and_wrong_size_frames_flagged(self):
        self.make_anim_source()
        out = self.p("pack")
        pack.pack_tree(self.p("src"), out)
        os.remove(os.path.join(out, "Anims", "L1_Test_64x32", "frame_3.bm"))
        report = pack.validate_pack(out, None)
        self.assertTrue(any("frame_3.bm" in e for e in report["errors"]))

        # a frame of the wrong size must be caught by decoding against the meta size
        make_png(self.p("odd.png"), 32, 32, 5)
        _, _, blob = pack.encode_png(self.p("odd.png"))
        with open(os.path.join(out, "Anims", "L1_Test_64x32", "frame_3.bm"), "wb") as f:
            f.write(blob)
        report = pack.validate_pack(out, None)
        self.assertTrue(any("Anims/L1_Test_64x32" in e for e in report["errors"]))

    def test_manifest_listing_missing_animation(self):
        self.make_anim_source()
        out = self.p("pack")
        pack.pack_tree(self.p("src"), out)
        shutil.rmtree(os.path.join(out, "Anims", "L1_Test_64x32"))
        report = pack.validate_pack(out, None)
        self.assertTrue(any("missing meta.txt" in e for e in report["errors"]))


class RealAssetsTests(TempDirCase):
    """The packer must handle the firmware's own icons, which is what the loader will see."""

    def test_real_icons_roundtrip(self):
        base = os.path.join(REPO_ROOT, "assets", "icons")
        src = self.p("src", "Icons")
        os.makedirs(os.path.join(src, "MainMenu"))
        shutil.copytree(os.path.join(base, "StatusBar"), os.path.join(src, "StatusBar"))
        shutil.copytree(os.path.join(base, "MainMenu", "NFC_14"), os.path.join(src, "MainMenu", "NFC_14"))

        out = self.p("pack")
        result = pack.pack_tree(self.p("src"), out)
        self.assertGreater(result["icons"], 10)

        seen = 0
        for path, w, h, blobs, _rate in pack.iter_pack_icons(out):
            seen += 1
            category, name = path.split("/")
            if len(blobs) == 1:
                png = os.path.join(base, category, name + ".png")
                _, _, want = reference_raw(png)
                self.assertEqual(pack.decode_blob(blobs[0], w, h), want, path)
            else:
                for i, blob in enumerate(blobs, start=1):
                    png = os.path.join(base, category, name, f"frame_{i:02d}.png")
                    _, _, want = reference_raw(png)
                    self.assertEqual(pack.decode_blob(blob, w, h), want, f"{path} frame {i}")
        self.assertEqual(seen, result["icons"])

    def test_real_source_index_covers_every_icon(self):
        base = os.path.join(REPO_ROOT, "assets", "icons")
        index = pack.source_icon_index(base)
        # 245 static + 19 animated; nested folders such as BLE/BLE_HID must be included
        self.assertEqual(len(index), 264)
        self.assertIn("BLE/BLE_HID/Ok_btn_9x9", index)
        self.assertEqual(index["BLE/BLE_HID/Ok_btn_9x9"], (9, 9, 1))
        hashes = {pack.icon_path_hash(p) for p in index}
        self.assertEqual(len(hashes), len(index), "path hashes must be unique")


if __name__ == "__main__":
    unittest.main()
