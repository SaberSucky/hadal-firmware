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


def stock_font_bytes():
    """The five built-in fonts, by slot, straight from lib/u8g2/u8g2_fonts.c."""
    text = read_u8g2_fonts()
    return {
        slot: pack.extract_u8g2_font(pack.find_u8g2_font_source(text, name), name)
        for slot, name in pack.STOCK_FONTS.items()
    }


def write_stock_fonts_c(path):
    """C file with the stock fonts for the host harnesses: stock_fonts[] and stock_font_sizes[]."""
    fonts = stock_font_bytes()
    out = ["#include <stddef.h>", "#include <stdint.h>"]
    for i, slot in enumerate(pack.STOCK_FONTS):
        out.append(f"static const uint8_t stock_{i}[] = {{{','.join(map(str, fonts[slot]))}}};")
    names = ",".join(f"stock_{i}" for i in range(len(fonts)))
    sizes = ",".join(str(len(fonts[slot])) for slot in pack.STOCK_FONTS)
    out.append(f"const uint8_t* const stock_fonts[{len(fonts)}] = {{{names}}};")
    out.append(f"const size_t stock_font_sizes[{len(fonts)}] = {{{sizes}}};")
    with open(path, "w") as f:
        f.write("\n".join(out) + "\n")


def font_chain_end(font):
    """Offset of the entry that ends the 8-bit glyph list."""
    position = 23
    while font[position + 1] != 0:
        position += font[position + 1]
    return position


def with_unicode_offset(font):
    """Set the header's unicode section offset to follow the glyph list, as it must."""
    font = bytearray(font)
    offset = font_chain_end(font) + 2 - 23
    font[21], font[22] = offset >> 8, offset & 0xFF
    return bytes(font)


