#!/usr/bin/env python3
"""Host tests for the asset pack format. Run: python3 -m unittest scripts/asset_packer_test.py
(from the repo root). Needs Pillow and heatshrink2."""

import os
import random
import re
import shutil
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from PIL import Image  # noqa: E402

from flipper.assets import pack  # noqa: E402
from flipper.assets.dolphin import frames_file_pack, frames_file_unpack  # noqa: E402

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HADAL_PACK_DIR = os.path.join(
    REPO_ROOT, "applications", "main", "hadal_resources", "resources", "asset_packs", "Hadal"
)
HADAL_ART_DIR = os.path.join(REPO_ROOT, "assets", "hadal_pack")


def u8g2font_serialize(font):
    from flipper.assets import u8g2font

    return u8g2font.serialize_font(font)


def anim_frame_blobs(folder):
    """An animation's frame blobs: from frames.bin, or frame_<n>.bm files when it has none"""
    frames_file = os.path.join(folder, "frames.bin")
    if os.path.isfile(frames_file):
        with open(frames_file, "rb") as f:
            return frames_file_unpack(f.read())
    names = sorted(
        (f for f in os.listdir(folder) if f.startswith("frame_") and f.endswith(".bm")),
        key=lambda n: int(n[6:-3]),
    )
    blobs = []
    for name in names:
        with open(os.path.join(folder, name), "rb") as f:
            blobs.append(f.read())
    return blobs


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
        # another frame count is refused; another size is drawn at its own size, with a warning
        self.assertEqual(len(report["errors"]), 1)
        self.assertIn("MainMenu/NFC_14", report["errors"][0])
        self.assertTrue(any("Battery_26x8" in w for w in report["warnings"]))

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

        # a little smaller than another slot's font: taken (packs made for Momentum do this)
        shutil.copy(os.path.join(out, "Fonts", "Secondary.u8f"), os.path.join(out, "Fonts", "Primary.u8f"))
        report = pack.validate_pack(out, None, REPO_ROOT)
        self.assertEqual(report["errors"], [])
        # the big digits in place of the interface font: far too tall, refused
        big_src = self.p("big", "Fonts")
        os.makedirs(big_src)
        with open(os.path.join(big_src, "Primary.c"), "w") as f:
            f.write(pack.find_u8g2_font_source(text, pack.STOCK_FONTS["BigNumbers"]))
        big_out = self.p("big_pack")
        pack.pack_tree(self.p("big"), big_out)
        report = pack.validate_pack(big_out, None, REPO_ROOT)
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
        self.assertLessEqual(loaded, len(self.index_paths))
        self.assertEqual(set(replaced.values()), {"same"})
        used = int(result.stderr.split(" icons, ")[1].split(" B")[0])
        self.assertLessEqual(used, pack.ICON_RAM_BUDGET)

    def test_other_size_icons_are_taken_unknown_and_wrong_frame_count_ignored(self):
        icons = os.path.join(self.root, "ext", "asset_packs", "Odd", "Icons")
        os.makedirs(os.path.join(icons, "StatusBar"))
        os.makedirs(os.path.join(icons, "Nowhere"))
        make_png(self.p("a.png"), 27, 8, 1)   # Battery_26x8 is 26 wide: drawn at 27
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
        self.assertEqual(
            set(replaced),
            {pack.icon_path_hash("StatusBar/Muted_8x8"), pack.icon_path_hash("StatusBar/Battery_26x8")},
        )
        self.assertEqual(loaded, 2)

    def write_icon(self, pack_name, path, width, height, seed, frames=1):
        icons = os.path.join(self.root, "ext", "asset_packs", pack_name, "Icons")
        if frames == 1:
            os.makedirs(os.path.dirname(os.path.join(icons, path)), exist_ok=True)
            make_png(self.p("i.png"), width, height, seed)
            w, h, blob = pack.encode_png(self.p("i.png"))
            with open(os.path.join(icons, path + ".bmx"), "wb") as f:
                f.write(pack.bmx_pack(w, h, blob))
            return
        anim = os.path.join(icons, path)
        os.makedirs(anim)
        for i in range(frames):
            make_png(self.p("f.png"), width, height, seed + i)
            _, _, blob = pack.encode_png(self.p("f.png"))
            with open(os.path.join(anim, pack.FRAME_NAME % i), "wb") as f:
                f.write(blob)
        with open(os.path.join(anim, "meta"), "wb") as f:
            f.write(pack.anim_meta_pack(width, height, 4, frames))

    def test_apps_on_the_sd_card_get_the_pack_icons_named_like_theirs(self):
        self.write_icon("Apps", "Passport/passport_okay_46x49", 46, 49, 1)
        self.write_icon("Apps", "Passport/passport_128x64", 128, 64, 2)
        self.write_icon("Apps", "NFC/Custom_10x10", 12, 10, 3)       # another size is fine
        self.write_icon("Apps", "MainMenu/Foo_10x10", 10, 10, 4)     # module icons stay stock
        self.write_icon("Apps", "Anim/Spin_8x8", 8, 8, 5, frames=3)
        result = self.run_host(
            "app", "Apps",
            "I_passport_okay1_46x49", "I_passport_okay3_46x49",  # one portrait for every level
            "I_passport_bad1_46x49",                              # the pack has none
            "I_passport_128x64", "I_Custom_10x10", "I_Foo_10x10",
            "A_Spin_8x8:3", "I_Spin_8x8",                         # frame counts must match
            "X_Custom_10x10",                                     # not an icon's name
        )
        self.assertEqual(result.returncode, 0, result.stderr[-1500:])
        lines = result.stdout.splitlines()
        run = {l.split()[1]: l.split()[2:] for l in lines if l.startswith("run ")}
        self.assertEqual(run["I_passport_okay1_46x49"], ["R", "46x49", "1"])
        self.assertEqual(run["I_passport_okay3_46x49"], ["R", "46x49", "1"])
        self.assertEqual(run["I_passport_bad1_46x49"], ["S"])
        self.assertEqual(run["I_passport_128x64"], ["R", "128x64", "1"])
        self.assertEqual(run["I_Custom_10x10"], ["R", "12x10", "1"])
        self.assertEqual(run["I_Foo_10x10"], ["S"])
        self.assertEqual(run["A_Spin_8x8"], ["R", "8x8", "3"])
        self.assertEqual(run["I_Spin_8x8"], ["S"])
        self.assertEqual(run["X_Custom_10x10"], ["S"])
        # once the app is gone, or the pack's icons are, nothing of it is replaced (and ASan saw
        # no use of freed icons)
        for when in ("gone", "dropped"):
            self.assertEqual({l.split()[2] for l in lines if l.startswith(when + " ")}, {"S"}, when)

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
                # far too tall for the slot: it would not fit the layouts
                "Primary": stock["BigNumbers"],
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

    # ---- the shipped pack ----

    def install_hadal_pack(self):
        shutil.copytree(HADAL_PACK_DIR, os.path.join(self.root, "ext", "asset_packs", "Hadal"))

    def test_the_shipped_hadal_pack_loads_completely_and_draws_identically(self):
        self.install_hadal_pack()
        result = self.run_host("verify", "Hadal")
        self.assertEqual(result.returncode, 0, result.stderr[-1500:])
        loaded, replaced = self.parse_verify(result)
        packed = {pack.icon_path_hash(p) for p, *_ in pack.iter_pack_icons(HADAL_PACK_DIR)}
        # every icon in the pack is one the firmware keeps, and the real loader took all of them
        self.assertEqual(loaded, len(packed))
        self.assertEqual(set(replaced), packed)
        self.assertTrue(all(v == "same" or v == "diff" for v in replaced.values()))
        # ... and they really are different from the built-in ones (the pack is not a no-op)
        self.assertEqual(sum(v == "diff" for v in replaced.values()), len(packed))

    def test_the_shipped_hadal_pack_keeps_every_stock_font(self):
        self.install_hadal_pack()
        result, (loaded, _, _) = self.font_report("Hadal")
        self.assertEqual([row[1] for row in loaded], ["S"] * len(pack.STOCK_FONTS), loaded)

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

    def test_the_title_font_is_safe_for_u8g2_and_draws(self):
        # Hadal's title font is built in (applications/services/gui/hadal_title_font.c); the
        # pack check walks it as u8g2 would, so a bad glyph would show up here
        sys.path.insert(0, os.path.join(REPO_ROOT, "assets", "hadal_ui"))
        import title_font

        data = u8g2font_serialize(title_font.build_font())
        self.assertIsNone(pack.font_problem(data, None))
        result = self.run_font("draw", self.write("title.u8f", data))
        self.assertEqual(result.returncode, 0, result.stderr[-1500:])
        self.assertGreater(int(result.stdout.split()[1]), 300, "the title font drew almost nothing")

    def test_re_encoded_stock_fonts_draw_exactly_like_the_originals(self):
        from flipper.assets import u8g2font

        for slot in self.slots:
            original = self.run_font("draw", self.write("a.u8f", self.stock[slot]))
            again = u8g2font.serialize_font(u8g2font.parse_font(self.stock[slot]))
            copy = self.run_font("draw", self.write("b.u8f", again))
            self.assertEqual(copy.returncode, 0, copy.stderr[-1500:])
            self.assertEqual(copy.stdout, original.stdout, f"{slot}: the codec changed what is drawn")

    def test_fonts_far_from_the_slot_metrics_are_refused_near_ones_taken(self):
        # Packs made for Momentum carry fonts a little smaller or larger than the stock ones;
        # within ASSET_PACK_FONT_SLACK pixels they are taken, beyond it refused, the same in C
        # (asset_pack_font_check) as in Python (pack.font_problem)
        refused = taken = 0
        for i, slot in enumerate(self.slots):
            expected = pack.font_info(self.stock[slot])
            for other in self.slots:
                python_ok = pack.font_problem(self.stock[other], expected) is None
                self.assertEqual(
                    self.c_status(self.stock[other], i), 0 if python_ok else 7, f"{other} in {slot}"
                )
                refused += not python_ok
                taken += python_ok and other != slot
        self.assertGreater(refused, 4)
        self.assertGreater(taken, 2)

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


class U8g2FontCodecTests(unittest.TestCase):
    """scripts/flipper/assets/u8g2font.py: decoding and encoding u8g2 fonts."""

    def test_every_stock_font_survives_a_round_trip(self):
        from flipper.assets import u8g2font

        for slot, data in stock_font_bytes().items():
            font = u8g2font.parse_font(data)
            again = u8g2font.parse_font(u8g2font.serialize_font(font))
            self.assertEqual(sorted(font.glyphs), sorted(again.glyphs), slot)
            for encoding in font.glyphs:
                self.assertEqual(font.glyphs[encoding], again.glyphs[encoding], f"{slot} {encoding}")

    def test_re_encoded_fonts_pass_the_firmware_check(self):
        from flipper.assets import u8g2font

        for slot, data in stock_font_bytes().items():
            new = u8g2font.serialize_font(u8g2font.parse_font(data))
            self.assertIsNone(pack.font_problem(new, pack.font_info(data)), slot)
            self.assertLessEqual(len(new), len(data) + 8, f"{slot} should not get bigger")

    def test_a_glyph_is_what_it_was_drawn_as(self):
        from flipper.assets import u8g2font

        g = u8g2font.Glyph(65, [[0, 1, 1, 0], [1, 0, 0, 1], [1, 1, 1, 1], [1, 0, 0, 1]], 0, 0, 5)
        base = stock_font_bytes()["Secondary"]
        font = u8g2font.Font(base[:23], {65: g, 32: u8g2font.Glyph(32, [], 0, 0, 3)})
        again = u8g2font.parse_font(u8g2font.serialize_font(font))
        self.assertEqual(again.glyphs[65].rows(), [".##.", "#..#", "####", "#..#"])
        self.assertEqual(again.glyphs[65].advance, 5)
        self.assertEqual(again.glyphs[32].width, 0)

    def test_long_runs_are_split_to_fit(self):
        from flipper.assets import u8g2font

        # 60 lit pixels in a row cannot be one run when runs hold at most 7: the encoder splits
        g = u8g2font.Glyph(66, [[1] * 60, [0] * 60, [1] * 60], 0, 0, 61)
        base = bytearray(stock_font_bytes()["Secondary"][:23])
        font = u8g2font.Font(bytes(base), {66: g})
        again = u8g2font.parse_font(u8g2font.serialize_font(font))
        self.assertEqual(again.glyphs[66], g)

    def test_text_is_drawn_with_the_glyph_boxes(self):
        from flipper.assets import u8g2font
        from PIL import Image

        font = u8g2font.parse_font(stock_font_bytes()["Secondary"])
        image = Image.new("1", (40, 12), 1)
        end = font.draw_text(image, 1, 9, "Hi", lit=0)
        self.assertEqual(end, 1 + font.text_width("Hi"))
        self.assertTrue(any(image.getpixel((x, y)) == 0 for x in range(40) for y in range(12)))


