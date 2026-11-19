"""
Hadal asset packs: on-disk format, packer and validator.

A pack is a folder in /ext/asset_packs/<Name>/ with up to three parts:

    Anims/manifest.txt, Anims/<anim>/{meta.txt, frame_N.bm}   desktop animations
    Icons/<Category>/<Name>.bmx                               static icon
    Icons/<Category>/<Name>/{meta, frame_NN.bm}               animated icon
    Fonts/<Slot>.u8f                                          raw u8g2 font

Frame blob (what the firmware's compress_icon_decode() reads):
    0x00 + raw 1bpp bitmap                                    stored
    0x01 0x00 <u16 LE n> + heatshrink(w8, l4) payload, n bytes   compressed
The raw bitmap is XBM: rows padded to whole bytes, LSB first, bit set = pixel on.

.bmx   = <u32 LE width> <u32 LE height> + frame blob
meta   = <u32 LE width> <u32 LE height> <u32 LE frame_rate> <u32 LE frame_count>

Icons are matched to the firmware's built-in icons by the hash of their path
relative to Icons/ without the extension, e.g. "StatusBar/Battery_26x8"
(see icon_path_hash). A replacement must have the same size and frame count.
"""

import os
import re
import struct

# Font slots a pack may replace, in the order of the firmware's Font enum
FONT_SLOTS = ["Primary", "Secondary", "Keyboard", "BigNumbers", "BatteryPercent"]
# Compiled-in u8g2 font each slot normally uses (for metric comparison)
STOCK_FONTS = {
    "Primary": "u8g2_font_helvB08_tr",
    "Secondary": "u8g2_font_haxrcorp4089_tr",
    "Keyboard": "u8g2_font_profont11_mr",
    "BigNumbers": "u8g2_font_profont22_tn",
    "BatteryPercent": "u8g2_font_5x7_tr",
}

PNG_EXT = ".png"
BMX_EXT = ".bmx"
FRAME_NAME = "frame_%02d.bm"
ANIM_META_NAME = "meta"
HEATSHRINK_WINDOW = 8
HEATSHRINK_LOOKAHEAD = 4

# Largest icon the firmware will accept (a full screen)
MAX_ICON_DIM = 128
MAX_ICON_FRAMES = 32
# RAM budget the firmware enforces for pack icons (keep in sync with the C side)
ICON_RAM_BUDGET = 24 * 1024
# Per-icon and per-frame bookkeeping in RAM (Icon + frame pointers + index entry)
ICON_RAM_OVERHEAD = 24
FRAME_RAM_OVERHEAD = 4


class PackError(Exception):
    pass


def icon_path_hash(path: str) -> int:
    """FNV-1a 32 of a "Category/Name" path. The firmware computes the same hash."""
    h = 0x811C9DC5
    for b in path.encode("utf-8"):
        h ^= b
        h = (h * 0x01000193) & 0xFFFFFFFF
    return h


def _read(path: str, mode: str = "rb"):
    with open(path, mode) as f:
        return f.read()


_SIZE_SUFFIX = re.compile(r"^(.*)_(\d+x\d+)$")


def mask_twin(path: str):
    """The mask belonging to an icon, or the icon belonging to a mask ("X_mask_WxH" <->
    "X_WxH"). None for icons whose names do not end in a size."""
    m = _SIZE_SUFFIX.match(path)
    if not m:
        return None
    stem, size = m.groups()
    if stem.endswith("_mask"):
        return f"{stem[: -len('_mask')]}_{size}"
    return f"{stem}_mask_{size}"


def is_mask(path: str) -> bool:
    m = _SIZE_SUFFIX.match(path)
    return bool(m and m.group(1).endswith("_mask"))