def pad_font(font, size):
    """A valid font of about `size` bytes: the first glyph repeated after the last one."""
    first = bytes(font[23 : 23 + font[24]])
    end = font_chain_end(font)
    padding = first * max(0, (size - len(font)) // len(first))
    return with_unicode_offset(font[:end] + padding + font[end:])


def variant_font(font, seed):
    """Same length, valid, but not the same bytes: one glyph bit flipped."""
    info = pack.font_info(font)
    rng = random.Random(seed)
    while True:
        candidate = bytearray(font)
        candidate[rng.randrange(23, len(font) - 8)] ^= 1 << rng.randrange(8)
        if bytes(candidate) != font and pack.font_problem(bytes(candidate), info) is None:
            return bytes(candidate)


def overrun_font(font):
    """A font that passes every structural check but whose one glyph wants far more data than it
    has: 127 x 127 pixels, no pixel data. u8g2 would decode on past the end of the font."""
    header = bytearray(font[:23])
    for i in range(2, 9):
        header[i] = 8
    header[17:21] = bytes(4)  # no 'A' / 'a' shortcuts
    glyph = bytes([65, 7, 127, 127, 0, 0, 0])
    body = bytes(header) + glyph + bytes([0, 0]) + bytes([0, 4, 255, 255, 0, 0])
    return with_unicode_offset(body)


def mutate_font(rng, font):
    """The kinds of damage a bad copy, a bad converter or a bit rot would do."""
    font = bytearray(font)
    for _ in range(rng.randint(1, 6)):
        kind = rng.randrange(6)
        if kind == 0:
            font[rng.randrange(len(font))] ^= 1 << rng.randrange(8)
        elif kind == 1:
            font[rng.randrange(len(font))] = rng.randrange(256)
        elif kind == 2:
            font[rng.randrange(min(23, len(font)))] ^= 1 << rng.randrange(8)
        elif kind == 3 and len(font) > 23:
            font[rng.randrange(23, len(font))] = rng.randrange(12)
        elif kind == 4 and len(font) > 1:
            del font[rng.randrange(1, len(font)) :]
        else:
            font += bytes(rng.randrange(256) for _ in range(rng.randint(1, 40)))
    return bytes(font)


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

    def test_canvas_font_table_matches_the_pack_slots(self):
        # The manager and the packer name font slots by position in the canvas's Font enum, and
        # check a pack font against the built-in font of that slot
        import re

        with open(os.path.join(REPO_ROOT, "applications", "services", "gui", "canvas.c")) as f:
            canvas_c = f.read()
        table = re.search(r"canvas_stock_fonts\[FontTotalNumber\] = \{(.*?)\};", canvas_c, re.S).group(1)
        entries = re.findall(r"\[Font(\w+)\]\s*=\s*(u8g2_font_\w+)", table)
        self.assertEqual(entries, list(pack.STOCK_FONTS.items()))

        with open(os.path.join(REPO_ROOT, "applications", "services", "gui", "canvas.h")) as f:
            canvas_h = f.read()
        body = re.search(r"typedef enum \{([^}]*)\} Font;", canvas_h).group(1)
        body = re.sub(r"//.*", "", body)
        names = [n for n in re.findall(r"Font\w+", body) if n != "FontTotalNumber"]
        self.assertEqual(names, ["Font" + slot for slot in pack.STOCK_FONTS])

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


class CFormatTests(TempDirCase):
    """lib/asset_packs/asset_pack_format.c (what the firmware runs) against the Python packer."""

    @classmethod
    def setUpClass(cls):
        import subprocess

        cls.build_dir = tempfile.mkdtemp(prefix="apc_")
        cls.exe = os.path.join(cls.build_dir, "asset_pack_check")
        subprocess.check_call([
            "gcc", "-Wall", "-Wextra", "-Werror",
            "-I", os.path.join(REPO_ROOT, "lib", "asset_packs"),
            "-I", os.path.join(REPO_ROOT, "lib", "heatshrink"),
            "-o", cls.exe,
            os.path.join(REPO_ROOT, "scripts", "host_tests", "asset_pack_check.c"),
            os.path.join(REPO_ROOT, "lib", "asset_packs", "asset_pack_format.c"),
            os.path.join(REPO_ROOT, "lib", "heatshrink", "heatshrink_decoder.c"),
        ])

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.build_dir, True)

    def run_c(self, *args):
        import subprocess

        r = subprocess.run([self.exe, *map(str, args)], capture_output=True, text=True)
        status = int(r.stdout.strip().splitlines()[-1].split()[1])
        return status, r.stdout.strip().splitlines()[:-1]

    def c_blob_ok(self, blob, w, h):
        path = self.p("blob.bin")
        with open(path, "wb") as f:
            f.write(blob)
        return self.run_c("blob", path, w, h)[0] == 0

    @staticmethod
    def py_blob_ok(blob, w, h):
        try:
            pack.decode_blob(blob, w, h)
            return True
        except pack.PackError:
            return False

    def test_hash_matches_python_for_every_builtin_icon(self):
        index = pack.source_icon_index(os.path.join(REPO_ROOT, "assets", "icons"))
        for path in index:
            status, out = self.run_c("hash", path)
            # the harness prints the value then "status 0": the value is the first line
            self.assertEqual(int(out[0], 16), pack.icon_path_hash(path), path)

    def test_valid_and_corrupted_blobs_agree_with_python(self):
        cases = 0
        for i, (w, h) in enumerate([(1, 1), (8, 8), (9, 9), (26, 8), (64, 32), (128, 64)]):
            for density in (0.0, 0.5, 1.0):
                png = self.p(f"c_{w}x{h}_{density}.png")
                make_png(png, w, h, seed=50 + i, density=density)
                _, _, blob = pack.encode_png(png)
                good = [(blob, w, h)]
                bad = [
                    (blob[:-1], w, h),                       # truncated
                    (blob + b"\x00", w, h),                  # extended
                    (b"", w, h),                             # empty
                    (b"\x02" + blob[1:], w, h),              # unknown type
                    (blob, w, min(h + 1, 128) if h < 128 else h - 1),   # wrong size
                    (blob, 129, h),                          # out of range
                ]
                if blob[0] == 1:
                    bad.append((blob[:1] + b"\x01" + blob[2:], w, h))   # reserved byte set
                    bad.append((blob[:2] + b"\xff" + blob[3:], w, h))   # length field lies
                for b, bw, bh in good:
                    self.assertTrue(self.py_blob_ok(b, bw, bh))
                    self.assertTrue(self.c_blob_ok(b, bw, bh), f"{w}x{h} d={density}")
                    cases += 1
                for b, bw, bh in bad:
                    self.assertFalse(self.py_blob_ok(b, bw, bh), f"python accepted bad blob {w}x{h}")
                    self.assertFalse(self.c_blob_ok(b, bw, bh), f"C accepted bad blob {w}x{h}")
                    cases += 1
        self.assertGreater(cases, 100)

    def test_bitmap_over_decode_buffer_rejected(self):
        # 128x128 is 2048 bytes: larger than the canvas decode buffer, so never acceptable
        blob = b"\x00" + b"\x00" * 2048
        self.assertFalse(self.py_blob_ok(blob, 128, 128))
        self.assertFalse(self.c_blob_ok(blob, 128, 128))

    def test_every_real_packed_file_is_accepted_by_c(self):
        """All 264 built-in icons packed by Python: every .bmx and animation frame must pass the C check."""
        import glob

        base = os.path.join(REPO_ROOT, "assets", "icons")
        shutil.copytree(base, self.p("src", "Icons"))
        out = self.p("pack")
        pack.pack_tree(self.p("src"), out)
        files = 0
        for bmx in glob.glob(os.path.join(out, "Icons", "**", "*.bmx"), recursive=True):
            status, lines = self.run_c("bmx", bmx)
            self.assertEqual(status, 0, bmx)
            files += 1
        for meta in glob.glob(os.path.join(out, "Icons", "**", "meta"), recursive=True):
            status, lines = self.run_c("meta", meta)
            self.assertEqual(status, 0, meta)
            w, h, _rate, count = map(int, lines[0].split())
            for i in range(count):
                frame = os.path.join(os.path.dirname(meta), pack.FRAME_NAME % i)
                self.assertTrue(self.c_blob_ok(pack._read(frame), w, h), frame)
                files += 1
        self.assertGreater(files, 264)

    def test_meta_and_bmx_parsing(self):
        meta = self.p("meta")
        with open(meta, "wb") as f:
            f.write(pack.anim_meta_pack(26, 8, 4, 12))
        status, lines = self.run_c("meta", meta)
        self.assertEqual((status, lines[0]), (0, "26 8 4 12"))
        with open(meta, "wb") as f:
            f.write(pack.anim_meta_pack(26, 8, 4, 12)[:15])
        self.assertNotEqual(self.run_c("meta", meta)[0], 0)
        bmx = self.p("x.bmx")
        with open(bmx, "wb") as f:
            f.write(b"\x01\x00\x00\x00")  # shorter than the 9 byte minimum
        self.assertNotEqual(self.run_c("bmx", bmx)[0], 0)

    def test_constants_and_cost_match_python(self):
        with open(os.path.join(REPO_ROOT, "lib", "asset_packs", "asset_pack_format.h")) as f:
            header = f.read()
        import re

        def macro(name):
            m = re.search(rf"#define {name}\s+\(?(\d+)u(?:\s*\*\s*(\d+)u)?\)?", header)
            return int(m.group(1)) * (int(m.group(2)) if m.group(2) else 1)

        self.assertEqual(macro("ASSET_PACK_MAX_ICON_DIM"), pack.MAX_ICON_DIM)
        self.assertEqual(macro("ASSET_PACK_MAX_RAW_BYTES"), pack.MAX_RAW_BYTES)
        self.assertEqual(macro("ASSET_PACK_MAX_ICON_FRAMES"), pack.MAX_ICON_FRAMES)
        self.assertEqual(macro("ASSET_PACK_ICON_RAM_BUDGET"), pack.ICON_RAM_BUDGET)
        self.assertEqual(macro("ASSET_PACK_ICON_OVERHEAD"), pack.ICON_RAM_OVERHEAD)
        self.assertEqual(macro("ASSET_PACK_FRAME_OVERHEAD"), pack.FRAME_RAM_OVERHEAD)
        for frames, nbytes in [(1, 100), (4, 333), (32, 2000)]:
            status, out = self.run_c("cost", frames, nbytes)
            want = pack.ICON_RAM_OVERHEAD + frames * pack.FRAME_RAM_OVERHEAD + nbytes
            self.assertEqual(int(out[0]), want)

    def test_font_slots_and_limits_match_python(self):
        status, out = self.run_c("slots", "x")
        self.assertEqual(out[:-1], list(pack.STOCK_FONTS))
        slots, max_bytes, budget = map(int, out[-1].split())
        self.assertEqual(slots, len(pack.STOCK_FONTS))
        self.assertEqual(max_bytes, pack.MAX_FONT_BYTES)
        self.assertEqual(budget, pack.FONT_RAM_BUDGET)

    def test_font_metrics_match_python(self):
        text = read_u8g2_fonts()
        for slot, name in pack.STOCK_FONTS.items():
            data = pack.extract_u8g2_font(pack.find_u8g2_font_source(text, name), name)
            path = self.p(f"{slot}.u8f")
            with open(path, "wb") as f:
                f.write(data)
            status, out = self.run_c("font", path)
            info = pack.font_info(data)
            self.assertEqual(
                tuple(map(int, out[0].split())),
                (info["max_char_height"], info["ascent_A"], info["descent_g"]),
                slot,
            )