class HadalPackTests(TempDirCase):
    """The generated Hadal pack: what is committed is what the scripts produce, it is complete,
    safe to load, and keeps every size the firmware lays out around."""

    @classmethod
    def setUpClass(cls):
        sys.path.insert(0, HADAL_ART_DIR)
        import build as hadal_build  # noqa: E402

        cls.build = hadal_build
        cls.out = tempfile.mkdtemp(prefix="hadal_")
        cls.fresh = os.path.join(cls.out, "Hadal")
        hadal_build.build(cls.fresh)

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.out, True)
        if HADAL_ART_DIR in sys.path:
            sys.path.remove(HADAL_ART_DIR)

    def decoded_icons(self, root):
        return {
            path: (w, h, [pack.decode_blob(b, w, h) for b in blobs], rate)
            for path, w, h, blobs, rate in pack.iter_pack_icons(root)
        }

    def decoded_anims(self, root):
        out = {}
        anims = os.path.join(root, "Anims")
        for name in sorted(os.listdir(anims)):
            folder = os.path.join(anims, name)
            if not os.path.isdir(folder):
                continue
            keys = pack._read_keys(os.path.join(folder, "meta.txt"))
            w, h = int(keys["Width"]), int(keys["Height"])
            out[name] = [pack.decode_blob(b, w, h) for b in anim_frame_blobs(folder)]
        return out

    def plain_files(self, root):
        """The files that are not compressed bitmaps, by relative path: metadata and fonts."""
        found = {}
        for d, _, files in os.walk(root):
            for f in files:
                if f.endswith((".bm", ".bmx")):
                    continue
                rel = os.path.relpath(os.path.join(d, f), root)
                with open(os.path.join(d, f), "rb") as fh:
                    found[rel] = fh.read()
        return found

    def test_the_committed_pack_is_what_the_scripts_generate(self):
        # Compressed frames are compared by what they decode to, so another heatshrink version
        # that packs the same pixels differently does not fail this
        self.assertEqual(self.decoded_icons(HADAL_PACK_DIR), self.decoded_icons(self.fresh))
        self.assertEqual(self.decoded_anims(HADAL_PACK_DIR), self.decoded_anims(self.fresh))
        self.assertEqual(self.plain_files(HADAL_PACK_DIR), self.plain_files(self.fresh))

    def test_the_committed_slideshow_frames_are_what_the_scripts_generate(self):
        from PIL import Image

        first, update = self.build.slideshow_frames()
        for folder, frames in (("first_start", first), ("update_default", update)):
            directory = os.path.join(REPO_ROOT, "assets", "slideshow", folder)
            names = sorted(os.listdir(directory))
            self.assertEqual(names, [f"frame_{i:02d}.png" for i in range(len(frames))], folder)
            for name, frame in zip(names, frames):
                with Image.open(os.path.join(directory, name)) as im:
                    self.assertEqual(im.mode, "1", name)
                    self.assertEqual(im.size, (128, 64), name)
                    same = im.convert("L").tobytes() == frame.image.convert("L").tobytes()
                    self.assertTrue(same, f"{folder}/{name} differs from what the scripts generate")

    def test_the_pack_validates_clean(self):
        index = pack.source_icon_index(os.path.join(REPO_ROOT, "assets", "icons"))
        report = pack.validate_pack(HADAL_PACK_DIR, index, REPO_ROOT)
        self.assertEqual(report["errors"], [])
        self.assertEqual(report["warnings"], [])
        self.assertEqual(report["stats"]["anims"], 4)
        # the loading spinner only: module and status bar icons stay stock with this pack
        self.assertEqual(report["stats"]["icons"], 1)
        self.assertEqual(report["stats"]["font_bytes"], 0)

    def test_every_icon_is_one_the_firmware_draws_at_the_size_it_has(self):
        with open(os.path.join(REPO_ROOT, "assets", "icon_override_list.txt")) as f:
            replaceable = set(pack.parse_icon_list(f.read()))
        index = pack.source_icon_index(os.path.join(REPO_ROOT, "assets", "icons"))
        for path, w, h, blobs, rate in pack.iter_pack_icons(HADAL_PACK_DIR):
            self.assertIn(path, replaceable, path)
            self.assertEqual((w, h, len(blobs)), index[path], path)

    def test_icons_with_a_mask_are_replaced_together(self):
        paths = {p for p, *_ in pack.iter_pack_icons(HADAL_PACK_DIR)}
        for path in paths:
            twin = pack.mask_twin(path)
            if twin and twin in self.build.pack.source_icon_index(os.path.join(REPO_ROOT, "assets", "icons")):
                self.assertIn(twin, paths, f"{path} without {twin}")

    def test_every_animation_loops_without_a_jump(self):
        # frame `loop` of a loop would be frame 0 again: the scenes are periodic in the loop
        anims, creatures = self.build.anims, self.build.creatures
        for name, fn in (
            ("angler", lambda p: anims.anglerfish_frame(p, self.build.LOOP)),
            ("jelly", lambda p: creatures.jellyfish_frame(p, self.build.LOOP)),
            ("bathy", lambda p: creatures.bathyscaphe_frame(p, self.build.LOOP)),
            ("vent", lambda p: creatures.vent_frame(p, self.build.LOOP)),
        ):
            first, wrapped = fn(0), fn(self.build.LOOP)
            self.assertTrue(first.image.tobytes() == wrapped.image.tobytes(), f"{name} jumps at the loop point")
            # and it does move
            self.assertTrue(first.image.tobytes() != fn(1).image.tobytes(), f"{name} does not move")

    def test_animation_metadata_is_consistent(self):
        anims_dir = os.path.join(HADAL_PACK_DIR, "Anims")
        with open(os.path.join(anims_dir, "manifest.txt")) as fh:
            manifest = fh.read()
        names = [line.split(":", 1)[1].strip() for line in manifest.splitlines() if line.startswith("Name:")]
        self.assertEqual(len(names), 4)
        for name in names:
            with open(os.path.join(anims_dir, name, "meta.txt")) as fh:
                text = fh.read()
            keys = pack._read_keys(os.path.join(anims_dir, name, "meta.txt"))
            passive, active = int(keys["Passive frames"]), int(keys["Active frames"])
            cycles = int(keys["Active cycles"])
            self.assertEqual(len(keys["Frames order"].split()), passive + active, name)
            frames = anim_frame_blobs(os.path.join(anims_dir, name))
            self.assertEqual(len(frames), passive + active, name)
            total = passive + active * cycles
            # speech bubbles stay inside the period they play in
            for start, end in zip(
                [int(v) for v in re.findall(r"StartFrame: (\d+)", text)],
                [int(v) for v in re.findall(r"EndFrame: (\d+)", text)],
            ):
                self.assertLess(start, end, name)
                self.assertLess(end, total, name)
                self.assertGreaterEqual(start, passive, f"{name}: bubbles belong to the active part")

    def test_the_pack_keeps_the_stock_fonts_and_module_icons(self):
        # no Fonts/ (Hadal's title font is built in) and nothing under MainMenu/
        self.assertFalse(os.path.exists(os.path.join(HADAL_PACK_DIR, "Fonts")))
        paths = [p for p, *_ in pack.iter_pack_icons(HADAL_PACK_DIR)]
        self.assertEqual(paths, ["Common/Loading_24"])

    def test_the_pack_is_small(self):
        total = sum(
            os.path.getsize(os.path.join(d, f)) for d, _, files in os.walk(HADAL_PACK_DIR) for f in files
        )
        self.assertLess(total, 40 * 1024, "the pack should stay light enough for the SD card resources")


class UpdatePackageCheckTests(TempDirCase):
    """scripts/update_package_check.py: it passes a well-formed package and fails each way one can
    go wrong. Packages are made with the real scripts/update.py from a tiny fake firmware and updater."""

    @classmethod
    def setUpClass(cls):
        sys.path.insert(0, os.path.join(REPO_ROOT, "scripts"))
        import update_package_check as checker

        cls.checker = checker
        cls.light = os.path.join(REPO_ROOT, "lib", "stm32wb_copro", "firmware", "stm32wb5x_BLE_Stack_light_fw.bin")
        cls.full = os.path.join(REPO_ROOT, "lib", "stm32wb_copro", "firmware", "stm32wb5x_BLE_Stack_full_fw.bin")

    def make_package(
        self, firmware_size=64 * 1024, stack="light", ob="scripts/ob.data", address=None,
        updater_reset=0x2000_0101, force=False,
    ):
        import struct
        import subprocess

        d = self.p("pkg_%d" % len(os.listdir(self.tmp)))
        os.makedirs(d)
        firmware = struct.pack("<II", 0x20030000, 0x08000101) + bytes(firmware_size - 8)
        updater = struct.pack("<II", 0x20030000, updater_reset) + bytes(4096)
        fw_bin, stage = self.p("fw.bin"), self.p("updater.bin")
        with open(fw_bin, "wb") as f:
            f.write(firmware)
        with open(stage, "wb") as f:
            f.write(updater)
        dfu = self.p("firmware.dfu")
        subprocess.check_call(
            [sys.executable, os.path.join(REPO_ROOT, "scripts", "bin2dfu.py"), "-i", fw_bin, "-o", dfu,
             "-a", "0x08000000", "-l", "test"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        )
        radio = self.light if stack == "light" else self.full
        cmd = [
            sys.executable, os.path.join(REPO_ROOT, "scripts", "update.py"), "generate",
            "-t", "f7", "-d", d, "-v", "test", "--stage", stage, "--dfu", dfu, "--radio", radio,
            "--radiotype", "ble_light" if stack == "light" else "ble_full",
            "--obdata", os.path.join(REPO_ROOT, ob),
        ]
        if address is not None:
            cmd += ["--radioaddr", hex(address)]
        if force:
            cmd += ["--I-understand-what-I-am-doing", "yes"]
        subprocess.check_call(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        return d

    def run_check(self, package, expect="any", **kw):
        with tempfile.TemporaryDirectory() as scratch:
            return self.checker.check_package(package, expect, scratch=scratch, **kw)[0]

    def failures(self, report):
        return [text for level, text in report.lines if level == "FAIL"]

    def test_a_well_formed_light_package_passes_as_standard(self):
        report = self.run_check(self.make_package(), "light")
        self.assertEqual(self.failures(report), [])

    def test_a_full_package_made_the_documented_way_passes_as_full(self):
        report = self.run_check(self.make_package(stack="full", ob="scripts/ob_custradio.data"), "full")
        self.assertEqual(self.failures(report), [])

    HOME_MADE_FULL_OB = 'RDP:0xAA:r\nBOR_LEV:0x4:rw\nnBOOT0:0x1:rw\nnBOOT1:0x1:rw\nnSWBOOT0:0x1:rw\nSRAM2RST:0x0:rw\nSRAM2PE:0x1:rw\nnRST_STOP:0x1:rw\nnRST_STDBY:0x1:rw\nnRSTSHDW:0x1:rw\nWWDGSW:0x1:rw\nIWGDSTDBY:0x1:rw\nIWDGSTOP:0x1:rw\nIWDGSW:0x1:rw\nIPCCDBA:0x0:rw\nESE:0x1:r\nSFSA:0xCE:r\nFSD:0x0:r\nDDS:0x1:r\nC2OPT:0x1:r\nNBRSD:0x0:r\nBRSD:0x0:r\nSBRV:0x35C00:r\nPCROP1A_STRT:0x1FF:r\nPCROP1A_END:0x0:r\nPCROP_RDP:0x1:rw\nPCROP1B_STRT:0x1FF:r\nPCROP1B_END:0x0:r\nWRP1A_STRT:0xFF:r\nWRP1A_END:0x0:r\nWRP1B_STRT:0xFF:r\nWRP1B_END:0x0:r\n'

    def test_the_full_package_with_a_home_made_option_byte_file_is_refused(self):
        # The option bytes of the Full-stack package that was flashed once and hung the radio
        # install: written by hand instead of the file documentation/OTA.md gives
        home_made = self.p("ob_home_made.data")
        with open(home_made, "w") as f:
            f.write(self.HOME_MADE_FULL_OB)
        report = self.run_check(self.make_package(stack="full", ob=home_made), "full")
        self.assertTrue(any("option bytes" in f for f in self.failures(report)), self.failures(report))

    def test_the_repository_default_is_the_documented_full_stack_recipe(self):
        # A plain ./fbt updater_package must give a package that passes as the documented Full stack
        import re

        with open(os.path.join(REPO_ROOT, "fbt_options.py")) as f:
            options = f.read()
        self.assertIn('COPRO_OB_DATA = "scripts/ob_custradio.data"', options)
        self.assertIn('COPRO_STACK_TYPE = "ble_full"', options)
        self.assertIn('COPRO_STACK_BIN = "stm32wb5x_BLE_Stack_full_fw.bin"', options)
        self.assertTrue(os.path.isfile(os.path.join(REPO_ROOT, "scripts", "ob_custradio.data")))

    def test_a_stack_other_than_the_expected_one_is_refused(self):
        report = self.run_check(self.make_package(stack="full", ob="scripts/ob_custradio.data"), "light")
        self.assertTrue(any("BLE_FULL" in f for f in self.failures(report)), self.failures(report))

    def test_a_firmware_that_runs_into_the_radio_stack_is_refused(self):
        # 0x080D7000 is the Light stack; a firmware this big ends beyond it. The packager refuses to
        # make such a package unless told to, which is the first line of defence ...
        import subprocess

        with self.assertRaises(subprocess.CalledProcessError):
            self.make_package(firmware_size=0xD8000)
        # ... and the checker is the second: it still says no when the packager was overruled
        report = self.run_check(self.make_package(firmware_size=0xD8000, force=True), "light")
        self.assertTrue(any("OVERLAPPING" in f for f in self.failures(report)), self.failures(report))

    def test_damage_to_any_file_is_noticed(self):
        for name in ("firmware.dfu", "updater.bin", "radio.bin"):
            package = self.make_package()
            path = os.path.join(package, name)
            with open(path, "r+b") as f:
                f.seek(100)
                byte = f.read(1)
                f.seek(100)
                f.write(bytes([byte[0] ^ 0xFF]))
            report = self.run_check(package, "light")
            self.assertTrue(self.failures(report), f"damage to {name} went unnoticed")

    def test_a_missing_file_is_noticed(self):
        package = self.make_package()
        os.remove(os.path.join(package, "radio.bin"))
        self.assertTrue(self.failures(self.run_check(package, "light")))

    def test_a_manifest_edited_to_other_option_bytes_is_noticed(self):
        package = self.make_package()
        manifest = os.path.join(package, "update.fuf")
        with open(manifest) as f:
            lines = f.read().splitlines()
        for i, line in enumerate(lines):
            if line.startswith("OB write mask:"):
                values = line.split(":", 1)[1].split()
                values[0] = "FF"  # let the updater rewrite more than the standard file allows
                lines[i] = "OB write mask: " + " ".join(values)
        with open(manifest, "w") as f:
            f.write("\n".join(lines) + "\n")
        self.assertTrue(any("option bytes" in f for f in self.failures(self.run_check(package, "light"))))

    def test_an_updater_that_does_not_run_from_ram_is_refused(self):
        report = self.run_check(self.make_package(updater_reset=0x0800_0101), "light")
        self.assertTrue(any("updater vector table" in f for f in self.failures(report)), self.failures(report))

    def real_fap(self):
        import glob

        found = sorted(glob.glob(os.path.join(REPO_ROOT, "build", "f7-firmware-C", ".extapps", "*.fap")))
        if not found:
            self.skipTest("no built app to use (run a firmware build first)")
        return found[0]

    def package_with_app(self, fap_bytes):
        import subprocess

        resources = self.p("res_%d" % len(os.listdir(self.tmp)))
        os.makedirs(os.path.join(resources, "apps", "Tools"))
        with open(os.path.join(resources, "apps", "Tools", "x.fap"), "wb") as f:
            f.write(fap_bytes)
        package = self.make_package()
        # a second package from the same pieces, now with resources
        out = package + "_r"
        subprocess.check_call(
            [sys.executable, os.path.join(REPO_ROOT, "scripts", "update.py"), "generate", "-t", "f7", "-d", out,
             "-v", "test", "--stage", os.path.join(package, "updater.bin"), "--dfu", os.path.join(package, "firmware.dfu"),
             "--radio", os.path.join(package, "radio.bin"), "--radiotype", "ble_light",
             "--obdata", os.path.join(REPO_ROOT, "scripts", "ob.data"), "-r", resources],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        )
        return out

    def test_a_bundled_app_the_firmware_would_load_passes(self):
        with open(self.real_fap(), "rb") as f:
            data = f.read()
        report = self.run_check(self.package_with_app(data), "light")
        self.assertEqual(self.failures(report), [])
        self.assertTrue(any("apps were built for API" in t for _, t in report.lines))

    def test_a_bundled_app_built_for_another_api_is_caught(self):
        import struct

        with open(self.real_fap(), "rb") as f:
            data = bytearray(f.read())
        meta = self.checker.fap_manifest(bytes(data))
        self.assertIsNotNone(meta)
        # find the manifest and change its API major version
        shoff, = struct.unpack_from("<I", data, 0x20)
        shentsize, shnum, shstrndx = struct.unpack_from("<HHH", data, 0x2E)
        sections = [struct.unpack_from("<IIIIII", data, shoff + i * shentsize) for i in range(shnum)]
        names = sections[shstrndx][4]
        for name_offset, _, _, _, offset, size in sections:
            if data[names + name_offset : data.index(b"\0", names + name_offset)] == b".fapmeta":
                struct.pack_into("<H", data, offset + 10, meta[0] + 1)  # major
        report = self.run_check(self.package_with_app(bytes(data)), "light")
        self.assertTrue(any("refuse to load" in f for f in self.failures(report)), self.failures(report))

    def test_the_comparison_with_a_reference_package_reports_every_difference(self):
        light = self.make_package()
        full = self.make_package(stack="full", ob="scripts/ob_custradio.data")
        with tempfile.TemporaryDirectory() as s1, tempfile.TemporaryDirectory() as s2:
            report, summary = self.checker.check_package(full, "any", scratch=s1)
            _, reference = self.checker.check_package(light, "any", scratch=s2)
            self.checker.compare_with_reference(report, summary, reference)
        text = " ".join(self.failures(report))
        for key in ("stack", "radio_address", "radio_version", "radio_crc", "OB reference"):
            self.assertIn(key, text)
        # and a package equals itself
        with tempfile.TemporaryDirectory() as s1, tempfile.TemporaryDirectory() as s2:
            report, summary = self.checker.check_package(light, "any", scratch=s1)
            _, same = self.checker.check_package(light, "any", scratch=s2)
            self.checker.compare_with_reference(report, summary, same)
        self.assertEqual(self.failures(report), [])


class BundleAppsTests(TempDirCase):
    """scripts/bundle_apps.py and applications/bundled_apps.json."""

    @classmethod
    def setUpClass(cls):
        sys.path.insert(0, os.path.join(REPO_ROOT, "scripts"))
        import bundle_apps

        cls.mod = bundle_apps
        with open(bundle_apps.LOCK) as f:
            import json

            cls.lock = json.load(f)

    def good_lock(self):
        import copy

        return copy.deepcopy(self.lock)

    def test_the_real_lock_is_valid_and_pins_a_commit(self):
        self.assertEqual(self.mod.check_lock(self.lock), [])
        self.assertRegex(self.lock["source"]["commit"], r"^[0-9a-f]{40}$")
        self.assertGreater(len(self.lock["apps"]), 100)

    def test_a_branch_instead_of_a_commit_is_refused(self):
        lock = self.good_lock()
        lock["source"]["commit"] = "main"
        self.assertTrue(any("commit" in p for p in self.mod.check_lock(lock)))

    def test_duplicates_and_unsafe_paths_are_refused(self):
        lock = self.good_lock()
        lock["apps"].append(dict(lock["apps"][0]))
        problems = " ".join(self.mod.check_lock(lock))
        self.assertIn("twice", problems)
        lock = self.good_lock()
        lock["apps"].append({"path": "../escape/app", "appids": ["x"]})
        lock["apps"].append({"path": "/abs/app", "appids": ["y"]})
        lock["apps"].append({"path": "a/b", "appids": []})
        problems = self.mod.check_lock(lock)
        self.assertGreaterEqual(len(problems), 3)

    def test_no_bundled_app_has_the_id_of_one_already_in_the_firmware(self):
        # two apps with one id make the build fail: this catches it before a build
        import re

        in_tree = set()
        for d, _, files in os.walk(os.path.join(REPO_ROOT, "applications")):
            if "application.fam" in files:
                with open(os.path.join(d, "application.fam"), errors="replace") as f:
                    in_tree |= set(re.findall(r'appid\s*=\s*"([^"]+)"', f.read()))
        bundled = {a for app in self.lock["apps"] for a in app["appids"]}
        self.assertEqual(sorted(bundled & in_tree), [])

    def test_clean_removes_only_what_fetch_put_there(self):
        stage = self.p("stage")
        os.makedirs(os.path.join(stage, "ours"))
        os.makedirs(os.path.join(stage, "yours"))
        with open(os.path.join(stage, "ours", self.mod.MARKER), "w") as f:
            f.write("x\n")
        saved = self.mod.STAGE
        self.mod.STAGE = stage
        try:
            self.mod.clean()
        finally:
            self.mod.STAGE = saved
        self.assertFalse(os.path.exists(os.path.join(stage, "ours")))
        self.assertTrue(os.path.exists(os.path.join(stage, "yours")), "something that was not ours was removed")

    def test_the_notice_reads_name_author_and_licence_from_the_app(self):
        folder = self.p("app")
        os.makedirs(folder)
        with open(os.path.join(folder, "application.fam"), "w") as f:
            f.write('App(\n    appid="demo",\n    name="Demo App",\n    apptype=FlipperAppType.EXTERNAL,\n'
                    '    fap_author="Someone",\n    fap_weburl="https://example.com/demo",\n)\n')
        with open(os.path.join(folder, "LICENSE"), "w") as f:
            f.write("MIT\n")
        info, licences = self.mod.manifest_info(folder)
        self.assertEqual(info["names"], ["Demo App"])
        self.assertEqual(info["authors"], {"Someone"})
        self.assertEqual(info["urls"], {"https://example.com/demo"})
        self.assertEqual(licences, ["LICENSE"])


class PackInfoTests(TempDirCase):
    """applications/settings/hadal_settings/pack_info.c, the code behind the asset pack manager:
    the real file compiled on the host against host storage, under AddressSanitizer."""

    @classmethod
    def setUpClass(cls):
        import subprocess

        if not _have_sanitizer("address,undefined"):
            raise unittest.SkipTest("gcc with AddressSanitizer is not available")
        cls.build_dir = tempfile.mkdtemp(prefix="api_")
        cls.exe = os.path.join(cls.build_dir, "pack_info_host")
        host = os.path.join(REPO_ROOT, "scripts", "host_tests")
        subprocess.check_call(
            [
                "gcc", "-std=gnu11", "-g", "-O1", "-fsanitize=address,undefined", "-Wall", "-Wextra", "-Werror",
                "-I", os.path.join(host, "stubs"),
                "-I", os.path.join(REPO_ROOT, "applications", "services"),
                "-I", os.path.join(REPO_ROOT, "furi"),
                "-I", os.path.join(REPO_ROOT, "lib"),
                "-I", REPO_ROOT,
                "-o", cls.exe,
                os.path.join(host, "pack_info_host.c"),
                os.path.join(host, "stubs", "storage_host.c"),
                "-lpthread",
            ],
            stderr=subprocess.DEVNULL,
        )

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.build_dir, True)

    def run_host(self, *args):
        import subprocess

        r = subprocess.run([self.exe, self.p("root"), *args], capture_output=True, text=True, errors="replace")
        self.assertEqual(r.returncode, 0, r.stderr[-1500:])
        return r.stdout

    def packs(self):
        path = self.p("root", "ext", "asset_packs")
        os.makedirs(path, exist_ok=True)
        return path

    def write(self, *parts, data=b"x"):
        path = os.path.join(*parts)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "wb") as f:
            f.write(data)

    def scan(self, name):
        return tuple(int(v) for v in self.run_host("scan", name).split())

    def test_the_hadal_pack_is_counted_as_the_packer_counts_it(self):
        shutil.copytree(HADAL_PACK_DIR, os.path.join(self.packs(), "Hadal"))
        icons = len(list(pack.iter_pack_icons(HADAL_PACK_DIR)))
        with open(os.path.join(HADAL_PACK_DIR, "Anims", "manifest.txt")) as f:
            animations = sum(1 for line in f if line.startswith("Name:"))
        self.assertEqual(self.scan("Hadal"), (animations, icons, 0))
        self.assertEqual(self.scan("Hadal"), (4, 1, 0))

    def test_the_list_is_sorted_without_regard_to_case_and_leaves_out_what_is_not_a_pack(self):
        root = self.packs()
        for name in ("zeta", "Alpha", "beta", "Gamma", ".hidden"):
            os.makedirs(os.path.join(root, name))
        self.write(root, "a_file.txt")
        os.makedirs(os.path.join(root, "x" * 40))  # a name the firmware refuses
        self.assertEqual(self.run_host("list").split(), ["Alpha", "beta", "Gamma", "zeta"])

    def test_no_asset_packs_folder_is_an_empty_list(self):
        os.makedirs(self.p("root", "ext"))
        self.assertEqual(self.run_host("list"), "")

    def test_nested_and_animated_icons_are_counted_once_each(self):
        root = os.path.join(self.packs(), "P")
        self.write(root, "Icons", "StatusBar", "A_5x5.bmx")
        self.write(root, "Icons", "StatusBar", "B_5x5.bmx")
        self.write(root, "Icons", "BLE", "BLE_HID", "Ok_9x9.bmx")  # nested two deep
        self.write(root, "Icons", "MainMenu", "NFC_14", "meta")  # an animated icon: a folder
        self.write(root, "Icons", "MainMenu", "NFC_14", "frame_01.bm")
        self.write(root, "Icons", "MainMenu", "NFC_14", "frame_02.bm")
        self.write(root, "Icons", "StatusBar", "readme.txt")  # not an icon
        self.assertEqual(self.scan("P"), (0, 4, 0))

    def test_the_manifest_counts_names_with_or_without_a_final_newline(self):
        root = os.path.join(self.packs(), "P")
        text = "Filetype: Flipper Animation Manifest\nVersion: 1\n\nName: one\nWeight: 3\n\nName: two\nWeight: 3\n\nName: three"
        self.write(root, "Anims", "manifest.txt", data=text.encode())
        self.assertEqual(self.scan("P"), (3, 0, 0))
        self.write(root, "Anims", "manifest.txt", data=(text + "\n").encode())
        self.assertEqual(self.scan("P"), (3, 0, 0))

    def test_only_u8f_files_are_fonts(self):
        root = os.path.join(self.packs(), "P")
        for n in ("Primary.u8f", "Secondary.u8f", "notes.txt", "Keyboard.c"):
            self.write(root, "Fonts", n)
        self.assertEqual(self.scan("P"), (0, 0, 2))

    def test_a_missing_or_bad_name_counts_nothing_and_does_not_escape_the_folder(self):
        self.packs()
        self.write(self.p("root"), "Icons", "x.bmx")  # outside the packs folder
        for name in ("Nope", "..", ".", "a/b", "../..", "x" * 50):
            self.assertEqual(self.scan(name), (0, 0, 0), name)

    def test_an_enormous_manifest_and_a_deep_tree_are_survived(self):
        root = os.path.join(self.packs(), "P")
        self.write(root, "Anims", "manifest.txt", data=b"Name: x\n" * 50000)  # far over what is read
        deep = os.path.join(root, "Icons", *["d"] * 12)
        self.write(deep, "Deep_5x5.bmx")
        animations, icons, fonts = self.scan("P")
        self.assertGreater(animations, 100)  # counted what it read, and stopped
        self.assertLess(animations, 50000)
        self.assertEqual((icons, fonts), (0, 0))  # beyond the depth limit: not counted, not followed

    def test_random_garbage_never_crashes_it(self):
        rng = random.Random(7)
        root = os.path.join(self.packs(), "P")
        for i in range(30):
            self.write(root, "Anims", "manifest.txt", data=bytes(rng.randrange(256) for _ in range(rng.randrange(0, 600))))
            self.write(root, "Icons", "".join(rng.choice("abc_-.12") for _ in range(rng.randrange(1, 40))), "meta")
            self.scan("P")