def raw_size(width: int, height: int) -> int:
    return ((width + 7) // 8) * height


# ---------------------------------------------------------------------------
# Frame blobs
# ---------------------------------------------------------------------------


def _heatshrink_decompress(payload: bytes) -> bytes:
    import heatshrink2

    return heatshrink2.decompress(
        payload, window_sz2=HEATSHRINK_WINDOW, lookahead_sz2=HEATSHRINK_LOOKAHEAD
    )


def decode_blob(blob: bytes, width: int, height: int) -> bytes:
    """Return the raw XBM bitmap of a frame blob, checking it is well formed."""
    want = raw_size(width, height)
    if len(blob) < 1:
        raise PackError("empty frame")
    kind = blob[0]
    if kind == 0:
        raw = bytes(blob[1:])
    elif kind == 1:
        if len(blob) < 4 or blob[1] != 0:
            raise PackError("bad compressed frame header")
        payload_len = blob[2] | (blob[3] << 8)
        if len(blob) != 4 + payload_len:
            raise PackError(
                f"compressed frame length mismatch ({len(blob) - 4} != {payload_len})"
            )
        raw = _heatshrink_decompress(bytes(blob[4:]))
    else:
        raise PackError(f"unknown frame type 0x{kind:02x}")
    if len(raw) != want:
        raise PackError(f"frame has {len(raw)} bitmap bytes, {width}x{height} needs {want}")
    return raw


def raw_to_image(raw: bytes, width: int, height: int):
    """Raw XBM bitmap -> PIL image (black pixel where the bit is set)."""
    from PIL import Image

    row_bytes = (width + 7) // 8
    im = Image.new("1", (width, height), 1)
    px = im.load()
    for y in range(height):
        for x in range(width):
            if raw[y * row_bytes + (x >> 3)] & (1 << (x & 7)):
                px[x, y] = 0
    return im


def encode_png(path: str):
    """PNG -> (width, height, frame blob), using the same encoder as the firmware build."""
    from .icon import file2image

    image = file2image(path)
    return image.width, image.height, bytes(image.data)


# ---------------------------------------------------------------------------
# .bmx / meta
# ---------------------------------------------------------------------------


def bmx_pack(width: int, height: int, blob: bytes) -> bytes:
    return struct.pack("<II", width, height) + blob


def bmx_parse(data: bytes):
    if len(data) < 9:
        raise PackError("bmx too short")
    width, height = struct.unpack_from("<II", data)
    return width, height, bytes(data[8:])


def anim_meta_pack(width, height, frame_rate, frame_count) -> bytes:
    return struct.pack("<IIII", width, height, frame_rate, frame_count)


def anim_meta_parse(data: bytes):
    if len(data) != 16:
        raise PackError(f"animated icon meta must be 16 bytes, got {len(data)}")
    return struct.unpack("<IIII", data)


def _check_dims(width, height, where):
    if not (1 <= width <= MAX_ICON_DIM and 1 <= height <= MAX_ICON_DIM):
        raise PackError(f"{where}: size {width}x{height} out of range")


# ---------------------------------------------------------------------------
# u8g2 fonts
# ---------------------------------------------------------------------------

_ESCAPES = {
    "n": 10, "t": 9, "r": 13, "a": 7, "b": 8, "f": 12, "v": 11,
    "\\": 92, '"': 34, "'": 39, "?": 63,
}  # fmt: skip


def _unescape_c_string(body: str) -> bytes:
    out = bytearray()
    i = 0
    while i < len(body):
        c = body[i]
        if c != "\\":
            out.append(ord(c))
            i += 1
            continue
        i += 1
        c = body[i]
        if c in "01234567":
            j = i
            while j < len(body) and j < i + 3 and body[j] in "01234567":
                j += 1
            out.append(int(body[i:j], 8) & 0xFF)
            i = j
        elif c == "x":
            j = i + 1
            while j < len(body) and body[j] in "0123456789abcdefABCDEF":
                j += 1
            out.append(int(body[i + 1 : j], 16) & 0xFF)
            i = j
        elif c in _ESCAPES:
            out.append(_ESCAPES[c])
            i += 1
        else:
            raise PackError(f"unsupported escape \\{c} in font source")
    return bytes(out)


_FONT_DECL = re.compile(
    r"const uint8_t\s+(\w+)\[\d*\]\s*U8G2_FONT_SECTION\(\"[^\"]*\"\)\s*="
    r"((?:\s*\"(?:[^\"\\]|\\.)*\")+)\s*;"
)


def find_u8g2_font_source(source: str, name: str) -> str:
    """The full declaration of one font in a u8g2 .c file. The font data itself can contain
    ';' bytes, so the declaration is matched as a whole, never cut at the first ';'."""
    pos = source.find(f"const uint8_t {name}[")
    if pos < 0:
        raise PackError(f"no u8g2 font named {name}")
    m = _FONT_DECL.match(source, pos)
    if not m:
        raise PackError(f"could not parse the declaration of {name}")
    return m.group(0)


def extract_u8g2_font(source: str, name: str = None) -> bytes:
    """Bytes of a u8g2 font from its generated C source (string literal form)."""
    for m in _FONT_DECL.finditer(source):
        if name and m.group(1) != name:
            continue
        literals = re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(2))
        return _unescape_c_string("".join(literals)) + b"\0"
    raise PackError("no u8g2 font found in source" + (f" named {name}" if name else ""))