def _have_sanitizer(flag):
    import subprocess

    try:
        r = subprocess.run(
            ["gcc", f"-fsanitize={flag}", "-x", "c", "-", "-o", os.devnull],
            input="int main(void){return 0;}", capture_output=True, text=True,
        )
        return r.returncode == 0
    except OSError:
        return False


class HostLoaderTests(TempDirCase):
    """The real lib/asset_packs/asset_packs.c and the real firmware decoder, compiled on the host
    against stand-ins for furi and storage (scripts/host_tests), loading packs the packer wrote."""

    FONT_GRACE_MS = 300

    @classmethod
    def build(cls, name, sanitizer, source_format=None, grace_ms=None):
        import subprocess

        exe = os.path.join(cls.build_dir, name)
        fmt = source_format or os.path.join(REPO_ROOT, "lib", "asset_packs", "asset_pack_format.c")
        gen = os.path.join(cls.build_dir, "gen")
        subprocess.check_call(
            [
                "gcc", "-std=gnu11", "-g", "-O1", f"-fsanitize={sanitizer}", "-fno-omit-frame-pointer",
                "-I", os.path.join(REPO_ROOT, "scripts", "host_tests", "stubs"),
                "-I", os.path.join(REPO_ROOT, "applications", "services"),
                "-I", os.path.join(REPO_ROOT, "furi"),
                "-I", os.path.join(REPO_ROOT, "lib"),
                "-I", REPO_ROOT,
                "-I", os.path.join(REPO_ROOT, "lib", "asset_packs"),
                "-I", os.path.join(REPO_ROOT, "lib", "heatshrink"),
                "-I", gen,
                f"-DASSET_PACKS_FONT_GRACE_MS={cls.FONT_GRACE_MS if grace_ms is None else grace_ms}",
                "-o", exe,
                os.path.join(REPO_ROOT, "scripts", "host_tests", "asset_packs_host.c"),
                os.path.join(gen, "stock_fonts.c"),
                os.path.join(REPO_ROOT, "scripts", "host_tests", "stubs", "storage_host.c"),
                fmt,
                os.path.join(REPO_ROOT, "lib", "toolbox", "compress.c"),
                os.path.join(REPO_ROOT, "lib", "heatshrink", "heatshrink_decoder.c"),
                os.path.join(REPO_ROOT, "lib", "heatshrink", "heatshrink_encoder.c"),
                os.path.join(gen, "assets_icons.c"),
                os.path.join(gen, "assets_icon_index.c"),
                "-lpthread",
            ],
            stderr=subprocess.DEVNULL,
        )
        return exe

    @classmethod
    def setUpClass(cls):
        import subprocess

        if not _have_sanitizer("address,undefined"):
            raise unittest.SkipTest("gcc with AddressSanitizer is not available")
        cls.build_dir = tempfile.mkdtemp(prefix="aph_")
        gen = os.path.join(cls.build_dir, "gen")
        os.makedirs(gen)
        # the firmware's real icons and icon index, from the real generator
        subprocess.check_call(
            [sys.executable, os.path.join(REPO_ROOT, "scripts", "assets.py"), "icons",
             os.path.join(REPO_ROOT, "assets", "icons"), gen,
             "--index-list", os.path.join(REPO_ROOT, "assets", "icon_override_list.txt")],
            stdout=subprocess.DEVNULL,
        )
        write_stock_fonts_c(os.path.join(gen, "stock_fonts.c"))
        cls.asan = cls.build("host_asan", "address,undefined")
        cls.tsan = None
        cls.stock_fonts = stock_font_bytes()

        with open(os.path.join(REPO_ROOT, "assets", "icon_override_list.txt")) as f:
            cls.index_paths = pack.parse_icon_list(f.read())
        base = os.path.join(REPO_ROOT, "assets", "icons")

        def source_tree(dest, paths):
            for p in paths:
                src, dst = os.path.join(base, p), os.path.join(dest, "Icons", p)
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                if os.path.isdir(src):
                    shutil.copytree(src, dst)
                else:
                    shutil.copy(src + ".png", dst + ".png")

        # "Real": every icon the firmware draws. "Small": status bar and main menu.
        cls.template = os.path.join(cls.build_dir, "template")
        real_src = os.path.join(cls.build_dir, "real_src")
        source_tree(real_src, cls.index_paths)
        pack.pack_tree(real_src, os.path.join(cls.template, "ext", "asset_packs", "Real"))
        # "Real" also replaces every font, with valid fonts that differ from the built-in ones
        fonts_dir = os.path.join(cls.template, "ext", "asset_packs", "Real", "Fonts")
        os.makedirs(fonts_dir)
        for n, slot in enumerate(pack.STOCK_FONTS):
            with open(os.path.join(fonts_dir, f"{slot}.u8f"), "wb") as f:
                f.write(variant_font(cls.stock_fonts[slot], 100 + n))
        small_src = os.path.join(cls.build_dir, "small_src")
        shutil.copytree(os.path.join(base, "StatusBar"), os.path.join(small_src, "Icons", "StatusBar"))
        shutil.copytree(os.path.join(base, "MainMenu"), os.path.join(small_src, "Icons", "MainMenu"))
        pack.pack_tree(small_src, os.path.join(cls.template, "ext", "asset_packs", "Small"))
        cls.small_loadable = sorted(
            p for p in cls.index_paths if p.startswith(("StatusBar/", "MainMenu/"))
        )

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.build_dir, True)

    def setUp(self):
        super().setUp()
        self.root = self.p("root")
        shutil.copytree(self.template, self.root)
        os.makedirs(os.path.join(self.root, "int"), exist_ok=True)

    def run_host(self, *args, exe=None, log=False, timeout=120):
        import subprocess

        env = dict(os.environ, **({"HOST_LOG": "1"} if log else {}))
        return subprocess.run(
            [exe or self.asan, args[0], self.root, *map(str, args[1:])],
            capture_output=True, text=True, env=env, timeout=timeout,
        )

    def parse_verify(self, result):
        loaded, replaced, same = 0, {}, {}
        for line in result.stdout.splitlines():
            parts = line.split()
            if parts[0] == "loaded":
                loaded = int(parts[1])
            elif parts[0] == "R":
                replaced[int(parts[1], 16)] = parts[2]
        return loaded, replaced

    def test_small_pack_loads_exactly_its_known_icons_and_draws_identically(self):
        result = self.run_host("verify", "Small")
        self.assertEqual(result.returncode, 0, result.stderr[-800:])
        loaded, replaced = self.parse_verify(result)
        want = {pack.icon_path_hash(p) for p in self.small_loadable}
        self.assertEqual(set(replaced), want)
        self.assertEqual(loaded, len(want))
        # replacements were packed from the same PNGs: every one draws exactly like the built-in
        self.assertEqual(set(replaced.values()), {"same"})
        # icons not in the pack are untouched
        self.assertGreater(len(self.index_paths), len(want))

    def test_a_changed_icon_is_the_one_that_differs(self):
        path = os.path.join(self.root, "ext", "asset_packs", "Small", "Icons", "StatusBar", "Muted_8x8.bmx")
        png = self.p("inv.png")
        im = Image.open(os.path.join(REPO_ROOT, "assets", "icons", "StatusBar", "Muted_8x8.png")).convert("1")
        from PIL import ImageOps

        ImageOps.invert(im.convert("L")).convert("1").save(png)
        w, h, blob = pack.encode_png(png)
        with open(path, "wb") as f:
            f.write(pack.bmx_pack(w, h, blob))
        result = self.run_host("verify", "Small")
        self.assertEqual(result.returncode, 0)
        _, replaced = self.parse_verify(result)
        differing = {h for h, v in replaced.items() if v == "diff"}
        self.assertEqual(differing, {pack.icon_path_hash("StatusBar/Muted_8x8")})

    def test_full_pack_respects_the_ram_budget(self):
        result = self.run_host("verify", "Real", log=True)
        self.assertEqual(result.returncode, 0, result.stderr[-800:])
        loaded, replaced = self.parse_verify(result)
        self.assertLess(loaded, len(self.index_paths))  # these icons need more than 24 KB
        self.assertEqual(set(replaced.values()), {"same"})
        self.assertIn("icon RAM budget used up", result.stderr)
        used = int(result.stderr.split(" icons, ")[1].split(" B")[0])
        self.assertLessEqual(used, pack.ICON_RAM_BUDGET)

    def test_wrong_size_unknown_and_wrong_frame_count_icons_are_ignored(self):
        icons = os.path.join(self.root, "ext", "asset_packs", "Odd", "Icons")
        os.makedirs(os.path.join(icons, "StatusBar"))
        os.makedirs(os.path.join(icons, "Nowhere"))
        make_png(self.p("a.png"), 27, 8, 1)   # Battery_26x8 is 26 wide
        make_png(self.p("b.png"), 5, 5, 2)    # not a built-in icon
        make_png(self.p("c.png"), 8, 8, 3)    # Muted_8x8: right size, accepted
        for name, png in (("StatusBar/Battery_26x8", "a"), ("Nowhere/Thing_5x5", "b"), ("StatusBar/Muted_8x8", "c")):
            w, h, blob = pack.encode_png(self.p(png + ".png"))
            with open(os.path.join(icons, name + ".bmx"), "wb") as f:
                f.write(pack.bmx_pack(w, h, blob))
        # animated icon with 3 frames where the firmware icon has 4
        anim = os.path.join(icons, "MainMenu", "NFC_14")
        os.makedirs(anim)
        for i in range(3):
            make_png(self.p("f.png"), 14, 14, 10 + i)
            _, _, blob = pack.encode_png(self.p("f.png"))
            with open(os.path.join(anim, pack.FRAME_NAME % i), "wb") as f:
                f.write(blob)
        with open(os.path.join(anim, "meta"), "wb") as f:
            f.write(pack.anim_meta_pack(14, 14, 3, 3))
        result = self.run_host("verify", "Odd")
        self.assertEqual(result.returncode, 0, result.stderr[-800:])
        loaded, replaced = self.parse_verify(result)
        self.assertEqual(set(replaced), {pack.icon_path_hash("StatusBar/Muted_8x8")})
        self.assertEqual(loaded, 1)

    def test_missing_pack_or_missing_icons_folder_is_harmless(self):
        for name in ("Nope", "../escape", "", "a/b"):
            result = self.run_host("verify", name)
            self.assertEqual(result.returncode, 0, name)
            self.assertEqual(self.parse_verify(result)[0], 0, name)
        os.makedirs(os.path.join(self.root, "ext", "asset_packs", "Empty"))
        self.assertEqual(self.parse_verify(self.run_host("verify", "Empty"))[0], 0)

    def test_unload_and_reload_cycles(self):
        result = self.run_host("cycle", "Real", 50)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr[-800:])
        self.assertNotIn("FAIL", result.stdout)
        self.assertIn("cycles done", result.stdout)

    def test_selecting_another_pack_cancels_the_first(self):
        result = self.run_host("switch", "Real", "Small")
        self.assertEqual(result.returncode, 0, result.stderr[-800:])
        self.assertIn("active Small", result.stdout)
        loaded, replaced = self.parse_verify(result)
        self.assertEqual(set(replaced), {pack.icon_path_hash(p) for p in self.small_loadable})

    def test_crash_marker_is_reported_once(self):
        result = self.run_host("crash")
        self.assertEqual(result.returncode, 0)
        self.assertIn("crash 1 0", result.stdout)

    def marker_report(self, name):
        result = self.run_host("marker", name)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr[-800:])
        return {k: int(v) for k, v in (line.rsplit(" ", 1) for line in result.stdout.splitlines())
                if k.startswith("after")}

    def test_marker_watches_an_installed_pack_until_it_is_marked_stable(self):
        report = self.marker_report("Small")
        # present once the pack is installed (a reset now counts against it) ...
        self.assertEqual(report["after load"], 1)
        # ... gone once the firmware has run with it for a while ...
        self.assertEqual(report["after stable"], 0)
        # ... back for the reload after an app, and gone when the pack is dropped
        self.assertEqual(report["after reload"], 1)
        self.assertEqual(report["after deselect"], 0)

    def test_no_marker_for_a_load_that_installs_nothing(self):
        os.makedirs(os.path.join(self.root, "ext", "asset_packs", "Empty"))
        for name in ("Empty", "Missing"):
            report = self.marker_report(name)
            self.assertEqual(report["after load"], 0, name)
            self.assertEqual(report["after reload"], 0, name)

    def test_crashed_load_marker_is_still_reported(self):
        # What a load that was killed halfway leaves behind
        marker = os.path.join(self.root, "int", ".asset_pack_loading")
        with open(marker, "wb"):
            pass
        self.assertIn("crash 1 0", self.run_host("crash").stdout)
        self.assertFalse(os.path.exists(marker))

    def test_drawing_against_unload_reload_switch_asan(self):
        result = self.run_host("stress", "Real", 2)
        self.assertEqual(result.returncode, 0, result.stderr[-1500:])
        self.assertIn("draws", result.stdout)

    @unittest.skipUnless(_have_sanitizer("thread"), "gcc with ThreadSanitizer is not available")
    def test_drawing_against_unload_reload_switch_tsan(self):
        cls = type(self)
        if cls.tsan is None:
            cls.tsan = cls.build("host_tsan", "thread")
        for _ in range(2):
            result = self.run_host("stress", "Real", 2, exe=cls.tsan)
            self.assertEqual(result.returncode, 0, result.stderr[-1500:])
            self.assertNotIn("ThreadSanitizer", result.stderr)

    # ---- fonts ----

    def pack_with_fonts(self, name, fonts):
        """A pack folder holding just these fonts: {slot: bytes}."""
        fonts_dir = os.path.join(self.root, "ext", "asset_packs", name, "Fonts")
        os.makedirs(fonts_dir)
        for slot, data in fonts.items():
            with open(os.path.join(fonts_dir, f"{slot}.u8f"), "wb") as f:
                f.write(data)

    def font_report(self, name):
        result = self.run_host("fonts", name)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr[-1500:])
        self.assertNotIn("FAIL", result.stdout)
        # three reports of one row per slot: loaded, after an icon unload/reload, after dropping
        rows = [line.split()[1:] for line in result.stdout.splitlines() if line.startswith("F ")]
        count = len(pack.STOCK_FONTS)
        reports = [rows[i * count : (i + 1) * count] for i in range(3)]
        return result, reports

    def test_pack_fonts_replace_exactly_the_slots_the_pack_has(self):
        stock = self.stock_fonts
        self.pack_with_fonts(
            "Two",
            {"Secondary": variant_font(stock["Secondary"], 1), "BigNumbers": variant_font(stock["BigNumbers"], 2)},
        )
        result, (loaded, reloaded, dropped) = self.font_report("Two")
        for report in (loaded, reloaded):
            self.assertEqual(
                [(row[1], row[2] if len(row) > 2 else None) for row in report],
                [("S", None), ("R", "same"), ("S", None), ("R", "same"), ("S", None)],
            )
        # dropping the pack puts every built-in font back
        self.assertTrue(all(row[1] == "S" for row in dropped), dropped)
        self.assertIn("after reload 2", result.stdout)
        self.assertIn("after drop 0 buried 2", result.stdout)
        # ... and the replaced fonts are freed once the grace period is over
        self.assertIn("later buried 0", result.stdout)

    def test_every_slot_and_the_real_pack_fonts(self):
        result, (loaded, _, _) = self.font_report("Real")
        self.assertEqual(
            [(row[1], row[2]) for row in loaded], [("R", "same")] * len(pack.STOCK_FONTS)
        )

    def test_unsafe_or_mismatched_fonts_are_ignored(self):
        stock = self.stock_fonts
        good = variant_font(stock["Secondary"], 3)
        self.pack_with_fonts(
            "Bad",
            {
                # the wrong font for the slot: its height would change every layout
                "Primary": stock["Secondary"],
                # valid font, but cut short
                "Secondary": good[: len(good) - 9],
                # a glyph that wants more data than it has
                "Keyboard": overrun_font(stock["Keyboard"]),
                # nothing at all
                "BigNumbers": b"",
                # not a font
                "BatteryPercent": bytes(range(256)) * 3,
            },
        )
        result, (loaded, _, _) = self.font_report("Bad")
        self.assertTrue(all(row[1] == "S" for row in loaded), loaded)
        self.assertIn("replaced 0", result.stdout)

    def test_fonts_over_the_ram_budget_are_ignored(self):
        stock = self.stock_fonts
        big = {s: pad_font(stock[s], 6000) for s in ("Primary", "Secondary", "Keyboard")}
        for slot, data in big.items():
            self.assertIsNone(pack.font_problem(data, pack.font_info(stock[slot])), slot)
            self.assertGreater(len(data), 5900)
        self.pack_with_fonts("Big", big)
        result, (loaded, _, _) = self.font_report("Big")
        # two fit (12000 of 12288 B); the third would not, and the slot keeps its built-in font
        self.assertEqual([row[1] for row in loaded], ["R", "R", "S", "S", "S"], loaded)

    def test_a_font_over_the_size_limit_is_ignored(self):
        stock = self.stock_fonts
        self.pack_with_fonts("Huge", {"Primary": pad_font(stock["Primary"], 6600)})
        result, (loaded, _, _) = self.font_report("Huge")
        self.assertTrue(all(row[1] == "S" for row in loaded), loaded)

    def test_replaced_fonts_are_freed_late_not_at_once(self):
        # With no grace period a draw holding the old pointer reads freed memory: AddressSanitizer
        # must see that, which shows the stress test below really exercises the graveyard.
        cls = type(self)
        immediate = cls.build("host_nograce", "address,undefined", grace_ms=0)
        failed = False
        for _ in range(3):
            result = self.run_host("stress", "Real", 2, exe=immediate)
            if result.returncode != 0 and "AddressSanitizer" in result.stderr:
                failed = True
                break
        self.assertTrue(failed, "a font freed with no grace period was not noticed")

    # ---- fuzzing: whatever is on the card, loading must never crash the firmware ----

    def mutate_pack(self, pack_dir, rng):
        def all_files():
            return sorted(os.path.join(d, f) for d, _, fs in os.walk(pack_dir) for f in fs)

        for _ in range(rng.randint(1, 6)):
            files = all_files()
            if not files:
                return
            path, kind = rng.choice(files), rng.randrange(9)
            with open(path, "rb") as f:
                data = f.read()
            if kind == 0 and data:
                b = bytearray(data)
                for _ in range(rng.randint(1, 4)):
                    b[rng.randrange(len(b))] ^= 1 << rng.randrange(8)
                data = bytes(b)
            elif kind == 1 and data:
                data = data[: rng.randrange(len(data))]
            elif kind == 2:
                data = data + bytes(rng.randrange(256) for _ in range(rng.randint(1, 200)))
            elif kind == 3:
                data = b""
            elif kind == 4:
                data = bytes(rng.randrange(256) for _ in range(rng.randint(1100, 5000)))
            elif kind == 6:
                data = bytes(rng.randrange(256) for _ in range(len(data)))
            elif kind == 5:
                os.remove(path)
                continue
            elif kind == 7:
                os.remove(path)
                os.makedirs(path)
                continue
            else:
                deep = os.path.join(os.path.dirname(path), *["x" * rng.randint(1, 40)] * rng.randint(1, 6))
                os.makedirs(deep, exist_ok=True)
                path = os.path.join(deep, "y" * rng.randint(1, 150) + ".bmx")
            with open(path, "wb") as f:
                f.write(data)

    def fuzz(self, rounds, seed, exe=None, stop_at_first=False):
        failures = []
        for r in range(rounds):
            shutil.rmtree(self.root, True)
            shutil.copytree(self.template, self.root)
            os.makedirs(os.path.join(self.root, "int"), exist_ok=True)
            self.mutate_pack(os.path.join(self.root, "ext", "asset_packs", "Real"), random.Random(seed + r))
            result = self.run_host("verify", "Real", exe=exe)
            if result.returncode != 0:
                failures.append((seed + r, result.stderr[:300]))
                if stop_at_first:
                    break
        return failures

    def test_fuzz_corrupted_packs_never_crash_the_loader(self):
        # Every icon the loader accepts is then drawn with the real decoder, so an invalid frame
        # that got through would abort the run.
        self.assertEqual(self.fuzz(150, 1000), [])

    def test_fuzz_has_teeth_a_validator_without_the_decode_check_is_caught(self):
        with open(os.path.join(REPO_ROOT, "lib", "asset_packs", "asset_pack_format.c")) as f:
            source = f.read()
        weak = source.replace(
            "if(!decode(blob, size, scratch, ASSET_PACK_DECODE_BUFFER_SIZE, &produced, decode_context)) {",
            "if(0) {",
        ).replace("return produced == raw ? AssetPackOk : AssetPackErrSize;", "return AssetPackOk;")
        self.assertNotEqual(weak, source)
        weak_path = self.p("weak_format.c")
        with open(weak_path, "w") as f:
            f.write(weak)
        cls = type(self)
        exe = cls.build("host_weak", "address,undefined", source_format=weak_path)
        failures = self.fuzz(400, 1000, exe=exe, stop_at_first=True)
        self.assertTrue(failures, "the fuzz did not notice a validator that skips decoding")
        self.assertIn("furi_check failed", failures[0][1])