class UpdateLogTests(TempDirCase):
    """applications/system/updater/util/update_log.c: the trail the updater leaves on the SD card.
    The real file, on host storage, under AddressSanitizer."""

    @classmethod
    def setUpClass(cls):
        import subprocess

        if not _have_sanitizer("address,undefined"):
            raise unittest.SkipTest("gcc with AddressSanitizer is not available")
        cls.build_dir = tempfile.mkdtemp(prefix="aul_")
        cls.exe = os.path.join(cls.build_dir, "update_log_host")
        host = os.path.join(REPO_ROOT, "scripts", "host_tests")
        subprocess.check_call(
            [
                "gcc", "-std=gnu11", "-g", "-O1", "-fsanitize=address,undefined", "-Wall", "-Wextra", "-Werror",
                "-I", os.path.join(host, "stubs"),
                "-I", os.path.join(REPO_ROOT, "applications", "services"),
                "-I", os.path.join(REPO_ROOT, "furi"),
                "-I", os.path.join(REPO_ROOT, "lib"),
                "-I", REPO_ROOT,
                "-o", cls.exe,
                os.path.join(host, "update_log_host.c"),
                os.path.join(host, "stubs", "storage_host.c"),
                "-lpthread",
            ],
            stderr=subprocess.DEVNULL,
        )

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.build_dir, True)

    def run_host(self, *args, root=None):
        import subprocess

        r = subprocess.run([self.exe, root or self.p("root"), *args], capture_output=True, text=True, errors="replace")
        self.assertEqual(r.returncode, 0, r.stderr[-1500:])

    def log_path(self):
        return self.p("root", "ext", "update", "hadal_update.log")

    def make_card(self):
        os.makedirs(os.path.dirname(self.log_path()))

    def read_lines(self):
        with open(self.log_path(), errors="replace") as f:
            return f.read().splitlines()

    def test_lines_are_appended_in_order_with_a_time(self):
        import re

        self.make_card()
        self.run_host("lines", "5")
        self.run_host("lines", "3")  # a later run (the updater restarts) appends, it does not replace
        lines = self.read_lines()
        self.assertEqual(len(lines), 8)
        for line in lines:
            self.assertRegex(line, r"^\[ *\d+\] line \d+$")
        self.assertEqual([l.split("] ", 1)[1] for l in lines], [f"line {i}" for i in (0, 1, 2, 3, 4, 0, 1, 2)])

    def test_over_long_and_odd_lines_are_cut_not_overrun(self):
        self.make_card()
        self.run_host("long")
        lines = self.read_lines()
        self.assertEqual(len(lines), 4)
        for line in lines:
            self.assertLessEqual(len(line), 191)
        self.assertIn("percent signs % and numbers -5 DEADBEEF and a string ok", lines[2])

    def test_a_card_that_cannot_be_written_changes_nothing_and_does_not_crash(self):
        # no ext/update folder at all: the update must go on exactly as it would without a log
        self.run_host("lines", "3")
        self.assertFalse(os.path.exists(self.log_path()))

    def test_the_file_is_started_again_when_it_grows_past_its_limit(self):
        self.make_card()
        for _ in range(12):
            self.run_host("long")  # about 750 bytes each
            self.run_host("lines", "2000")  # about 24 KB each
        size = os.path.getsize(self.log_path())
        self.assertLess(size, 64 * 1024 + 4096)
        self.assertGreater(size, 0)

    def test_the_updater_source_routes_every_radio_call_through_the_log(self):
        # Guard: a radio chip call whose result is not logged would leave a gap in the trail
        with open(os.path.join(REPO_ROOT, "applications", "system", "updater", "util", "update_task_worker_flasher.c")) as f:
            text = f.read()
        for call in ("ble_glue_fus_stack_install", "ble_glue_fus_stack_delete", "ble_glue_fus_wait_operation"):
            for m in re.finditer(call + r"\(", text):
                before = text[max(0, m.start() - 400) : m.start()]
                self.assertTrue(
                    "update_log(" in before or "UPDATE_LOG_" in before,
                    f"{call} is called with no log line just before it",
                )
                after = text[m.start() : m.start() + 400]
                self.assertIn("update_log(", after, f"the result of {call} is not logged")
        self.assertIn("C2 timeout", text)
        self.assertNotIn("FURI_LOG_W(TAG", text)  # every warning also goes to the card


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
        self.assertEqual(out["lock"], "0 0 0 0")

    def test_v17_file_loads_with_no_pack(self):
        out = self.migrate(17)
        self.assertKept(out)
        self.assertEqual((out["usb"], out["menu_style_len"], out["pack_len"]), ("1", "0", "0"))

    def test_v18_file_keeps_everything_and_gains_an_empty_pack(self):
        out = self.migrate(18)
        self.assertKept(out)
        self.assertEqual(out["menu_style"], "Test.fal")
        self.assertEqual(out["pack_len"], "0")

    def test_v19_file_keeps_the_pack_and_gains_the_old_lock_behaviour(self):
        out = self.migrate(19)
        self.assertKept(out)
        self.assertEqual((out["menu_style"], out["pack"]), ("Test.fal", "Hadal"))
        # locks at start, no clock, nothing happens on wrong PINs: as before v20
        self.assertEqual(out["lock"], "0 0 0 0")

    def test_v20_file_keeps_the_lock_options_and_gains_no_dark_mode(self):
        out = self.migrate(20)
        self.assertKept(out)
        self.assertEqual(out["pack"], "Hadal")
        self.assertEqual(out["lock"], "1 1 2 5")
        self.assertEqual(out["dark"], "0")
        self.assertEqual(out["file_version"], 20)

    def test_v21_file_round_trips_dark_mode(self):
        out = self.migrate(21)
        self.assertKept(out)
        self.assertEqual(out["lock"], "1 1 2 5")
        self.assertEqual(out["dark"], "1")
        self.assertEqual(out["file_version"], 21)
        # any other value is read as on, never left as it is
        self.assertEqual(self.migrate(21, "baddark")["dark"], "1")

    def test_older_files_load_without_dark_mode(self):
        for version in (14, 17, 18, 19):
            self.assertEqual(self.migrate(version)["dark"], "0", version)

    def test_an_unknown_wrong_pin_action_is_none(self):
        out = self.migrate(21, "badaction")
        self.assertEqual(out["lock"], "1 1 0 5")

    def test_strings_without_a_terminator_are_cut_to_fit(self):
        out = self.migrate(19, "unterminated")
        self.assertEqual((out["menu_style_len"], out["pack_len"]), ("31", "31"))

    def test_a_damaged_file_gives_defaults_and_is_rewritten_as_v21(self):
        out = self.migrate(19, "badsum")
        self.assertEqual((out["auto_lock"], out["pack_len"], out["menu_style_len"]), ("0", "0", "0"))
        self.assertEqual(out["lock"], "0 0 0 0")
        self.assertEqual(out["dark"], "0")
        self.assertEqual(out["file_version"], 21)


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
        # all frames in one frames.bin, and they really decode to the source pixels
        folder = os.path.join(out, "Anims", "L1_Test_64x32")
        self.assertFalse(any(f.endswith(".bm") for f in os.listdir(folder)))
        blob = anim_frame_blobs(folder)[2]
        _, _, want = reference_raw(self.p("src", "Anims", "L1_Test_64x32", "frame_2.png"))
        self.assertEqual(pack.decode_blob(blob, 64, 32), want)

    def test_missing_and_wrong_size_frames_flagged(self):
        self.make_anim_source()
        out = self.p("pack")
        pack.pack_tree(self.p("src"), out)
        folder = os.path.join(out, "Anims", "L1_Test_64x32")
        blobs = anim_frame_blobs(folder)

        # a frames.bin cut short, or holding too few frames
        frames_file = os.path.join(folder, "frames.bin")
        with open(frames_file, "rb") as f:
            data = f.read()
        with open(frames_file, "wb") as f:
            f.write(data[:-5])
        self.assertTrue(pack.validate_pack(out, None)["errors"])
        with open(frames_file, "wb") as f:
            f.write(frames_file_pack(blobs[:-1]))
        report = pack.validate_pack(out, None)
        self.assertTrue(any("frames.bin holds" in e for e in report["errors"]))

        # packs made elsewhere keep a file per frame; those are read when frames.bin is absent
        os.remove(frames_file)
        for i, blob in enumerate(blobs):
            with open(os.path.join(folder, f"frame_{i}.bm"), "wb") as f:
                f.write(blob)
        self.assertEqual(pack.validate_pack(out, None)["errors"], [])
        os.remove(os.path.join(folder, "frame_3.bm"))
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
        # 264 + Archive/disk_10px + the control centre's 13 icons + the hint bar's 3 glyphs, and
        # the passport's one background for its two pieces
        self.assertEqual(len(index), 280)
        self.assertIn("BLE/BLE_HID/Ok_btn_9x9", index)
        self.assertEqual(index["BLE/BLE_HID/Ok_btn_9x9"], (9, 9, 1))
        hashes = {pack.icon_path_hash(p) for p in index}
        self.assertEqual(len(hashes), len(index), "path hashes must be unique")