def font_info(font: bytes) -> dict:
    """Header fields of a u8g2 font that decide text layout."""
    if len(font) < 23:
        raise PackError("font shorter than its 23 byte header")

    def s8(v):
        return v - 256 if v > 127 else v

    return {
        "glyph_cnt": font[0],
        "max_char_width": s8(font[9]),
        "max_char_height": s8(font[10]),
        "x_offset": s8(font[11]),
        "y_offset": s8(font[12]),
        "ascent_A": s8(font[13]),
        "descent_g": s8(font[14]),
        "ascent_para": s8(font[15]),
        "descent_para": s8(font[16]),
    }


def stock_font_info(repo_root: str, slot: str) -> dict:
    """Header of the compiled-in font a slot normally uses, read from the repo source."""
    path = os.path.join(repo_root, "lib", "u8g2", "u8g2_fonts.c")
    name = STOCK_FONTS[slot]
    with open(path, "r", errors="replace") as f:
        text = f.read()
    return font_info(extract_u8g2_font(find_u8g2_font_source(text, name), name))


# ---------------------------------------------------------------------------
# Packing
# ---------------------------------------------------------------------------


def _log(log, msg):
    if log:
        log(msg)


def pack_icons(src_dir: str, out_dir: str, log=None) -> int:
    """Pack a tree laid out like assets/icons: <folders>/Name.png, and folders holding
    frame_rate and frame_*.png for animated icons. Folders may nest (e.g.
    BLE/BLE_HID/Ok_btn_9x9). Returns the icon count."""
    count = 0
    for dirpath, dirnames, filenames in os.walk(src_dir):
        dirnames.sort()
        filenames.sort()
        rel = os.path.relpath(dirpath, src_dir)
        if rel == ".":
            for f in filenames:
                if f.lower().endswith(PNG_EXT):
                    _log(log, f"skip  {f}: icons must be inside a category folder")
            continue
        if "frame_rate" in filenames:
            dirnames[:] = []  # an animated icon's folder only holds its frames
            _pack_animated_icon(dirpath, os.path.join(out_dir, rel), log)
            count += 1
            continue
        for entry in filenames:
            if not entry.lower().endswith(PNG_EXT):
                continue
            width, height, blob = encode_png(os.path.join(dirpath, entry))
            _check_dims(width, height, os.path.join(dirpath, entry))
            dest = os.path.join(out_dir, rel)
            os.makedirs(dest, exist_ok=True)
            name = entry[: -len(PNG_EXT)] + BMX_EXT
            with open(os.path.join(dest, name), "wb") as f:
                f.write(bmx_pack(width, height, blob))
            _log(log, f"icon  {rel}/{name} {width}x{height} {len(blob)}B")
            count += 1
    return count