class FontHostTests(TempDirCase):
    """asset_pack_font_check() against the real u8g2 text renderer under AddressSanitizer.

    u8g2 trusts a font completely and reads wherever it says, so the only proof that a font the
    check accepts is safe is to draw with it, in a heap block of exactly the font's size."""

    U8G2_SOURCES = [
        "u8g2_font", "u8g2_setup", "u8g2_buffer", "u8g2_hvline", "u8g2_ll_hvline",
        "u8g2_intersection", "u8g2_d_memory", "u8g2_bitmap", "u8g2_box", "u8g2_circle",
        "u8g2_line", "u8x8_setup", "u8x8_8x8", "u8x8_byte", "u8x8_cad", "u8x8_display", "u8x8_gpio",
    ]

    @classmethod
    def build(cls, name, source_format=None):
        import subprocess

        u8g2 = os.path.join(REPO_ROOT, "lib", "u8g2")
        exe = os.path.join(cls.build_dir, name)
        subprocess.check_call(
            [
                "gcc", "-std=gnu11", "-g", "-O1", "-ffunction-sections", "-fdata-sections",
                "-Wl,--gc-sections", "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                "-I", u8g2, "-I", os.path.join(REPO_ROOT, "lib", "asset_packs"),
                "-o", exe,
                os.path.join(REPO_ROOT, "scripts", "host_tests", "font_host.c"),
                os.path.join(cls.build_dir, "stock_fonts.c"),
                source_format or os.path.join(REPO_ROOT, "lib", "asset_packs", "asset_pack_format.c"),
                *[os.path.join(u8g2, f"{n}.c") for n in cls.U8G2_SOURCES],
            ],
            stderr=subprocess.DEVNULL,
        )
        return exe

    @classmethod
    def setUpClass(cls):
        if not _have_sanitizer("address,undefined"):
            raise unittest.SkipTest("gcc with AddressSanitizer is not available")
        cls.build_dir = tempfile.mkdtemp(prefix="apf_")
        write_stock_fonts_c(os.path.join(cls.build_dir, "stock_fonts.c"))
        cls.exe = cls.build("font_asan")
        cls.stock = stock_font_bytes()
        cls.slots = list(pack.STOCK_FONTS)

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.build_dir, True)

    def run_font(self, *args, exe=None, timeout=120):
        import subprocess

        return subprocess.run(
            [exe or self.exe, *map(str, args)], capture_output=True, text=True, timeout=timeout
        )

    def write(self, name, data):
        path = self.p(name)
        with open(path, "wb") as f:
            f.write(data)
        return path

    def c_status(self, data, slot, exe=None):
        result = self.run_font("check", self.write("f.u8f", data), slot, exe=exe)
        self.assertEqual(result.returncode, 0, result.stderr[-1500:])
        return int(result.stdout.split()[1])

    def test_stock_fonts_are_accepted_for_their_own_slot_and_draw_something(self):
        for i, slot in enumerate(self.slots):
            self.assertEqual(self.c_status(self.stock[slot], i), 0, slot)
            result = self.run_font("draw", self.write("s.u8f", self.stock[slot]))
            self.assertEqual(result.returncode, 0, result.stderr[-1500:])
            lit = int(result.stdout.split()[1])
            self.assertGreater(lit, 500, f"{slot} drew almost nothing: the test would be vacuous")

    def test_a_font_with_other_metrics_is_refused_for_the_slot(self):
        metrics = {s: tuple(pack.font_info(self.stock[s]).values())[:3] for s in self.slots}
        refused = 0
        for i, slot in enumerate(self.slots):
            for other in self.slots:
                if other != slot and metrics[other] != metrics[slot]:
                    self.assertEqual(self.c_status(self.stock[other], i), 7, f"{other} in {slot}")
                    refused += 1
        self.assertGreater(refused, 8)

    def test_the_overrun_font_is_refused_by_both_checks(self):
        font = overrun_font(self.stock["Secondary"])
        info = pack.font_info(self.stock["Secondary"])
        self.assertIn("past its own data", pack.font_problem(font, info))
        self.assertEqual(self.c_status(font, self.slots.index("Secondary")), 5)

    def test_python_and_c_agree_on_damaged_fonts(self):
        import struct

        for i, slot in enumerate(self.slots):
            info = pack.font_info(self.stock[slot])
            rng = random.Random(500 + i)
            fonts = [mutate_font(rng, self.stock[slot]) for _ in range(1500)]
            fonts += [overrun_font(self.stock[slot]), self.stock[slot], b"", b"\0" * 40]
            batch = self.write("batch.bin", b"".join(struct.pack("<I", len(f)) + f for f in fonts))
            result = self.run_font("batch", i, batch)
            self.assertEqual(result.returncode, 0, result.stderr[-1500:])
            statuses = [int(x) for x in result.stdout.split()]
            self.assertEqual(len(statuses), len(fonts))
            accepted = 0
            for font, status in zip(fonts, statuses):
                python_ok = pack.font_problem(font, info) is None
                self.assertEqual(python_ok, status == 0, f"{slot}: {font.hex()}")
                accepted += status == 0
            self.assertGreater(accepted, 100, "too few accepted fonts for the comparison to mean much")
            self.assertLess(accepted, len(fonts) - 100)

    def fuzz(self, slot, rounds, seed, exe=None):
        result = self.run_font("fuzz", slot, rounds, seed, exe=exe, timeout=600)
        return result

    def test_every_accepted_mutation_of_every_stock_font_draws_inside_its_bounds(self):
        for slot in range(len(self.slots)):
            result = self.fuzz(slot, 4000, 11 + slot)
            self.assertEqual(result.returncode, 0, f"{self.slots[slot]}: {result.stdout}{result.stderr[-1500:]}")
            fields = result.stdout.split()
            accepted, changed = int(fields[3]), int(fields[5].rstrip(")"))
            self.assertGreater(accepted, 300, result.stdout)
            self.assertGreater(changed, 200, "the accepted fonts were mostly unchanged")

    def weak_build(self, name, old, new):
        with open(os.path.join(REPO_ROOT, "lib", "asset_packs", "asset_pack_format.c")) as f:
            source = f.read()
        weak = source.replace(old, new)
        self.assertNotEqual(weak, source)
        path = self.p(f"{name}.c")
        with open(path, "w") as f:
            f.write(weak)
        return type(self).build(name, source_format=path)

    def test_teeth_a_check_that_does_not_decode_glyphs_lets_the_overrun_through(self):
        exe = self.weak_build(
            "weak_glyphs",
            "if(!glyph_decodes_in_bounds(font, font + position + 2, entry_size - 2)) {",
            "if(0) {",
        )
        font = overrun_font(self.stock["Secondary"])
        slot = self.slots.index("Secondary")
        self.assertEqual(self.c_status(font, slot, exe=exe), 0, "the weakened check should accept it")
        result = self.run_font("draw", self.write("o.u8f", font), exe=exe)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("AddressSanitizer: heap-buffer-overflow", result.stderr)

    def test_teeth_the_fuzz_catches_a_check_that_trusts_the_glyph_chain(self):
        # Accept any glyph entry size: u8g2 is then sent past the end of the font
        exe = self.weak_build(
            "weak_chain",
            "if(entry_size < U8G2_MIN_GLYPH_ENTRY || position + entry_size > size) {",
            "if(0) {",
        )
        caught = False
        for seed in range(1, 6):
            result = self.fuzz(self.slots.index("Secondary"), 4000, seed, exe=exe)
            if result.returncode != 0:
                caught = True
                break
        self.assertTrue(caught, "the fuzz did not notice a check that ignores the glyph chain")