# ---- universal remotes: trimming, on-air keys, sending order ----

IR_ASSETS = os.path.join(REPO_ROOT, "applications", "main", "infrared", "resources", "infrared", "assets")
IR_SCENES = os.path.join(REPO_ROOT, "applications", "main", "infrared", "scenes")
IR_LIBRARY_SCENES = {"tv.ir": "tv", "audio.ir": "audio", "projectors.ir": "projector", "fans.ir": "fan", "ac.ir": "ac", "leds.ir": "leds"}


def ir_registered_names(library):
    with open(os.path.join(IR_SCENES, f"infrared_scene_universal_{IR_LIBRARY_SCENES[library]}.c")) as f:
        return re.findall(r'add_record\(\s*brute_force,\s*i\+\+,\s*"([^"]+)"', f.read())


def ir_library_signals(library):
    import rank_ir_library

    with open(os.path.join(IR_ASSETS, library)) as f:
        return rank_ir_library.parse_ir_signals(f.read())


class IrRawTrimTests(TempDirCase):
    """lib/infrared/signal/infrared_raw_trim.c, which decides how much of a raw code the universal
    remote sends and the silence before it: the real file under ASan/UBSan, fuzzed, and against
    the timing model of scripts/rank_ir_library.py on every raw code of the six libraries."""

    @classmethod
    def setUpClass(cls):
        import subprocess

        if not _have_sanitizer("address,undefined"):
            raise unittest.SkipTest("gcc with AddressSanitizer is not available")
        cls.build_dir = tempfile.mkdtemp(prefix="irt_")
        cls.exe = os.path.join(cls.build_dir, "ir_trim_host")
        subprocess.check_call(
            [
                "gcc", "-std=gnu11", "-g", "-O1", "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                "-Wall", "-Wextra", "-Werror",
                "-I", os.path.join(REPO_ROOT, "lib", "infrared", "signal"),
                "-o", cls.exe,
                os.path.join(REPO_ROOT, "scripts", "host_tests", "ir_trim_host.c"),
                os.path.join(REPO_ROOT, "lib", "infrared", "signal", "infrared_raw_trim.c"),
            ]
        )

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.build_dir, True)

    def run_host(self, *args, stdin=None):
        import subprocess

        r = subprocess.run([self.exe, *args], input=stdin, capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stderr[-1500:])
        return r.stdout

    def test_fuzz(self):
        # a cut is always at a frame gap and ends on a mark, Turbo never sends more than Fast,
        # the turbo silence stays within 20..50 ms, whatever the timings
        self.assertIn("ok 100000", self.run_host("fuzz", "100000", "7"))
        self.assertIn("ok 100000", self.run_host("fuzz", "100000", "99"))

    def test_firmware_and_model_agree_on_every_library_raw_code(self):
        import rank_ir_library as r

        signals = []
        for library in IR_LIBRARY_SCENES:
            signals += [r.raw_timings(s) for s in ir_library_signals(library) if s.get("type") == "raw"]
        self.assertGreater(len(signals), 1000)
        out = self.run_host(stdin="\n".join(" ".join(map(str, t)) for t in signals) + "\n").split("\n")
        for t, line in zip(signals, out):
            fast, turbo, gap = map(int, line.split())
            cut = r.raw_cut(t, "turbo")
            self.assertEqual((fast, turbo, gap), (r.raw_cut(t, "fast"), cut, r.raw_turbo_gap_us(t, cut)), t[:10])

    def test_shapes(self):
        nec = [9000, 4500] + [560, 560] * 32 + [560]
        repeat = [9000, 2250, 560]
        sony = [2400, 600] + [1200, 600] * 11 + [600]
        rows = [
            nec,  # one frame: sent whole
            nec + [40000] + repeat + [96000] + repeat,  # NEC + repeats: Fast keeps them, Turbo drops them
            nec + [40000] + nec + [40000] + nec,  # three identical frames: Fast two, Turbo one
            sony + [20000] + sony + [20000] + sony + [20000] + sony,  # Sony: three in both modes
            [500, 500, 500],  # tiny
        ]
        out = [tuple(map(int, l.split())) for l in self.run_host(stdin="\n".join(" ".join(map(str, t)) for t in rows) + "\n").split("\n") if l]
        n = len(nec)
        s = len(sony)
        self.assertEqual(out[0], (n, n, 45000))
        self.assertEqual(out[1][:2], (n + 8, n))
        self.assertEqual(out[2][:2], (2 * n + 1, n))
        self.assertEqual(out[3][:2], (3 * s + 2, 3 * s + 2))
        self.assertEqual(out[4], (3, 3, 20000))


class IrSignalKeyTests(TempDirCase):
    """lib/infrared/signal/infrared_signal_key.c, which tells the universal remote that two
    library entries send the same code: built with the real encoders and decoders."""

    @classmethod
    def setUpClass(cls):
        import subprocess

        if not _have_sanitizer("address,undefined"):
            raise unittest.SkipTest("gcc with AddressSanitizer is not available")
        cls.build_dir = tempfile.mkdtemp(prefix="irk_")
        cls.exe = os.path.join(cls.build_dir, "ir_key_host")
        stubs = os.path.join(REPO_ROOT, "scripts", "host_tests", "stubs_ir")
        codec_dir = os.path.join(REPO_ROOT, "lib", "infrared", "encoder_decoder")
        objects = []
        for root, _, files in os.walk(codec_dir):
            for name in sorted(files):
                if name.endswith(".c"):
                    obj = os.path.join(cls.build_dir, name[:-2] + ".o")
                    # upstream code: a shift into the sign bit (NEC/Samsung encoders) and a
                    # misaligned load (Pioneer decoder) are fine on the Cortex-M4, not checked here
                    subprocess.check_call(
                        [
                            "gcc", "-std=gnu11", "-c", "-g", "-O1", "-fsanitize=address,undefined",
                            "-fno-sanitize=shift,alignment", "-fno-sanitize-recover=all",
                            "-I", stubs, "-I", codec_dir, "-o", obj, os.path.join(root, name),
                        ]
                    )
                    objects.append(obj)
        subprocess.check_call(
            [
                "gcc", "-std=gnu11", "-g", "-O1", "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                "-Wall", "-Wextra", "-Werror",
                "-I", stubs, "-I", codec_dir, "-I", os.path.join(REPO_ROOT, "lib", "infrared", "signal"),
                "-o", cls.exe,
                os.path.join(REPO_ROOT, "scripts", "host_tests", "ir_key_host.c"),
                os.path.join(REPO_ROOT, "lib", "infrared", "signal", "infrared_signal_key.c"),
                *objects,
            ]
        )

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.build_dir, True)

    def keys(self, lines):
        import subprocess

        r = subprocess.run([self.exe], input="\n".join(lines) + "\n", capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stderr[-1500:])
        return r.stdout.split()

    def test_selftest(self):
        # aliases (NEC / NECext, NEC42 / NEC42ext), every protocol's own encoding read back as a
        # capture, held buttons, captures with an unknown frame or with two different codes
        import subprocess

        r = subprocess.run([self.exe, "selftest", "2000", "11"], capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stderr[-1500:])
        self.assertTrue(r.stdout.strip().endswith("ok"), r.stdout[-500:])
        for line in r.stdout.splitlines():
            if line.startswith("roundtrip"):
                done, tried = map(int, line.split()[-1].split("/"))
                self.assertEqual(done, tried, line)

    def test_skewed_capture_is_its_clean_code(self):
        # a capture with a long leader and fat marks, as other receivers record them
        bits = 0xF708_FB04  # NEC address 0x04, command 0x08 (LG power): addr, ~addr, cmd, ~cmd, LSB first
        t = [9250, 4400]
        for i in range(32):
            t += [680, 1640 if bits >> i & 1 else 470]
        t += [680]
        lines = ["P NEC 4 8", "P NECext 64260 63240", "R 38000 " + " ".join(map(str, t)),
                 "R 38000 " + " ".join(map(str, t + [40000, 9200, 2200, 650])), "P NEC 4 9"]
        k = self.keys(lines)
        self.assertEqual(len(set(k[:4])), 1, k)
        self.assertNotEqual(k[0], k[4])

    def test_libraries_parsed_codes_agree_with_the_model(self):
        # for parsed codes the firmware and scripts/rank_ir_library.py must merge exactly the same
        # pairs; raw captures may differ (the firmware knows more protocols), and are counted
        import rank_ir_library as r

        for library in IR_LIBRARY_SCENES:
            names = set(ir_registered_names(library))
            signals = [s for s in ir_library_signals(library) if s["name"] in names]
            lines = [
                f"R {s.get('frequency', 38000)} {s['data']}" if s.get("type") == "raw"
                else f"P {s['protocol']} {r.le_hex(s['address'])} {r.le_hex(s['command'])}"
                for s in signals
            ]
            keys = self.keys(lines)
            self.assertEqual(len(keys), len(signals))
            parsed = [(k, r.signal_key(s)) for k, s in zip(keys, signals) if s.get("type") != "raw"]
            by_c, by_py = {}, {}
            for ck, pk in parsed:
                by_c.setdefault(ck, set()).add(pk)
                by_py.setdefault(pk, set()).add(ck)
            self.assertTrue(all(len(v) == 1 for v in by_c.values()), library)
            self.assertTrue(all(len(v) == 1 for v in by_py.values()), library)


class IrLibraryOrderTests(TempDirCase):
    """scripts/rank_ir_library.py and the .order files it made: the firmware only uses an order
    file made for the exact library next to it, so they must stay in step."""

    def test_order_files_match_their_libraries(self):
        import rank_ir_library as r

        for library in IR_LIBRARY_SCENES:
            path = os.path.join(IR_ASSETS, library)
            order = r.read_order(path)
            self.assertIsNotNone(order, f"{library}: no usable order file (run scripts/rank_ir_library.py order)")
            counts = {}
            for s in ir_library_signals(library):
                counts[s["name"]] = counts.get(s["name"], 0) + 1
            for name in ir_registered_names(library):
                self.assertIn(name, order, f"{library}: {name} has no order")
                self.assertEqual(sorted(order[name]), list(range(counts[name])), f"{library} {name}")

    def test_most_used_first_ties_keep_library_order(self):
        import rank_ir_library as r

        signals = [
            {"name": "Power", "type": "parsed", "protocol": "NEC", "address": "01 00 00 00", "command": "01 00 00 00"},
            {"name": "Power", "type": "parsed", "protocol": "NEC", "address": "02 00 00 00", "command": "01 00 00 00"},
            {"name": "Mute", "type": "parsed", "protocol": "NEC", "address": "02 00 00 00", "command": "05 00 00 00"},
            {"name": "Power", "type": "parsed", "protocol": "NEC", "address": "03 00 00 00", "command": "01 00 00 00"},
            {"name": "Power", "type": "parsed", "protocol": "NECext", "address": "02 FD 00 00", "command": "01 FE 00 00"},
        ]
        counts = {r.signal_key(signals[3]): 5, r.signal_key(signals[1]): 2}
        order = r.button_order(signals, counts)
        self.assertEqual(order["Power"], [2, 1, 3, 0])  # 03 (5 models), 02 and its NECext twin, then 01
        self.assertEqual(order["Mute"], [0])
        sent = r.sending_order(signals, "Power", order)
        self.assertEqual([s["address"] for s in sent], ["03 00 00 00", "02 00 00 00", "01 00 00 00"])

    def test_parsed_replaces_a_raw_capture_of_the_same_code(self):
        import rank_ir_library as r

        t = [9000, 4500]
        bits = 0xF708_FB04  # NEC 0x04 / 0x08
        for i in range(32):
            t += [560, 1690 if bits >> i & 1 else 560]
        t += [560]
        signals = [
            {"name": "Power", "type": "raw", "frequency": "38000", "duty_cycle": "0.33", "data": " ".join(map(str, t))},
            {"name": "Power", "type": "parsed", "protocol": "Samsung32", "address": "07 00 00 00", "command": "02 00 00 00"},
            {"name": "Power", "type": "parsed", "protocol": "NEC", "address": "04 00 00 00", "command": "08 00 00 00"},
        ]
        sent = r.sending_order(signals, "Power")
        self.assertEqual([s.get("protocol", "raw") for s in sent], ["NEC", "Samsung32"])

    def test_stale_order_file_is_ignored(self):
        import rank_ir_library as r

        lib = self.p("x.ir")
        with open(lib, "w") as f:
            f.write("Filetype: IR library file\nVersion: 1\n#\nname: Power\ntype: parsed\nprotocol: NEC\naddress: 01 00 00 00\ncommand: 01 00 00 00\n")
        r.write_order(lib, {"Power": [0]})
        self.assertEqual(r.read_order(lib), {"Power": [0]})
        with open(lib, "a") as f:
            f.write("#\n")
        self.assertIsNone(r.read_order(lib))