def _pack_animated_icon(src: str, out: str, log) -> None:
    frame_rate = int(_read(os.path.join(src, "frame_rate"), "r").strip())
    frames = sorted(f for f in os.listdir(src) if f.lower().endswith(PNG_EXT))
    if frame_rate <= 0 or not frames:
        raise PackError(f"{src}: needs a positive frame_rate and at least one frame")
    if len(frames) > MAX_ICON_FRAMES:
        raise PackError(f"{src}: {len(frames)} frames, max {MAX_ICON_FRAMES}")
    os.makedirs(out, exist_ok=True)
    width = height = None
    for index, name in enumerate(frames):
        w, h, blob = encode_png(os.path.join(src, name))
        if width is None:
            width, height = w, h
            _check_dims(w, h, src)
        elif (w, h) != (width, height):
            raise PackError(f"{src}/{name}: size differs from the first frame")
        with open(os.path.join(out, FRAME_NAME % index), "wb") as f:
            f.write(blob)
    with open(os.path.join(out, ANIM_META_NAME), "wb") as f:
        f.write(anim_meta_pack(width, height, frame_rate, len(frames)))
    _log(log, f"anim  {os.path.basename(out)} {width}x{height} x{len(frames)} @{frame_rate}")


def pack_anims(src_dir: str, out_dir: str, log=None) -> int:
    """Pack dolphin animations with the firmware's own animation packer."""
    from .dolphin import Dolphin

    dolphin = Dolphin()
    dolphin.load(src_dir)
    dolphin.pack(out_dir)
    _log(log, f"anims {len(dolphin.manifest.animations)} -> {out_dir}")
    return len(dolphin.manifest.animations)


def pack_fonts(src_dir: str, out_dir: str, log=None) -> int:
    """Copy .u8f fonts, converting generated u8g2 .c sources. Returns the font count."""
    count = 0
    for entry in sorted(os.listdir(src_dir)):
        stem, ext = os.path.splitext(entry)
        if stem not in FONT_SLOTS:
            _log(log, f"skip  Fonts/{entry}: not one of {', '.join(FONT_SLOTS)}")
            continue
        path = os.path.join(src_dir, entry)
        if ext == ".u8f":
            data = _read(path)
        elif ext == ".c":
            data = extract_u8g2_font(_read(path, "r"))
        else:
            continue
        font_info(data)  # reject obviously broken data early
        os.makedirs(out_dir, exist_ok=True)
        with open(os.path.join(out_dir, stem + ".u8f"), "wb") as f:
            f.write(data)
        _log(log, f"font  {stem} {len(data)}B")
        count += 1
    return count


def pack_tree(src: str, out: str, log=None) -> dict:
    """Pack a source tree with Anims/, Icons/ and/or Fonts/ into a pack folder."""
    os.makedirs(out, exist_ok=True)
    result = {"anims": 0, "icons": 0, "fonts": 0}
    if os.path.isdir(os.path.join(src, "Anims")):
        result["anims"] = pack_anims(
            os.path.join(src, "Anims"), os.path.join(out, "Anims"), log
        )
    if os.path.isdir(os.path.join(src, "Icons")):
        result["icons"] = pack_icons(
            os.path.join(src, "Icons"), os.path.join(out, "Icons"), log
        )
    if os.path.isdir(os.path.join(src, "Fonts")):
        result["fonts"] = pack_fonts(
            os.path.join(src, "Fonts"), os.path.join(out, "Fonts"), log
        )
    return result


# ---------------------------------------------------------------------------
# Reading and validating a packed pack
# ---------------------------------------------------------------------------