class SettingsMigrationTests(TempDirCase):
    """applications/services/desktop/desktop_settings.c: every older settings file must still
    load, with the asset pack setting empty, and a damaged one must fall back to defaults."""

    @classmethod
    def setUpClass(cls):
        import subprocess

        if not _have_sanitizer("address,undefined"):
            raise unittest.SkipTest("gcc with AddressSanitizer is not available")
        cls.build_dir = tempfile.mkdtemp(prefix="aps_")
        cls.exe = os.path.join(cls.build_dir, "settings_host")
        host = os.path.join(REPO_ROOT, "scripts", "host_tests")
        subprocess.check_call(
            [
                "gcc", "-std=gnu11", "-g", "-O1", "-fsanitize=address,undefined", "-Wall", "-Wextra",
                "-I", os.path.join(host, "stubs"),
                "-I", os.path.join(REPO_ROOT, "applications", "services"),
                "-I", os.path.join(REPO_ROOT, "applications", "services", "storage"),
                "-I", os.path.join(REPO_ROOT, "lib", "toolbox"),
                "-I", os.path.join(REPO_ROOT, "furi"),
                "-I", os.path.join(REPO_ROOT, "lib"),
                "-I", REPO_ROOT,
                "-o", cls.exe,
                os.path.join(host, "settings_host.c"),
                os.path.join(REPO_ROOT, "applications", "services", "desktop", "desktop_settings.c"),
                os.path.join(REPO_ROOT, "lib", "toolbox", "saved_struct.c"),
                os.path.join(host, "stubs", "storage_host.c"),
                "-lpthread",
            ],
            stderr=subprocess.DEVNULL,
        )

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.build_dir, True)

    def migrate(self, version, variant=""):
        import subprocess

        root = self.p("root")
        os.makedirs(os.path.join(root, "int"), exist_ok=True)
        r = subprocess.run(
            [self.exe, "migrate", root, str(version), variant],
            capture_output=True, text=True, errors="replace",
        )
        self.assertEqual(r.returncode, 0, r.stderr[-1500:])
        out = {}
        for line in r.stdout.splitlines():
            key, _, value = line.partition(" ")
            out[key] = value
        with open(os.path.join(root, "int", ".desktop.settings"), "rb") as f:
            out["file_version"] = f.read(2)[1]
        return out

    def assertKept(self, out):
        self.assertEqual(out["auto_lock"], "90000")
        self.assertEqual(out["battery"], "3")
        self.assertEqual(out["clock"], "1")
        self.assertEqual(out["favorite0"], "/ext/apps/Tools/example.fap")
        self.assertEqual(out["dummy_last"], "Snake Game")

    def test_v14_file_loads_with_no_pack(self):
        out = self.migrate(14)
        self.assertKept(out)
        self.assertEqual((out["usb"], out["menu_style_len"], out["pack_len"]), ("0", "0", "0"))

    def test_v17_file_loads_with_no_pack(self):
        out = self.migrate(17)
        self.assertKept(out)
        self.assertEqual((out["usb"], out["menu_style_len"], out["pack_len"]), ("1", "0", "0"))

    def test_v18_file_keeps_everything_and_gains_an_empty_pack(self):
        out = self.migrate(18)
        self.assertKept(out)
        self.assertEqual(out["menu_style"], "Test.fal")
        self.assertEqual(out["pack_len"], "0")

    def test_v19_file_round_trips_the_pack(self):
        out = self.migrate(19)
        self.assertKept(out)
        self.assertEqual((out["menu_style"], out["pack"]), ("Test.fal", "Hadal"))
        self.assertEqual(out["file_version"], 19)

    def test_strings_without_a_terminator_are_cut_to_fit(self):
        out = self.migrate(19, "unterminated")
        self.assertEqual((out["menu_style_len"], out["pack_len"]), ("31", "31"))

    def test_a_damaged_file_gives_defaults_and_is_rewritten_as_v19(self):
        out = self.migrate(19, "badsum")
        self.assertEqual((out["auto_lock"], out["pack_len"], out["menu_style_len"]), ("0", "0", "0"))
        self.assertEqual(out["file_version"], 19)