class DfuRangeTests(TempDirCase):
    """lib/update_util/dfu_file.c dfu_file_get_address_range(): the RAM updater uses it to decide
    whether the new firmware can go in before the radio stack (it must, for BLE Light -> Full,
    or the Full stack overwrites the end of the old firmware). Built as-is on the host."""

    @classmethod
    def setUpClass(cls):
        import subprocess

        if not _have_sanitizer("address,undefined"):
            raise unittest.SkipTest("gcc with AddressSanitizer is not available")
        cls.build_dir = tempfile.mkdtemp(prefix="dfu_")
        cls.exe = os.path.join(cls.build_dir, "dfu_range_host")
        host = os.path.join(REPO_ROOT, "scripts", "host_tests")
        subprocess.check_call(
            [
                "gcc", "-std=gnu11", "-g", "-O1", "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                "-Wall", "-Wextra", "-Werror",
                "-I", os.path.join(host, "stubs_dfu"),
                "-I", os.path.join(host, "stubs"),
                "-I", os.path.join(REPO_ROOT, "applications", "services"),
                "-I", os.path.join(REPO_ROOT, "lib"),
                "-I", os.path.join(REPO_ROOT, "furi"),
                "-I", REPO_ROOT,
                "-o", cls.exe,
                os.path.join(host, "dfu_range_host.c"),
                os.path.join(REPO_ROOT, "lib", "update_util", "dfu_file.c"),
                os.path.join(host, "stubs", "storage_host.c"),
                "-lpthread",
            ]
        )

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.build_dir, True)

    @staticmethod
    def make_dfu(targets):
        """A DfuSe file: targets is a list of lists of (address, data)."""
        import struct
        import zlib

        body = b""
        for elements in targets:
            payload = b"".join(struct.pack("<II", a, len(d)) + d for a, d in elements)
            body += b"Target" + bytes([0, 1, 0, 0, 0]) + b"hadal".ljust(255, b"\0") + struct.pack("<II", len(payload), len(elements)) + payload
        prefix = b"DfuSe" + bytes([1]) + struct.pack("<I", 11 + len(body)) + bytes([len(targets)])
        image = prefix + body
        suffix = struct.pack("<HHHH", 0xFFFF, 0xDF11, 0x0483, 0x011A) + b"UFD" + bytes([16])
        crc = zlib.crc32(image + suffix) ^ 0xFFFFFFFF
        return image + suffix + struct.pack("<I", crc)

    def run_host(self, path):
        import subprocess

        r = subprocess.run([self.exe, path], capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stderr[-1500:])
        targets, crc_ok, ok, start, end = r.stdout.split()
        return int(targets), crc_ok == "1", ok == "1", int(start, 16), int(end, 16)

    def write(self, name, data):
        path = self.p(name)
        with open(path, "wb") as f:
            f.write(data)
        return path

    def test_one_element(self):
        path = self.write("a.dfu", self.make_dfu([[(0x08000000, b"\x11" * 5000)]]))
        self.assertEqual(self.run_host(path), (1, True, True, 0x08000000, 0x08000000 + 5000))

    def test_several_targets_and_elements(self):
        path = self.write("b.dfu", self.make_dfu([
            [(0x08001000, b"a" * 300), (0x08000000, b"b" * 100)],
            [(0x08080000, b"c" * 4096)],
        ]))
        self.assertEqual(self.run_host(path), (2, True, True, 0x08000000, 0x08081000))

    def test_cut_short(self):
        data = self.make_dfu([[(0x08000000, b"\x22" * 9000)]])
        path = self.write("c.dfu", data[: len(data) // 2])
        targets, crc_ok, ok, _, _ = self.run_host(path)
        self.assertFalse(crc_ok)
        self.assertEqual(targets, 0)  # the updater then keeps the old order, radio first

    def test_built_package_fits_below_the_full_stack(self):
        dfu = os.path.join(REPO_ROOT, "dist", "f7-C", "f7-update-local", "firmware.dfu")
        if not os.path.exists(dfu):
            self.skipTest("no update package built")
        sys.path.insert(0, os.path.join(REPO_ROOT, "scripts"))
        import update_package_check

        with open(dfu, "rb") as f:
            elements = update_package_check.parse_dfu(f.read())[0]
        want_start = min(a for a, _ in elements)
        want_end = max(a + len(d) for a, d in elements)
        self.assertEqual(self.run_host(dfu), (1, True, True, want_start, want_end))
        self.assertLessEqual(want_end, 0x080CE000, "the firmware must end below the Full stack")


class MainMenuListTests(TempDirCase):
    """applications/services/loader/loader_menu_list.c: the main menu file the user arranges
    (Hadal Settings > Interface > Main Menu). Built as-is under ASan/UBSan and fuzzed."""

    @classmethod
    def setUpClass(cls):
        import subprocess

        if not _have_sanitizer("address,undefined"):
            raise unittest.SkipTest("gcc with AddressSanitizer is not available")
        cls.build_dir = tempfile.mkdtemp(prefix="mml_")
        cls.exe = os.path.join(cls.build_dir, "menu_list_host")
        loader = os.path.join(REPO_ROOT, "applications", "services", "loader")
        subprocess.check_call(
            [
                "gcc", "-std=gnu11", "-g", "-O1", "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                "-Wall", "-Wextra", "-Werror", "-I", loader, "-o", cls.exe,
                os.path.join(REPO_ROOT, "scripts", "host_tests", "menu_list_host.c"),
                os.path.join(loader, "loader_menu_list.c"),
            ]
        )

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.build_dir, True)

    def parse(self, text):
        import subprocess

        data = text.encode() if isinstance(text, str) else text
        r = subprocess.run([self.exe], input=data, capture_output=True)
        self.assertEqual(r.returncode, 0, r.stderr[-1500:])
        return [tuple(line.split(" ", 1)) for line in r.stdout.decode().splitlines()]

    def test_order_kinds_comments_and_spaces(self):
        got = self.parse("# my menu\r\n  NFC  \n\nApps\n/ext/apps/Tools/clock.fap\r\nSettings\nSub-GHz\n")
        self.assertEqual(got, [("N", "NFC"), ("A", "Apps"), ("P", "/ext/apps/Tools/clock.fap"), ("S", "Settings"), ("N", "Sub-GHz")])

    def test_settings_is_always_there(self):
        self.assertEqual(self.parse(""), [("S", "Settings")])
        self.assertEqual(self.parse("NFC\n"), [("N", "NFC"), ("S", "Settings")])

    def test_duplicates_kept_once(self):
        self.assertEqual(self.parse("NFC\nNFC\nSettings\nSettings\n"), [("N", "NFC"), ("S", "Settings")])

    def test_full_list_keeps_settings(self):
        lines = [f"App {i}" for i in range(200)] + ["Settings"]
        got = self.parse("\n".join(lines))
        self.assertEqual(len(got), 64)
        self.assertEqual(got[-1], ("S", "Settings"))
        got = self.parse("\n".join(lines[:200]))
        self.assertEqual(len(got), 64)
        self.assertEqual(got[-1], ("S", "Settings"))

    def test_long_and_binary_lines_skipped(self):
        got = self.parse(b"x" * 300 + b"\nNFC\nbad\x00line\nSettings")
        self.assertEqual(got, [("N", "NFC"), ("S", "Settings")])

    def test_fuzz(self):
        import subprocess

        for seed in ("3", "77"):
            r = subprocess.run([self.exe, "fuzz", "3000", seed], capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stderr[-1500:])
            self.assertIn("ok 3000", r.stdout)


class ResourceManifestCheckTests(unittest.TestCase):
    """update_package_check.resource_manifest_problems(): the archive must let updates skip
    unchanged files (Manifest first, every file listed with its size and MD5)."""

    def manifest(self, files):
        import hashlib

        lines = ["V:0", "T:0"] + [f"F:{hashlib.md5(d).hexdigest()}:{len(d)}:{n}" for n, d in files.items()]
        return ("\n".join(lines) + "\n").encode()

    def test_good_and_bad(self):
        sys.path.insert(0, os.path.join(REPO_ROOT, "scripts"))
        from update_package_check import resource_manifest_problems

        files = {"apps/a.fap": b"abc", "infrared/assets/tv.ir": b"0123"}
        good = dict(files, Manifest=self.manifest(files))
        self.assertEqual(resource_manifest_problems(good, "Manifest"), [])
        self.assertTrue(resource_manifest_problems(good, "apps/a.fap"))  # not first
        stale = dict(good, **{"apps/a.fap": b"abd"})
        self.assertIn("apps/a.fap differs from its Manifest entry", resource_manifest_problems(stale, "Manifest"))
        extra = dict(good, **{"new.txt": b"x"})
        self.assertIn("new.txt is not in the Manifest", resource_manifest_problems(extra, "Manifest"))
        self.assertIn("there is no Manifest", resource_manifest_problems(files, "apps/a.fap"))


class ResourceIndexTests(TempDirCase):
    """applications/system/updater/util/update_resource_index.c: which files an update can leave
    alone. Built as-is under ASan/UBSan."""

    @classmethod
    def setUpClass(cls):
        import subprocess

        if not _have_sanitizer("address,undefined"):
            raise unittest.SkipTest("gcc with AddressSanitizer is not available")
        cls.build_dir = tempfile.mkdtemp(prefix="rix_")
        cls.exe = os.path.join(cls.build_dir, "resource_index_host")
        util = os.path.join(REPO_ROOT, "applications", "system", "updater", "util")
        subprocess.check_call(
            [
                "gcc", "-std=gnu11", "-g", "-O1", "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                "-Wall", "-Wextra", "-Werror", "-I", util, "-o", cls.exe,
                os.path.join(REPO_ROOT, "scripts", "host_tests", "resource_index_host.c"),
                os.path.join(util, "update_resource_index.c"),
            ]
        )

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.build_dir, True)

    def run_index(self, lines):
        import subprocess

        r = subprocess.run([self.exe], input="\n".join(lines) + "\n", capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stderr[-1500:])
        return r.stdout.split()

    def test_lookups(self):
        a, b = "11" * 16, "22" * 16
        got = self.run_index([
            f"F apps/Tools/clock.fap 1000 {a}",
            "D apps/Tools",
            f"F infrared/assets/tv.ir 99445 {b}",
            ".",
            f"? apps/Tools/clock.fap 1000 {a}",   # same file
            f"? apps/Tools/clock.fap 1001 {a}",   # size differs
            f"? apps/Tools/clock.fap 1000 {b}",   # content differs
            f"? ./apps/Tools/clock.fap 1000 {a}", # tar-style name
            f"? /apps/Tools/clock.fap 1000 {a}",
            f"? apps/Tools/ 0 {a}",               # folder, trailing slash
            f"? apps/Tools/other.fap 1000 {a}",   # not in the new resources
        ])
        self.assertEqual(got, ["match", "differs", "differs", "match", "match", "folder", "absent"])

    def test_many_entries(self):
        import random

        rng = random.Random(5)
        files = [(f"dir{i % 37}/file_{i}.bin", rng.randrange(1 << 20), "%032x" % rng.getrandbits(128)) for i in range(5000)]
        lines = [f"F {n} {sz} {h}" for n, sz, h in files] + ["."]
        lines += [f"? {n} {sz} {h}" for n, sz, h in files]
        got = self.run_index(lines)
        # a hash two names share may hide one of them (it is then written, never wrongly skipped)
        self.assertGreaterEqual(got.count("match"), 4990)
        self.assertEqual(got.count("folder"), 0)


class BootGuardTests(TempDirCase):
    """lib/update_util/boot_guard_state.c: when a new firmware is replaced by the last one that
    started steadily. Built as-is under ASan/UBSan."""

    @classmethod
    def setUpClass(cls):
        import subprocess

        if not _have_sanitizer("address,undefined"):
            raise unittest.SkipTest("gcc with AddressSanitizer is not available")
        cls.build_dir = tempfile.mkdtemp(prefix="bgd_")
        cls.exe = os.path.join(cls.build_dir, "boot_guard_host")
        lib = os.path.join(REPO_ROOT, "lib", "update_util")
        subprocess.check_call(
            [
                "gcc", "-std=gnu11", "-g", "-O1", "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                "-Wall", "-Wextra", "-Werror", "-I", lib, "-o", cls.exe,
                os.path.join(REPO_ROOT, "scripts", "host_tests", "boot_guard_host.c"),
                os.path.join(lib, "boot_guard_state.c"),
            ]
        )

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.build_dir, True)

    def run_guard(self, events):
        import subprocess

        r = subprocess.run([self.exe], input="\n".join(events) + "\n", capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stderr[-1500:])
        return r.stdout.splitlines()

    def test_failing_firmware_is_rolled_back_once(self):
        out = self.run_guard(["U 1 0", "B", "B", "B", "B", "B", "B"])
        self.assertEqual(out[0], "1 1 0 0 -")      # armed after an update, a rollback exists
        self.assertEqual(out[1:4], ["1 1 0 1 -", "1 1 0 2 -", "1 1 0 3 -"])
        self.assertEqual(out[4], "0 1 1 4 ROLLBACK")  # the 4th start that never settled
        self.assertEqual(out[5:], ["0 1 1 4 -", "0 1 1 4 -"])  # never twice

    def test_steady_firmware_is_left_alone(self):
        out = self.run_guard(["U 1 0", "B", "B", "S", "B", "B", "B", "B"])
        self.assertEqual(out[3], "0 1 0 0 -")
        self.assertTrue(all(line.endswith(" -") for line in out))

    def test_no_rollback_package_no_watch(self):
        out = self.run_guard(["U 0 0", "B", "B", "B", "B", "B"])
        self.assertTrue(all(line.startswith("0 0 0 0") for line in out), out)

    def test_after_a_rollback_nothing_is_watched(self):
        out = self.run_guard(["U 1 0", "B", "B", "B", "B", "U 1 1", "B", "B", "B", "B"])
        self.assertEqual(out[5], "0 1 0 0 -")
        self.assertTrue(all(line.endswith(" -") for line in out[6:]))

    def test_foreign_register_reads_as_off(self):
        out = self.run_guard(["R 12345678", "B", "R ffffffff", "B"])
        self.assertTrue(all(line == "0 0 0 0 -" for line in out), out)