def iter_pack_icons(pack_dir: str):
    """Yield (path, width, height, [frame blobs], frame_rate) for every icon in a pack;
    path is relative to Icons/ without the extension, e.g. "StatusBar/Battery_26x8".
    A folder holding a `meta` file is an animated icon, any other folder is a category.
    Raises PackError for a malformed icon."""
    icons = os.path.join(pack_dir, "Icons")
    if not os.path.isdir(icons):
        return
    for dirpath, dirnames, filenames in os.walk(icons):
        dirnames.sort()
        filenames.sort()
        rel = os.path.relpath(dirpath, icons).replace(os.sep, "/")
        if rel == ".":
            continue
        if ANIM_META_NAME in filenames:
            dirnames[:] = []
            width, height, rate, count = anim_meta_parse(
                _read(os.path.join(dirpath, ANIM_META_NAME))
            )
            if not (1 <= count <= MAX_ICON_FRAMES):
                raise PackError(f"{rel}: frame count {count} out of range")
            blobs = []
            for i in range(count):
                fp = os.path.join(dirpath, FRAME_NAME % i)
                if not os.path.isfile(fp):
                    raise PackError(f"{rel}: missing {FRAME_NAME % i}")
                blobs.append(_read(fp))
            yield rel, width, height, blobs, rate
            continue
        for entry in filenames:
            if entry.endswith(BMX_EXT):
                width, height, blob = bmx_parse(_read(os.path.join(dirpath, entry)))
                yield f"{rel}/{entry[: -len(BMX_EXT)]}", width, height, [blob], 0


def source_icon_index(icons_dir: str) -> dict:
    """Built-in icons as {"Category/Name": (width, height, frame_count)} derived from the
    source tree the firmware icons are compiled from (same walk and naming as packing)."""
    from PIL import Image

    index = {}
    for dirpath, dirnames, filenames in os.walk(icons_dir):
        dirnames.sort()
        filenames.sort()
        rel = os.path.relpath(dirpath, icons_dir).replace(os.sep, "/")
        if rel == ".":
            continue
        if "frame_rate" in filenames:
            dirnames[:] = []
            frames = sorted(f for f in filenames if f.endswith(PNG_EXT))
            with Image.open(os.path.join(dirpath, frames[0])) as im:
                index[rel] = (im.width, im.height, len(frames))
            continue
        for entry in filenames:
            if entry.endswith(PNG_EXT):
                with Image.open(os.path.join(dirpath, entry)) as im:
                    index[f"{rel}/{entry[: -len(PNG_EXT)]}"] = (im.width, im.height, 1)
    return index


def _read_keys(path: str) -> dict:
    """Flipper format "Key: value" lines (first value wins) - enough for meta checks."""
    keys = {}
    for line in _read(path, "r").splitlines():
        if ":" in line and not line.startswith("#"):
            k, v = line.split(":", 1)
            keys.setdefault(k.strip(), v.strip())
    return keys


def validate_packed_anims(anims_dir: str):
    """Check a packed Anims/ folder: manifest, every animation's meta, and that every
    frame file decodes to the size the meta declares. Returns (errors, animation count)."""
    errors = []
    manifest = os.path.join(anims_dir, "manifest.txt")
    if not os.path.isfile(manifest):
        return ["Anims: missing manifest.txt"], 0
    names = [
        line.split(":", 1)[1].strip()
        for line in _read(manifest, "r").splitlines()
        if line.startswith("Name:")
    ]
    if _read(manifest, "r").splitlines()[:1] != ["Filetype: Flipper Animation Manifest"]:
        errors.append("Anims/manifest.txt: wrong file type")
    if not names:
        errors.append("Anims/manifest.txt: lists no animations")
    for name in names:
        base = os.path.join(anims_dir, name)
        meta = os.path.join(base, "meta.txt")
        if not os.path.isfile(meta):
            errors.append(f"Anims/{name}: missing meta.txt")
            continue
        try:
            keys = _read_keys(meta)
            width, height = int(keys["Width"]), int(keys["Height"])
            order = [int(v) for v in keys["Frames order"].split()]
            _check_dims(width, height, f"Anims/{name}")
            if not order:
                raise PackError("empty frame order")
            for i in range(max(order) + 1):
                frame = os.path.join(base, f"frame_{i}.bm")
                if not os.path.isfile(frame):
                    raise PackError(f"missing frame_{i}.bm")
                decode_blob(_read(frame), width, height)
        except (KeyError, ValueError, PackError) as e:
            errors.append(f"Anims/{name}: {e}")
    return errors, len(names)