class IconIndexTests(TempDirCase):
    ICONS = [
        {"path": "StatusBar/Battery_26x8", "symbol": "I_Battery_26x8", "width": 26, "height": 8, "frames": 1, "animated": False},
        {"path": "MainMenu/NFC_14", "symbol": "A_NFC_14", "width": 14, "height": 14, "frames": 4, "animated": True},
        {"path": "BLE/BLE_HID/Ok_btn_9x9", "symbol": "I_Ok_btn_9x9", "width": 9, "height": 9, "frames": 1, "animated": False},
    ]

    def test_parse_list(self):
        text = "# comment\n\nStatusBar/Battery_26x8\n  MainMenu/NFC_14  \n"
        self.assertEqual(pack.parse_icon_list(text), ["StatusBar/Battery_26x8", "MainMenu/NFC_14"])
        with self.assertRaises(pack.PackError):
            pack.parse_icon_list("A/b\nA/b\n")  # duplicate
        with self.assertRaises(pack.PackError):
            pack.parse_icon_list("# nothing\n\n")  # empty

    def test_unknown_listed_icon_fails_loudly(self):
        with self.assertRaises(pack.PackError) as ctx:
            pack.generate_icon_index(self.ICONS, ["StatusBar/Renamed_26x8"])
        self.assertIn("update assets/icon_override_list.txt", str(ctx.exception))

    def test_generated_c_compiles_sorted_and_searchable(self):
        paths = [i["path"] for i in self.ICONS]
        header, source = pack.generate_icon_index(self.ICONS, paths)
        os.makedirs(self.p("gui"))
        # minimal stand-ins for the firmware headers the generated code includes
        with open(self.p("gui", "icon.h"), "w") as f:
            f.write("#pragma once\ntypedef struct Icon { int id; } Icon;\n")
        with open(self.p("assets_icons.h"), "w") as f:
            f.write("#pragma once\n#include <gui/icon.h>\n"
                    "extern const Icon I_Battery_26x8, A_NFC_14, I_Ok_btn_9x9;\n")
        with open(self.p("assets_icon_index.h"), "w") as f:
            f.write(header)
        with open(self.p("assets_icon_index.c"), "w") as f:
            f.write(source)
        with open(self.p("main.c"), "w") as f:
            f.write(r'''
#include <stdio.h>
#include "assets_icon_index.h"
#include "assets_icons.h"
const Icon I_Battery_26x8 = {1}, A_NFC_14 = {2}, I_Ok_btn_9x9 = {3};
static const Icon* find(uint32_t h) {
    size_t lo = 0, hi = assets_icon_index_count;
    while(lo < hi) {
        size_t mid = (lo + hi) / 2;
        if(assets_icon_index[mid].path_hash == h) return assets_icon_index[mid].icon;
        if(assets_icon_index[mid].path_hash < h) lo = mid + 1; else hi = mid;
    }
    return 0;
}
int main(int argc, char** argv) {
    for(size_t i = 1; i < assets_icon_index_count; i++)
        if(assets_icon_index[i - 1].path_hash >= assets_icon_index[i].path_hash) return 10;
    /* argv: hash id pairs */
    for(int i = 1; i + 1 < argc; i += 2) {
        const Icon* icon = find((uint32_t)strtoul(argv[i], 0, 16));
        if(!icon || icon->id != atoi(argv[i + 1])) return 20 + i;
    }
    if(find(0x12345678u)) return 30;
    printf("%zu\n", assets_icon_index_count);
    return 0;
}
'''.replace("#include <stdio.h>", "#include <stdio.h>\n#include <stdlib.h>"))
        import subprocess
        exe = self.p("t")
        subprocess.check_call(["gcc", "-Wall", "-Werror", "-I", self.tmp, "-o", exe,
                               self.p("main.c"), self.p("assets_icon_index.c")])
        args = []
        for icon, ident in zip(paths, (1, 2, 3)):
            args += ["%08x" % pack.icon_path_hash(icon), str(ident)]
        out = subprocess.check_output([exe] + args, text=True)
        self.assertEqual(out.strip(), "3")


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