class CrashReportScriptTests(unittest.TestCase):
    """scripts/crash_report.py reads the addresses out of a report as the firmware writes it
    (applications/services/desktop/helpers/crash_report_save.c)."""

    def test_addresses(self):
        sys.path.insert(0, os.path.join(REPO_ROOT, "scripts"))
        import crash_report

        text = (
            "Hadal crash report\nIn: the thread \"NFC\"\nMessage: furi_check failed\n"
            "Fault at: 0x0801ABCD (in a function called from 0x08020001)\n"
            "Raised from: 0x08001000\nCall chain: 0x08002001 0x08003003\n"
            "Registers:\n r0=00000001 r1=08001234\n"
        )
        self.assertEqual(
            crash_report.report_addresses(text),
            [("fault", 0x0801ABCD), ("raised", 0x08001000), ("fault caller", 0x08020001),
             ("stack #0", 0x08002001), ("stack #1", 0x08003003)],
        )
        # without a fault (a furi_check in a thread)
        plain = "Raised from: 0x08001000\nCall chain:\n"
        self.assertEqual(crash_report.report_addresses(plain), [("raised", 0x08001000)])


class BleSeenTests(TempDirCase):
    """applications/system/ble_tools/ble_seen.c: what Tracker Detector, Find My Stuff and Crowd
    Meter know about the devices around. Built as-is under ASan/UBSan."""

    # Find My: long payload away from the owner, short with the owner near; the status byte's
    # bits 4-5 say what sent it (1 AirTag, 2 AirPods, 3 another maker's accessory, 0 an iPhone...)
    AIRTAG_AWAY = "1EFF4C001219" + "10" + "AB" * 22 + "0100"
    AIRTAG_HOME = "07FF4C0012021000"
    ACCESSORY_AWAY = "1EFF4C001219" + "34" + "AB" * 22 + "0100"
    # E4: what an AirPods Pro case sent, near its owner and (the same status) away
    AIRPODS_AWAY = "1EFF4C001219" + "E4" + "AB" * 22 + "0100"
    AIRPODS_HOME = "07FF4C001202E400"
    IPHONE_AWAY = "1EFF4C001219" + "00" + "AB" * 22 + "0100"
    FIND_MY_CUT = "0EFF4C001219" + "10" + "AB" * 8                # says long, is not: not a tag
    GOOGLE_UTP = "0D16AAFE41" + "CD" * 9                             # Google tag, unwanted-tracking mode
    GOOGLE_HOME = "0D16AAFE40" + "CD" * 9
    TILE = "0303EDFE"                                                # 16-bit UUID list with Tile
    NAMED = "0A09" + "Headphone".encode().hex()             # length byte: type + 9 characters
    AIRPODS = "0BFF4C0007" + "00" * 7

    @classmethod
    def setUpClass(cls):
        import subprocess

        if not _have_sanitizer("address,undefined"):
            raise unittest.SkipTest("gcc with AddressSanitizer is not available")
        cls.build_dir = tempfile.mkdtemp(prefix="bls_")
        cls.exe = os.path.join(cls.build_dir, "ble_seen_host")
        app = os.path.join(REPO_ROOT, "applications", "system", "ble_tools")
        subprocess.check_call(
            [
                "gcc", "-std=gnu11", "-g", "-O1", "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                "-Wall", "-Wextra", "-Werror", "-I", app, "-o", cls.exe,
                os.path.join(REPO_ROOT, "scripts", "host_tests", "ble_seen_host.c"),
                os.path.join(app, "ble_seen.c"),
                os.path.join(app, "ble_mine.c"),
                os.path.join(app, "ble_crowd.c"),
            ]
        )

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.build_dir, True)

    def run_seen(self, lines, *args):
        import subprocess

        r = subprocess.run([self.exe, *args], input="\n".join(lines) + "\n", capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stderr[-1500:])
        return r.stdout.splitlines()

    def test_kinds_and_separation(self):
        out = self.run_seen([
            f"H 010000000000 -60 0 {self.AIRTAG_AWAY}",
            f"H 020000000000 -60 0 {self.AIRTAG_HOME}",
            f"H 030000000000 -60 0 {self.GOOGLE_UTP}",
            f"H 040000000000 -60 0 {self.GOOGLE_HOME}",
            f"H 050000000000 -60 0 {self.TILE}",
            f"H 060000000000 -60 0 {self.NAMED}",
            f"H 070000000000 -60 0 {self.AIRPODS}",
            "?",
        ])
        self.assertEqual(out[:7], [
            "010000000000 AirTag separated=1 name= rssi=-60",
            "020000000000 AirTag separated=0 name= rssi=-60",
            "030000000000 Google tag separated=1 name= rssi=-60",
            "040000000000 Google tag separated=0 name= rssi=-60",
            "050000000000 Tile separated=1 name= rssi=-60",
            "060000000000 Named device separated=0 name=Headphone rssi=-60",
            "070000000000 AirPods separated=0 name= rssi=-60",
        ])

    def test_find_my_says_what_sent_it(self):
        # AirPods and iPhones away from their owners send the same advertisement as AirTags
        out = self.run_seen([
            f"H 010000000000 -60 0 {self.ACCESSORY_AWAY}",
            f"H 020000000000 -60 0 {self.AIRPODS_AWAY}",
            f"H 030000000000 -60 0 {self.IPHONE_AWAY}",
            f"H 040000000000 -60 0 {self.FIND_MY_CUT}",
            f"H 050000000000 -60 0 {self.AIRPODS_HOME}",
            "?",
        ])
        self.assertEqual(out[:5], [
            "010000000000 Find My tag separated=1 name= rssi=-60",
            "020000000000 Find My AirPods separated=1 name= rssi=-60",
            "030000000000 Apple device separated=0 name= rssi=-60",
            "040000000000 Apple device separated=0 name= rssi=-60",
            "050000000000 Find My AirPods separated=0 name= rssi=-60",
        ])

    def test_airpods_away_from_their_owner_follow_like_a_tag(self):
        # a case slipped into someone's bag tracks them as well as an AirTag would; with its
        # owner near, or marked as yours, it is no warning
        def with_you(adv, *extra):
            lines = list(extra)
            for minute in range(0, 12):
                t = minute * 60000
                lines += [f"H 010000000000 -70 {t} {adv}", f"E {t} 60000 180000"]
            return self.run_seen(lines + ["F 660000 600000"])

        self.assertEqual(with_you(self.AIRPODS_AWAY), ["010000000000", "."])
        self.assertEqual(with_you(self.AIRPODS_HOME), ["."])
        self.assertEqual(
            with_you(self.AIRPODS_AWAY, "L 00:00:00:00:00:01 11 My AirPods case", "P"), ["."]
        )

    def test_an_iphone_away_from_its_owner_is_no_tracker(self):
        lines = []
        for minute in range(0, 12):
            t = minute * 60000
            lines += [f"H 010000000000 -70 {t} {self.IPHONE_AWAY}", f"E {t} 60000 180000"]
        self.assertEqual(self.run_seen(lines + ["F 660000 600000"]), ["."])

    # ---- the user's own devices (ble_mine.c) ----

    def heard(self, addr, rssi, start, end, adv=None, step=2000):
        adv = adv or self.AIRTAG_AWAY
        return [f"H {addr} {rssi} {t} {adv}" for t in range(start, end + 1, step)]

    def test_mine_file_reads_writes_and_skips_what_is_not_a_device(self):
        out = self.run_seen([
            "L # my things",
            "L AA:BB:CC:DD:EE:01 10 AirTag keys",
            "L not a device",
            "L AA:BB:CC:DD:EE:02 99 kind out of range",
            "L aa:bb:cc:dd:ee:03 2 Buds\r",
            "L AA:BB:CC:DD:EE:01 1 the same again",
            "P", "W",
        ])
        self.assertEqual(out, ["AA:BB:CC:DD:EE:01 10 AirTag keys", "AA:BB:CC:DD:EE:03 2 Buds", "."])

    def test_a_tracker_of_mine_never_warns_and_is_known_next_time(self):
        lines = self.heard("010000000000", -60, 0, 660000, step=60000)
        lines += ["M 010000000000 1 AirTag", "F 660000 600000", "W", "Q"]
        out = self.run_seen(lines)
        self.assertEqual(out[0], ".")                                   # not following: mine
        self.assertEqual(out[1:3], ["00:00:00:00:00:01 10 AirTag", "."])  # file: most significant first
        self.assertEqual(out[3], "010000000000 mine=1")
        # read back from the file, it is recognised when first heard
        out = self.run_seen(["L 00:00:00:00:00:01 10 AirTag", "P",
                             *self.heard("010000000000", -60, 0, 0), "Q"])
        self.assertEqual(out[0], "010000000000 mine=1")
        # and unmarked, it is a stranger's again
        out = self.run_seen([*self.heard("010000000000", -60, 0, 0), "M 010000000000 1 a",
                             "M 010000000000 0 a", "W", "Q"])
        self.assertEqual(out, [".", "010000000000 mine=0", "."])

    def test_a_mark_follows_its_tracker_to_a_new_address(self):
        lines = self.heard("010000000000", -60, 0, 60000)              # mine, until 60 s
        lines += ["M 010000000000 1 Keys"]
        lines += self.heard("020000000000", -63, 62000, 70000)          # then it, under a new address
        lines += ["T 72000", "W", "Q", "F 1000000 600000"]
        out = self.run_seen(lines)
        self.assertEqual(out[0], "1")
        self.assertEqual(out[1:3], ["00:00:00:00:00:02 10 Keys", "."])
        self.assertIn("020000000000 mine=1", out)
        self.assertEqual(out[-1], ".")                                  # neither address warns

    def test_a_mark_stays_put_when_it_cannot_tell(self):
        base = self.heard("010000000000", -60, 0, 60000) + ["M 010000000000 1 Keys"]
        cases = {
            # two of its kind turned up: no telling which
            "two": self.heard("020000000000", -61, 62000, 70000) + self.heard("030000000000", -62, 63000, 70000),
            # far stronger: another one, close by
            "strong": self.heard("020000000000", -40, 62000, 70000),
            # heard while it still was: another one
            "together": self.heard("020000000000", -60, 50000, 70000),
            # long after it fell silent
            "late": self.heard("020000000000", -60, 90000, 98000),
            # another kind
            "kind": self.heard("020000000000", -60, 62000, 70000, adv=self.GOOGLE_UTP),
        }
        for name, extra in cases.items():
            out = self.run_seen(base + extra + ["T 100000", "W"])
            self.assertEqual(out[0], "0", name)
            self.assertEqual(out[1], "00:00:00:00:00:01 10 Keys", name)

    # AirPods Pro (0x0E20), colour 2, in the pairing broadcast; an iPhone's Nearby message; a
    # Samsung phone; an iBeacon; a Windows computer
    AIRPODS_PRO = "1EFF4C00071901" + "0E20" + "55AA1105" + "02" + "00" + "00" * 16
    IPHONE = "0AFF4C0010050118445566"
    SAMSUNG = "0BFF7500" + "42" * 8
    IBEACON = "1AFF4C000215" + "11" * 16 + "00010002C5"
    WINDOWS = "0BFF0600" + "01" * 8
    FLAGS_ONLY = "020106"

    def test_airpods_say_their_model_and_are_known_by_it(self):
        out = self.run_seen([
            f"H 010000000000 -50 0 {self.AIRPODS_PRO}", "O 010000000000",
            "M 010000000000 1 My AirPods", "W",
            # the same pair under another address, on you; and a stranger's, across the room
            f"H 090000000000 -52 1000 {self.AIRPODS_PRO}",
            f"H 0a0000000000 -85 1000 {self.AIRPODS_PRO}", "Q",
        ])
        self.assertEqual(out[0], "Worn AirPods Pro")
        self.assertEqual(out[1:3], ["00:00:00:00:00:01 2 @0E2002 My AirPods", "."])
        self.assertIn("090000000000 mine=1", out)
        self.assertIn("0a0000000000 mine=0", out)
        # the signature survives the file
        out = self.run_seen(["L 00:00:00:00:00:01 2 @0E2002 My AirPods", "P", "W",
                             f"H 0b0000000000 -50 0 {self.AIRPODS_PRO}", "Q"])
        self.assertEqual(out[:2], ["00:00:00:00:00:01 2 @0E2002 My AirPods", "."])
        self.assertIn("0b0000000000 mine=1", out)

    def test_a_paired_phone_is_mine(self):
        out = self.run_seen([f"H 4a0000000040 -60 0 {self.IPHONE}", "A 4a0000000040", "Q"])
        self.assertEqual(out[0], "4a0000000040 mine=1")

    def test_people_are_phones_and_what_is_worn_beyond_them(self):
        lines = []
        for i, adv in enumerate([self.IPHONE] * 3 + [self.SAMSUNG] + [self.AIRPODS_PRO] * 2 +
                                [self.AIRTAG_AWAY, self.IBEACON, self.WINDOWS] + [self.FLAGS_ONLY] * 2):
            lines.append(f"H {i + 1:012x} -60 1000 {adv}")
        lines += ["K 2000 60000 -127"]
        # 4 phones, 2 worn, 3 things, 2 unknown: 4 people
        self.assertEqual(self.run_seen(lines), ["4 2 3 2 0 4"])
        # earbuds beyond the phones are people too (their phones are quiet)
        lines = [f"H 000000000001 -60 1000 {self.IPHONE}"]
        lines += [f"H {i + 2:012x} -60 1000 {self.AIRPODS_PRO}" for i in range(3)]
        self.assertEqual(self.run_seen(lines + ["K 2000 60000 -127"]), ["1 3 0 0 0 3"])

    def test_a_phone_under_two_addresses_is_one_person(self):
        out = self.run_seen([
            *self.heard("000000000001", -60, 0, 30000, adv=self.IPHONE),
            *self.heard("000000000002", -62, 34000, 50000, adv=self.IPHONE),
            # two phones heard at the same time are two
            *self.heard("000000000003", -61, 0, 50000, adv=self.IPHONE),
            "K 50000 60000 -127",
        ])
        self.assertEqual(out, ["2 0 0 0 0 2"])

    def test_mine_is_not_the_crowd_and_the_range_is_kept(self):
        lines = [
            f"H 000000000001 -55 1000 {self.IPHONE}", "M 000000000001 1 phone",
            f"H 000000000002 -60 1000 {self.IPHONE}",
            f"H 000000000003 -85 1000 {self.IPHONE}",
        ]
        # everything in range, then near only: the far phone is left out
        self.assertEqual(self.run_seen(lines + ["K 2000 60000 -127", "K 2000 60000 -70"]),
                         ["2 0 0 0 1 2", "1 0 0 0 1 1"])

    def test_mine_fuzz(self):
        import subprocess

        r = subprocess.run([self.exe, "fuzzmine", "3000", "7"], capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stderr[-1500:])
        self.assertEqual(r.stdout.strip(), "ok")

    def test_a_name_does_not_hide_a_tracker(self):
        out = self.run_seen([f"H 010000000000 -60 0 {self.TILE}", f"H 010000000000 -60 10 {self.NAMED}", "?"])
        self.assertTrue(out[0].startswith("010000000000 Tile "), out[0])

    def test_following_needs_away_and_time(self):
        lines = []
        for minute in range(0, 12):
            t = minute * 60000
            lines += [f"H 010000000000 -70 {t} {self.AIRTAG_AWAY}", f"H 020000000000 -70 {t} {self.AIRTAG_HOME}",
                      f"E {t} 60000 180000"]
        lines += ["F 540000 600000", "F 660000 600000"]
        # at 9 min nothing yet; at 11 min only the tag away from its owner
        self.assertEqual(self.run_seen(lines), [".", "010000000000", "."])

    def test_a_gap_starts_the_clock_again(self):
        out = self.run_seen([
            f"H 010000000000 -70 0 {self.AIRTAG_AWAY}",
            "E 400000 60000 180000",                          # unheard for 6.7 min: forgotten
            f"H 010000000000 -70 400000 {self.AIRTAG_AWAY}",
            "F 900000 600000",                                # 8.3 min since it came back
        ])
        self.assertEqual(out, ["."])

    def test_counts_and_a_full_table(self):
        lines = [f"H {i:012x} -60 {i * 10} 020106" for i in range(1, 41)]
        lines += ["C 400 1000", "C 400 100", "?"]
        out = self.run_seen(lines, "16")
        self.assertEqual(out[0], "16")          # the table holds 16, the newest
        self.assertEqual(out[1], "11")          # heard within the last 100 ms (300..400, both ends)
        self.assertEqual(len(out) - 3, 16)

    def test_fuzz(self):
        import subprocess

        for seed in ("1", "99"):
            r = subprocess.run([self.exe, "fuzz", "20000", seed], capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stderr[-1500:])