def validate_pack(pack_dir: str, icon_index: dict = None, repo_root: str = None) -> dict:
    """Check a pack the way the firmware will.

    icon_index maps path -> (width, height, frame_count) of the firmware's built-in
    icons (see the generated icon index); when given, every pack icon must match one.
    repo_root enables the font metric check. Returns {"errors", "warnings", "stats"}."""
    errors, warnings = [], []
    ram = 0
    icon_count = 0
    seen = {}
    paths = set()

    try:
        for path, width, height, blobs, rate in iter_pack_icons(pack_dir):
            icon_count += 1
            paths.add(path)
            try:
                _check_dims(width, height, path)
                for blob in blobs:
                    decode_blob(blob, width, height)
            except PackError as e:
                errors.append(f"Icons/{path}: {e}")
                continue
            ram += ICON_RAM_OVERHEAD + len(blobs) * FRAME_RAM_OVERHEAD
            ram += sum(len(b) for b in blobs)
            h = icon_path_hash(path)
            if h in seen:
                errors.append(f"Icons/{path}: hash collides with {seen[h]}")
            seen[h] = path
            if icon_index is not None:
                want = icon_index.get(path)
                if want is None:
                    warnings.append(f"Icons/{path}: no such built-in icon (ignored)")
                elif want != (width, height, len(blobs)):
                    errors.append(
                        f"Icons/{path}: is {width}x{height} x{len(blobs)}, "
                        f"firmware icon is {want[0]}x{want[1]} x{want[2]} (would be skipped)"
                    )
    except PackError as e:
        errors.append(str(e))

    # A mask is drawn together with its icon: replacing only one looks broken
    for path in sorted(paths):
        twin = mask_twin(path)
        if twin is None or twin in paths:
            continue
        if is_mask(path):
            warnings.append(f"Icons/{path}: mask replaced without its icon")
        elif icon_index and twin in icon_index:
            warnings.append(f"Icons/{path}: icon replaced without its mask")

    if ram > ICON_RAM_BUDGET:
        errors.append(
            f"icons need about {ram} B of RAM, the firmware loads at most {ICON_RAM_BUDGET} B"
        )

    fonts = os.path.join(pack_dir, "Fonts")
    font_bytes = 0
    if os.path.isdir(fonts):
        for entry in sorted(os.listdir(fonts)):
            stem, ext = os.path.splitext(entry)
            if ext != ".u8f":
                continue
            if stem not in FONT_SLOTS:
                warnings.append(f"Fonts/{entry}: not a font slot (ignored)")
                continue
            data = _read(os.path.join(fonts, entry))
            font_bytes += len(data)
            try:
                info = font_info(data)
                if data[-1] != 0:
                    errors.append(f"Fonts/{entry}: missing terminating zero byte")
                if repo_root:
                    stock = stock_font_info(repo_root, stem)
                    for key in ("max_char_height", "ascent_A", "descent_g"):
                        if info[key] != stock[key]:
                            errors.append(
                                f"Fonts/{entry}: {key} is {info[key]}, stock font has "
                                f"{stock[key]} (would be rejected: it changes layout)"
                            )
            except PackError as e:
                errors.append(f"Fonts/{entry}: {e}")

    anims = os.path.join(pack_dir, "Anims")
    anim_count = 0
    if os.path.isdir(anims):
        a_errors, anim_count = validate_packed_anims(anims)
        errors.extend(a_errors)

    return {
        "errors": errors,
        "warnings": warnings,
        "stats": {
            "icons": icon_count,
            "icon_ram": ram,
            "font_bytes": font_bytes,
            "anims": anim_count,
        },
    }