class IrLearnedTests(unittest.TestCase):
    """Universal remote codes that worked, sent first next time (lib/infrared/signal/infrared_learned.c)"""

    @classmethod
    def setUpClass(cls):
        import subprocess

        if not _have_sanitizer("address,undefined"):
            raise unittest.SkipTest("gcc with AddressSanitizer is not available")
        cls.build_dir = tempfile.mkdtemp(prefix="irl_")
        cls.exe = os.path.join(cls.build_dir, "ir_learned_host")
        lib = os.path.join(REPO_ROOT, "lib", "infrared", "signal")
        subprocess.check_call(
            [
                "gcc", "-std=gnu11", "-g", "-O1", "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                "-Wall", "-Wextra", "-Werror", "-I", lib, "-o", cls.exe,
                os.path.join(REPO_ROOT, "scripts", "host_tests", "ir_learned_host.c"),
                os.path.join(lib, "infrared_learned.c"),
            ]
        )

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.build_dir, True)

    def run_host(self, text, *args):
        import subprocess

        r = subprocess.run([self.exe, *args], input=text, capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stderr[-1500:])
        return r.stdout

    def bump(self, text, library, button, key, weight=1):
        return self.run_host(text, "bump", library, button, f"{key:X}", str(weight))

    def find(self, text, library, button, limit=8):
        return [line.split() for line in self.run_host(text, "find", library, button, str(limit)).splitlines()]

    def test_bump_adds_then_raises(self):
        text = self.bump("", "tv.ir", "Power", 0x1A2B3C4D, 3)
        self.assertEqual(text, "tv.ir Power 1A2B3C4D 3\n")
        text = self.bump(text, "tv.ir", "Power", 0x1A2B3C4D, 1)
        self.assertEqual(text, "tv.ir Power 1A2B3C4D 4\n")

    def test_find_best_first_per_button_and_library(self):
        text = ""
        for library, button, key, weight in [
            ("tv.ir", "Power", 0x11, 1), ("tv.ir", "Power", 0x22, 3), ("tv.ir", "Mute", 0x33, 3),
            ("audio.ir", "Power", 0x44, 3), ("tv.ir", "Power", 0x55, 1), ("tv.ir", "Power", 0x11, 1),
        ]:
            text = self.bump(text, library, button, key, weight)
        self.assertEqual(self.find(text, "tv.ir", "Power"), [["00000022", "3"], ["00000011", "2"], ["00000055", "1"]])
        self.assertEqual(self.find(text, "tv.ir", "Power", 2), [["00000022", "3"], ["00000011", "2"]])
        self.assertEqual(self.find(text, "audio.ir", "Power"), [["00000044", "3"]])
        self.assertEqual(self.find(text, "tv.ir", "Vol_up"), [])

    def test_the_score_stops_growing(self):
        text = ""
        for _ in range(30):
            text = self.bump(text, "tv.ir", "Power", 0x11, 3)
        self.assertEqual(text, "tv.ir Power 00000011 50\n")

    def test_bad_lines_and_names(self):
        text = "junk\ntv.ir Power zz 3\ntv.ir Power 00000011 0\ntv.ir Power 00000022 7 extra\ntv.ir Mute 00000033 2\n"
        self.assertEqual(self.find(text, "tv.ir", "Mute"), [["00000033", "2"]])
        self.assertEqual(self.find(text, "tv.ir", "Power"), [])
        self.assertEqual(self.bump(text, "tv.ir", "Mute", 0x33, 1), "tv.ir Mute 00000033 3\n")
        self.assertEqual(self.bump("", "tv.ir", "Vol up", 0x33, 1), "NULL\n")       # a space cannot be stored
        self.assertEqual(self.bump("", "tv.ir", "B" * 40, 0x33, 1), "NULL\n")

    def test_a_full_file_drops_the_lowest(self):
        text = "".join(f"tv.ir B{i} {i:08X} {2 if i else 1}\n" for i in range(200))
        out = self.bump(text, "tv.ir", "New", 0xABC, 3)
        lines = out.splitlines()
        self.assertEqual(len(lines), 200)
        self.assertNotIn("tv.ir B0 00000000 1", lines)          # the lowest score made room
        self.assertEqual(lines[-1], "tv.ir New 00000ABC 3")

    def test_fuzz(self):
        import subprocess

        for seed in ("1", "77"):
            r = subprocess.run([self.exe, "fuzz", "3000", seed], capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stderr[-1500:])


class BatteryStatsTests(unittest.TestCase):
    """Battery History's graph and time-left estimate (applications/system/battery_history)"""

    @classmethod
    def setUpClass(cls):
        import subprocess

        if not _have_sanitizer("address,undefined"):
            raise unittest.SkipTest("gcc with AddressSanitizer is not available")
        cls.build_dir = tempfile.mkdtemp(prefix="bat_")
        cls.exe = os.path.join(cls.build_dir, "battery_stats_host")
        app = os.path.join(REPO_ROOT, "applications", "system", "battery_history")
        subprocess.check_call(
            [
                "gcc", "-std=gnu11", "-g", "-O1", "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                "-Wall", "-Wextra", "-Werror", "-I", app, "-o", cls.exe,
                os.path.join(REPO_ROOT, "scripts", "host_tests", "battery_stats_host.c"),
                os.path.join(app, "battery_stats.c"),
            ]
        )

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.build_dir, True)

    def run_host(self, records, *args):
        import subprocess

        text = "".join(f"{t} {c} {f}\n" for t, c, f in records)
        r = subprocess.run([self.exe, *args], input=text, capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stderr[-1500:])
        return r.stdout.split("\n")[:-1]

    @staticmethod
    def drain(start, hours, charge, per_hour, flags_first=2):
        # a record every 10 minutes, the charge falling per_hour % an hour, in whole percents
        records = []
        for i in range(hours * 6 + 1):
            records.append((start + i * 600, round(charge - per_hour * i / 6), flags_first if i == 0 else 0))
        return records

    def test_estimate_from_a_steady_drain(self):
        records = self.drain(1_000_000, 10, 90, 1.25)        # 30% a day, from 90% to 77.5%
        seconds, per_day_x10 = map(int, self.run_host(records, "estimate")[0].split())
        self.assertAlmostEqual(per_day_x10, 300, delta=8)
        self.assertAlmostEqual(seconds / 3600, records[-1][1] / 1.25, delta=1.5)

    def test_no_estimate_when_charging_short_or_flat(self):
        records = self.drain(1_000_000, 10, 90, 1.25)
        self.assertEqual(self.run_host(records + [(records[-1][0] + 600, 78, 1)], "estimate"), ["none"])
        self.assertEqual(self.run_host(self.drain(1_000_000, 1, 90, 3), "estimate"), ["none"])   # one hour
        self.assertEqual(self.run_host(self.drain(1_000_000, 10, 90, 0.1), "estimate"), ["none"])  # 1% drop
        self.assertEqual(self.run_host([], "estimate"), ["none"])

    def test_the_estimate_starts_after_charging_and_restarts(self):
        # fast drain, a charge, then a slow drain: only the slow one counts
        fast = self.drain(1_000_000, 6, 100, 5)
        charge = [(fast[-1][0] + 600 * (i + 1), 70 + i * 5, 1) for i in range(6)]
        slow = self.drain(charge[-1][0] + 600, 8, 100, 1, 0)
        seconds, per_day_x10 = map(int, self.run_host(fast + charge + slow, "estimate")[0].split())
        self.assertAlmostEqual(per_day_x10, 240, delta=10)
        # a start record (the Flipper was off) begins a new stretch: too short after it
        restart = self.drain(slow[-1][0] + 600, 1, 90, 1)
        self.assertEqual(self.run_host(fast + charge + slow + restart, "estimate"), ["none"])

    def test_columns_fill_between_records_but_not_across_gaps(self):
        records = [(0, 90, 2), (600, 89, 0), (1200, 88, 0), (9000, 80, 2), (9600, 79, 1)]
        out = self.run_host(records, "columns", "12000", "12000", "20")    # 600 s a column
        self.assertEqual(out[0:3], ["90", "89", "88"])
        self.assertEqual(out[3:15], ["-"] * 12)                          # off: not filled
        self.assertEqual(out[15:17], ["80", "79c"])
        self.assertEqual(out[17:], ["-"] * 3)
        # two columns a record apart are filled with the earlier charge
        out = self.run_host([(0, 50, 2), (1200, 49, 0)], "columns", "1200", "1200", "4")
        self.assertEqual(out, ["50", "50", "50", "49"])

    def test_duration(self):
        for seconds, text in [(35 * 60, "35 min"), (5 * 3600 + 20 * 60, "5 h 20 min"),
                              (30 * 3600, "1 day 6 h"), (52 * 3600, "2 days 4 h")]:
            self.assertEqual(self.run_host([], "duration", str(seconds)), [text])


class KeybindTests(unittest.TestCase):
    """The desktop's keybinds file (applications/services/desktop/helpers/desktop_keybinds.c)"""

    @classmethod
    def setUpClass(cls):
        import subprocess

        if not _have_sanitizer("address,undefined"):
            raise unittest.SkipTest("gcc with AddressSanitizer is not available")
        cls.build_dir = tempfile.mkdtemp(prefix="kb_")
        cls.exe = os.path.join(cls.build_dir, "keybinds_host")
        helpers = os.path.join(REPO_ROOT, "applications", "services", "desktop", "helpers")
        subprocess.check_call(
            [
                "gcc", "-std=gnu11", "-g", "-O1", "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                "-Wall", "-Wextra", "-Werror", "-I", helpers, "-o", cls.exe,
                os.path.join(REPO_ROOT, "scripts", "host_tests", "keybinds_host.c"),
                os.path.join(helpers, "desktop_keybinds.c"),
            ]
        )

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.build_dir, True)

    def parse(self, *lines):
        import subprocess

        r = subprocess.run([self.exe], input="".join(l + "\n" for l in lines), capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stderr[-1500:])
        return r.stdout.splitlines()

    def test_bindings_and_desktop_actions(self):
        out = self.parse(
            "up press: Lock Menu",
            "  OK HOLD :   main menu  ",
            "down hold: Nothing",
            "right press: Power Off",
            "left press:Archive",
            "up hold: lock",
        )
        self.assertEqual(out, [
            "up press|Lock Menu|2|-",
            "ok hold|main menu|1|-",
            "down hold|Nothing|6|-",
            "right press|Power Off|5|-",
            "left press|Archive|4|-",
            "up hold|lock|3|-",
        ])

    def test_apps_and_files(self):
        out = self.parse(
            "ok hold: Infrared",
            "left hold: /ext/subghz/garage.sub",
            "right hold: /ext/apps/Tools/battery_history.fap",
            "down press: /ext/badusb/demo.txt",
            "down hold: /ext/notes/demo.txt",
            "up press: /ext/scripts/blink.JS",
            "ok press: /ext/infrared/tv.ir",
        )
        self.assertEqual(out, [
            "ok hold|Infrared|0|-",
            "left hold|/ext/subghz/garage.sub|0|Sub-GHz",
            "right hold|/ext/apps/Tools/battery_history.fap|0|-",
            "down press|/ext/badusb/demo.txt|0|Bad USB",
            "down hold|/ext/notes/demo.txt|0|-",
            "up press|/ext/scripts/blink.JS|0|/ext/apps/assets/js_app.fap",
            "ok press|/ext/infrared/tv.ir|0|Infrared",
        ])

    def test_lines_that_are_not_bindings(self):
        out = self.parse(
            "# a comment: with a colon",
            "",
            "up: Lock",
            "up press:",
            "up press:    ",
            "sideways press: Lock",
            "up pressed: Lock",
        )
        self.assertEqual(out, ["-"] * 7)

    def test_fuzz(self):
        import subprocess

        for seed in ("3", "41"):
            r = subprocess.run([self.exe, "fuzz", "20000", seed], capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stderr[-1500:])


class FileSortTests(unittest.TestCase):
    """File list order (gui/modules/file_browser_sort.c) and Archive's search name match"""

    @classmethod
    def setUpClass(cls):
        import subprocess

        if not _have_sanitizer("address,undefined"):
            raise unittest.SkipTest("gcc with AddressSanitizer is not available")
        cls.build_dir = tempfile.mkdtemp(prefix="fs_")
        cls.exe = os.path.join(cls.build_dir, "file_sort_host")
        modules = os.path.join(REPO_ROOT, "applications", "services", "gui", "modules")
        search = os.path.join(REPO_ROOT, "applications", "main", "archive", "helpers", "archive_search.c")
        # only the match function of the search: the walk needs storage
        with open(search) as f:
            src = f.read()
        start = src.index("static char lower(")
        end = src.index("size_t archive_search(")
        match = os.path.join(cls.build_dir, "match.c")
        with open(match, "w") as f:
            f.write("#include <stdbool.h>\n#include <stddef.h>\n" + src[start:end])
        subprocess.check_call(
            [
                "gcc", "-std=gnu11", "-g", "-O1", "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                "-Wall", "-Wextra", "-Werror", "-I", modules, "-o", cls.exe,
                os.path.join(REPO_ROOT, "scripts", "host_tests", "file_sort_host.c"),
                os.path.join(modules, "file_browser_sort.c"),
                match,
            ]
        )

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.build_dir, True)

    ENTRIES = [
        ("zeta.sub", 0, 300), ("Alpha.ir", 0, 100), ("music", 1, 0), ("beta.nfc", 0, 900),
        ("Apps", 1, 0), (".hidden", 0, 50), ("gamma.sub", 0, 900), ("readme", 0, 10),
    ]

    def order(self, sort_by, mixed=0):
        import subprocess

        text = "".join(f"{n} {f} {k}\n" for n, f, k in self.ENTRIES)
        r = subprocess.run([self.exe, "sort", str(sort_by), str(mixed)], input=text, capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stderr[-1500:])
        return r.stdout.split()

    def test_name_is_the_old_order(self):
        self.assertEqual(self.order(0), ["Apps", "music", ".hidden", "Alpha.ir", "beta.nfc", "gamma.sub", "readme", "zeta.sub"])

    def test_name_reversed(self):
        self.assertEqual(self.order(1), ["music", "Apps", "zeta.sub", "readme", "gamma.sub", "beta.nfc", "Alpha.ir", ".hidden"])

    def test_newest_and_largest_by_key_then_name(self):
        expected = ["Apps", "music", "beta.nfc", "gamma.sub", "zeta.sub", "Alpha.ir", ".hidden", "readme"]
        self.assertEqual(self.order(2), expected)
        self.assertEqual(self.order(3), expected)

    def test_type_then_name(self):
        # no extension first (".hidden" has none: its only dot starts it), then .ir, .nfc, .sub
        self.assertEqual(self.order(4), ["Apps", "music", ".hidden", "readme", "Alpha.ir", "beta.nfc", "gamma.sub", "zeta.sub"])

    def test_folders_mixed_in(self):
        self.assertEqual(self.order(0, 1), [".hidden", "Alpha.ir", "Apps", "beta.nfc", "gamma.sub", "music", "readme", "zeta.sub"])

    def test_search_match(self):
        import subprocess

        def match(name, needle):
            return subprocess.run([self.exe, "match", name, needle], capture_output=True, text=True).stdout.strip()

        self.assertEqual(match("Garage_Door.sub", "door"), "1")
        self.assertEqual(match("Garage_Door.sub", "GARAGE_d"), "1")
        self.assertEqual(match("Garage_Door.sub", ".sub"), "1")
        self.assertEqual(match("Garage_Door.sub", "doors"), "0")
        self.assertEqual(match("ab", "abc"), "0")
        self.assertEqual(match("anything", ""), "0")


class SubGhzTextTests(unittest.TestCase):
    """Sub-GHz: what counts as the same signal, and editing the user frequency file"""

    @classmethod
    def setUpClass(cls):
        import subprocess

        if not _have_sanitizer("address,undefined"):
            raise unittest.SkipTest("gcc with AddressSanitizer is not available")
        cls.build_dir = tempfile.mkdtemp(prefix="sgt_")
        cls.exe = os.path.join(cls.build_dir, "subghz_text_host")
        helpers = os.path.join(REPO_ROOT, "applications", "main", "subghz", "helpers")
        settings = os.path.join(REPO_ROOT, "applications", "settings", "hadal_settings")
        subprocess.check_call(
            [
                "gcc", "-std=gnu11", "-g", "-O1", "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                "-Wall", "-Wextra", "-Werror", "-I", helpers, "-I", settings, "-o", cls.exe,
                os.path.join(REPO_ROOT, "scripts", "host_tests", "subghz_text_host.c"),
                os.path.join(helpers, "subghz_signal_hash.c"),
                os.path.join(settings, "subghz_freqs.c"),
            ]
        )

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.build_dir, True)

    def run_host(self, text, *args):
        import subprocess

        r = subprocess.run([self.exe, *args], input=text, capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stderr[-1500:])
        return r.stdout

    RECORD = (
        "Filetype: Flipper SubGhz Key File\nVersion: 1\nFrequency: 433920000\n"
        "Preset: FuriHalSubGhzPresetOok650Async\nProtocol: Princeton\nBit: 24\n"
        "Key: 00 00 00 00 00 95 D5 D4\nTE: 400\nGuard_time: 30\n"
    )

    def hash(self, text, chunk=None):
        return self.run_host(text, "hash", *([str(chunk)] if chunk else [])).strip()

    def test_the_same_signal_hashes_the_same_however_it_was_received(self):
        base = self.hash(self.RECORD)
        other = (self.RECORD.replace("433920000", "433420000").replace("TE: 400", "TE: 397")
                 .replace("Ok650", "Ok270").replace("Guard_time: 30", "Guard_time: 31"))
        self.assertEqual(self.hash(other), base)
        self.assertEqual(self.hash(self.RECORD.replace("\n", "\r\n")), base)
        self.assertEqual(self.hash(self.RECORD + "RSSI: -60\nLatitute: 1\nLongitude: 2\n"), base)

    def test_another_signal_hashes_differently(self):
        base = self.hash(self.RECORD)
        self.assertNotEqual(self.hash(self.RECORD.replace("D5 D4", "D5 D5")), base)
        self.assertNotEqual(self.hash(self.RECORD.replace("Princeton", "CAME")), base)
        self.assertNotEqual(self.hash(self.RECORD.replace("Bit: 24", "Bit: 25")), base)

    def test_chunks_do_not_change_the_hash(self):
        base = self.hash(self.RECORD)
        long_key = self.RECORD + "AVeryLongKeyNameThatDoesNotFitTheBuffer_ABCDEFGH: x\n"
        whole = self.hash(long_key)
        for chunk in (1, 2, 3, 7, 64):
            self.assertEqual(self.hash(self.RECORD, chunk), base)
            self.assertEqual(self.hash(long_key, chunk), whole)

    USER = (
        "Filetype: Flipper SubGhz Setting File\nVersion: 1\n# comment\n"
        "#Frequency: 300000000\nFrequency: 433920000\nHopper_frequency: 433920000\n"
        "Frequency: 868350000\nCustom_preset_name: FM95\n"
    )

    def test_list_and_standard(self):
        self.assertEqual(self.run_host(self.USER, "list").split("\n")[:-1], ["433920000", "868350000", "standard 1"])
        self.assertEqual(self.run_host("", "list").strip(), "standard 1")
        off = self.USER.replace("Version: 1\n", "Version: 1\nAdd_standard_frequencies: false\n")
        self.assertIn("standard 0", self.run_host(off, "list"))

    def test_add_after_the_last_and_not_twice(self):
        out = self.run_host(self.USER, "with", "315000000")
        lines = out.split("\n")
        self.assertEqual(lines[lines.index("Frequency: 868350000") + 1], "Frequency: 315000000")
        self.assertIn("Custom_preset_name: FM95", out)
        self.assertEqual(self.run_host(self.USER, "with", "433920000"), self.USER)
        new = self.run_host("", "with", "315000000")
        self.assertTrue(new.startswith("Filetype: Flipper SubGhz Setting File\nVersion: 1\n"))
        self.assertIn("Frequency: 315000000\n", new)

    def test_remove_keeps_everything_else(self):
        out = self.run_host(self.USER, "without", "433920000")
        self.assertNotIn("\nFrequency: 433920000", out)
        self.assertIn("Hopper_frequency: 433920000", out)
        self.assertIn("#Frequency: 300000000", out)
        self.assertIn("Frequency: 868350000", out)

    def test_standard_switch(self):
        out = self.run_host(self.USER, "standard", "0")
        self.assertIn("Version: 1\nAdd_standard_frequencies: false\n", out)
        again = self.run_host(out, "standard", "1")
        self.assertEqual(again.count("Add_standard_frequencies"), 1)
        self.assertIn("Add_standard_frequencies: true", again)


class DolphinLevelTests(unittest.TestCase):
    """Levels up to 30 (applications/services/dolphin/helpers/dolphin_state.c), the first three
    where they always were"""

    @classmethod
    def setUpClass(cls):
        import subprocess

        cls.build_dir = tempfile.mkdtemp(prefix="lvl_")
        cls.exe = os.path.join(cls.build_dir, "levels")
        with open(os.path.join(REPO_ROOT, "applications", "services", "dolphin", "helpers", "dolphin_state.c")) as f:
            src = f.read()
        block = src[src.index("/* Levels:"):src.index("void dolphin_state_on_deed(")]
        driver = (
            "#include <stdbool.h>\n#include <stdint.h>\n#include <stdio.h>\n"
            "#define LEVEL2_THRESHOLD 300\n#define LEVEL3_THRESHOLD 1800\n#define DOLPHIN_LEVEL_MAX 30\n"
            + block +
            "int main(void) {\n"
            "  for(uint32_t xp = 0; xp <= 80000; xp++)\n"
            "    printf(\"%u %u %d %u %u\\n\", xp, dolphin_get_level(xp), dolphin_state_is_levelup(xp),\n"
            "           dolphin_state_xp_to_levelup(xp), dolphin_state_xp_above_last_levelup(xp));\n"
            "  return 0;\n}\n"
        )
        path = os.path.join(cls.build_dir, "levels.c")
        with open(path, "w") as f:
            f.write(driver)
        subprocess.check_call(["gcc", "-std=gnu11", "-O1", "-Wall", "-Wextra", "-Werror", "-o", cls.exe, path])
        out = subprocess.run([cls.exe], capture_output=True, text=True, check=True).stdout
        cls.rows = [tuple(int(v) for v in line.split()) for line in out.splitlines()]

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.build_dir, True)

    def test_the_first_three_levels_did_not_move(self):
        level = {xp: lvl for xp, lvl, *_ in self.rows}
        self.assertEqual((level[0], level[300], level[301], level[1800], level[1801]), (1, 1, 2, 2, 3))

    def test_thirty_levels_one_after_another(self):
        levels = [lvl for _, lvl, *_ in self.rows]
        self.assertEqual(levels, sorted(levels))
        self.assertEqual(sorted(set(levels)), list(range(1, 31)))
        first_30 = next(xp for xp, lvl, *_ in self.rows if lvl == 30)
        self.assertEqual(first_30, 77401)

    def test_level_ups_happen_at_the_end_of_each_level(self):
        ups = [xp for xp, _, up, *_ in self.rows if up]
        self.assertEqual(len(ups), 29)
        self.assertEqual(ups[:3], [300, 1800, 3300])
        for xp, lvl, up, to_next, above in self.rows:
            if lvl < 30:
                self.assertEqual(up, to_next == 0, xp)
            else:
                self.assertFalse(up)
            # the XP into this level and the XP to the next add up to the level's size
            if lvl < 30 and xp > 0:
                size = to_next + above
                self.assertGreater(size, 0)


class ScreenPreviewTests(TempDirCase):
    """The app screens drawn by the firmware's own canvas on the host, under sanitizers"""

    def test_every_screen_draws(self):
        import glob
        import subprocess

        if not glob.glob(os.path.join(REPO_ROOT, "build", "f7-firmware-*", "assets", "compiled", "assets_icons.c")):
            raise unittest.SkipTest("the firmware has not been built: no compiled icons")
        if not _have_sanitizer("address"):
            raise unittest.SkipTest("gcc with AddressSanitizer is not available")
        from PIL import Image

        r = subprocess.run(
            [sys.executable, os.path.join(REPO_ROOT, "scripts", "screen_preview.py"), self.tmp],
            capture_output=True, text=True,
        )
        self.assertEqual(r.returncode, 0, r.stderr[-3000:])
        screens = [p for p in glob.glob(os.path.join(self.tmp, "*.png")) if "contact_sheet" not in p]
        self.assertGreaterEqual(len(screens), 14)
        for path in screens:
            image = Image.open(path).convert("L")
            self.assertEqual(image.size, (512, 256), path)
            self.assertLess(image.getextrema()[0], 50, f"{path} is blank")



class AnimationFramesFileTests(unittest.TestCase):
    """frames.bin: an animation's frames in one file, read by animation_storage.c"""

    def test_round_trip(self):
        blobs = [bytes([0]) + bytes([i] * 16) for i in range(5)] + [b"\x01\x00\x03\x00abc"]
        data = frames_file_pack(blobs)
        self.assertEqual(data[:4], b"HDF1")
        self.assertEqual(int.from_bytes(data[4:6], "little"), len(blobs))
        self.assertEqual(frames_file_unpack(data), blobs)

    def test_layout_is_what_the_firmware_reads(self):
        # magic, u16 count, u16 reserved, u16 size per frame, then the frames back to back
        data = frames_file_pack([b"\x00ab", b"\x00cde"])
        self.assertEqual(data, b"HDF1" + b"\x02\x00\x00\x00" + b"\x03\x00\x04\x00" + b"\x00ab\x00cde")

    def test_bad_files_are_refused(self):
        good = frames_file_pack([b"\x00ab", b"\x00cd"])
        for bad in (good[:-1], good + b"x", b"HDF2" + good[4:], good[:6]):
            with self.assertRaises(ValueError):
                frames_file_unpack(bad)
        with self.assertRaises(ValueError):
            frames_file_pack([])
        with self.assertRaises(ValueError):
            frames_file_pack([b""])


class ResourceSplitTests(unittest.TestCase):
    """update.py: resources that seldom change go in archives of their own, so qFlipper,
    which skips files the card already holds with the same MD5, uploads them once"""

    def setUp(self):
        import update

        self.main = update.Main.__new__(update.Main)
        self.big = update.Main.BIG_RESOURCE_SIZE

    def test_groups(self):
        # resources.ths keeps the Manifest (new with every build) and the files at the root;
        # everything else goes with the folder it is in, so a build re-uploads what changed
        group = self.main.resource_group
        self.assertIsNone(group("Manifest", 10**7))
        self.assertIsNone(group("readme.txt", 1000))
        self.assertEqual(group("dolphin/L1_x/frames.bin", 1000), "dolphin-files")
        self.assertEqual(group("dolphin/manifest.txt", 1000), "dolphin-files")
        self.assertEqual(group("infrared/assets/tv.ir", 1000), "infrared-files")
        self.assertEqual(group("apps_data/nfc/assets/x.nfc", 1000), "apps_data-nfc")
        self.assertEqual(group("asset_packs/Hadal/Icons/x.bmx", 1000), "asset_packs-Hadal")
        self.assertEqual(group("apps/GPIO/gpio.fap", 1000), "apps-GPIO")
        self.assertEqual(group("apps/GPIO/esp_flasher.fap", self.big), "esp_flasher")
        self.assertEqual(group("infrared/assets/tv.ir", self.big + 1), "tv")

    def test_small_groups_share_an_archive(self):
        small = self.main.SMALL_GROUP_SIZE
        groups = {"a-files": ["a/1", "a/2"], "b-files": ["b/1"], "apps-GPIO": ["apps/GPIO/x.fap"]}
        sizes = {"a/1": small // 2, "a/2": small // 2, "b/1": 10, "apps/GPIO/x.fap": 5}
        merged = self.main.merge_small_groups(groups, sizes)
        self.assertEqual(
            merged, {"a-files": ["a/1", "a/2"], "misc": ["b/1", "apps/GPIO/x.fap"]}
        )

    def test_archive_names_are_stable_and_safe(self):
        name = self.main.extra_archive_name
        self.assertEqual(name("apps-GPIO"), name("apps-GPIO"))
        self.assertNotEqual(name("apps-GPIO"), name("apps-NFC"))
        self.assertRegex(name("apps-Sub-GHz"), r"^res-apps-Sub-GHz-[0-9a-f]{6}\.ths$")
        self.assertRegex(name("we ird/na:me"), r"^res-we_ird_na_me-[0-9a-f]{6}\.ths$")

    def test_an_archive_unpacks_alone(self):
        import tarfile
        import heatshrink2
        from flipper.assets.heatshrink_stream import HeatshrinkDataStreamHeader
        from flipper.assets.tarball import compress_files_tarball

        with tempfile.TemporaryDirectory() as tmp:
            src = os.path.join(tmp, "src")
            os.makedirs(os.path.join(src, "apps", "GPIO"))
            for n in ("a.fap", "b.fap"):
                with open(os.path.join(src, "apps", "GPIO", n), "wb") as f:
                    f.write(n.encode() * 100)
            out = os.path.join(tmp, "x.ths")
            compress_files_tarball(src, ["apps/GPIO/a.fap", "apps/GPIO/b.fap"], out)
            with open(out, "rb") as f:
                header = HeatshrinkDataStreamHeader.unpack(f.read(7))
                body = f.read()
            raw = heatshrink2.decompress(
                body, window_sz2=header.window_size, lookahead_sz2=header.lookahead_size
            )
            with tarfile.open(fileobj=__import__("io").BytesIO(raw)) as tar:
                members = tar.getmembers()
            # the folders come first, so the archive unpacks on a card that lacks them
            self.assertEqual(
                [m.name for m in members], ["apps", "apps/GPIO", "apps/GPIO/a.fap", "apps/GPIO/b.fap"]
            )
            self.assertTrue(all(m.mtime == 0 for m in members))
            # and it is the same, byte for byte, when built again
            with open(out, "rb") as f:
                first = f.read()
            compress_files_tarball(src, ["apps/GPIO/a.fap", "apps/GPIO/b.fap"], out)
            with open(out, "rb") as f:
                self.assertEqual(f.read(), first)


if __name__ == "__main__":
    unittest.main()
