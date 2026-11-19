"""Reading and writing u8g2 fonts.

The firmware draws text with u8g2, whose font format is a run-length coded bitmap per glyph. This
module turns a font into glyph bitmaps and back, so a font can be redrawn or restyled by script.
It covers the 8-bit part of the format (characters 0 to 255) which is what asset packs may carry;
see lib/asset_packs/asset_pack_format.c for how the firmware checks such a font.

Layout, as u8g2 reads it:

    header, 23 bytes: glyph count, bbox mode, bits for zero runs, bits for one runs, bits for
        glyph width / height / x offset / y offset / advance, then max width, max height, x and y
        offset of the font box, ascent of 'A', descent of 'g', ascent and descent of '(',
        big-endian offsets of the first glyph from 'A' and from 'a', and of the Unicode section
    glyphs: [encoding][record size] then the glyph bits, in ascending order of encoding
    end of glyphs: [0][0]
    Unicode section: 00 04 FF FF 00 00 (empty), and the zero byte that ends the array

A glyph's bits, least significant bit of each byte first: width, height, x offset, y offset
(signed: stored plus half the range) and advance (signed), then pairs of run lengths, zeros then
ones, filling the width x height box row by row. After a pair comes a one bit meaning "the same
pair again" or a zero bit meaning "next pair". The box's top left sits at
(x + x offset, baseline - height - y offset).
"""

HEADER_SIZE = 23
UNICODE_SECTION = bytes([0, 4, 255, 255, 0, 0])


class FontError(Exception):
    pass


class Glyph:
    """One character: pixels[row][column] is 1 where the pixel is lit."""

    def __init__(self, encoding, pixels, x_offset, y_offset, advance):
        self.encoding = encoding
        self.pixels = [list(row) for row in pixels]
        self.x_offset = x_offset
        self.y_offset = y_offset
        self.advance = advance

    @property
    def width(self):
        return len(self.pixels[0]) if self.pixels else 0

    @property
    def height(self):
        return len(self.pixels)

    def copy(self):
        return Glyph(self.encoding, self.pixels, self.x_offset, self.y_offset, self.advance)

    def key(self):
        return (self.encoding, self.pixels, self.x_offset, self.y_offset, self.advance)

    def __eq__(self, other):
        return isinstance(other, Glyph) and self.key() == other.key()

    def rows(self):
        """The bitmap as strings, # for lit, handy for reading and for tests."""
        return ["".join("#" if p else "." for p in row) for row in self.pixels]


class Font:
    def __init__(self, header, glyphs):
        self.header = bytes(header)
        self.glyphs = dict(glyphs)  # encoding -> Glyph

    # ---- header fields that decide layout ----

    @staticmethod
    def _s8(v):
        return v - 256 if v > 127 else v

    @property
    def max_char_height(self):
        return self._s8(self.header[10])

    @property
    def ascent(self):
        return self._s8(self.header[13])

    @property
    def descent(self):
        return self._s8(self.header[14])

    def text_width(self, text):
        return sum(self.glyphs[ord(c)].advance for c in text if ord(c) in self.glyphs)

    def draw_text(self, image, x, baseline, text, lit=0):
        """Draw text onto a PIL image, advancing from x; lit is the pixel value of a lit pixel.
        Returns the x after the last character."""
        px = image.load()
        for c in text:
            glyph = self.glyphs.get(ord(c))
            if glyph is None:
                continue
            top = baseline - glyph.height - glyph.y_offset
            for row, line in enumerate(glyph.pixels):
                for col, on in enumerate(line):
                    gx, gy = x + glyph.x_offset + col, top + row
                    if on and 0 <= gx < image.width and 0 <= gy < image.height:
                        px[gx, gy] = lit
            x += glyph.advance
        return x


# ---------------------------------------------------------------------------
# Bits
# ---------------------------------------------------------------------------


class _BitReader:
    def __init__(self, data):
        self.data = data
        self.position = 0

    def unsigned(self, count):
        value = 0
        for i in range(count):
            if self.position >= len(self.data) * 8:
                raise FontError("glyph data ends in the middle of a field")
            bit = (self.data[self.position >> 3] >> (self.position & 7)) & 1
            value |= bit << i
            self.position += 1
        return value

    def signed(self, count):
        return self.unsigned(count) - (1 << (count - 1))


class _BitWriter:
    def __init__(self):
        self.bits = []

    def unsigned(self, value, count):
        if not 0 <= value < (1 << count):
            raise FontError(f"{value} does not fit in {count} bits")
        self.bits.extend((value >> i) & 1 for i in range(count))

    def signed(self, value, count):
        self.unsigned(value + (1 << (count - 1)), count)

    def to_bytes(self):
        out = bytearray((len(self.bits) + 7) // 8)
        for i, bit in enumerate(self.bits):
            if bit:
                out[i >> 3] |= 1 << (i & 7)
        return bytes(out)


# ---------------------------------------------------------------------------
# Decoding
# ---------------------------------------------------------------------------


def _decode_glyph(encoding, data, widths):
    bits0, bits1, bits_w, bits_h, bits_x, bits_y, bits_d = widths
    reader = _BitReader(data)
    width = reader.unsigned(bits_w)
    height = reader.unsigned(bits_h)
    x_offset = reader.signed(bits_x)
    y_offset = reader.signed(bits_y)
    advance = reader.signed(bits_d)

    pixels = [[0] * width for _ in range(height)]
    if width > 0:
        x = y = 0
        while y < height:
            zeros = reader.unsigned(bits0)
            ones = reader.unsigned(bits1)
            while True:
                for count, lit in ((zeros, 0), (ones, 1)):
                    for _ in range(count):
                        if y < height:
                            pixels[y][x] = lit
                        x += 1
                        if x == width:
                            x = 0
                            y += 1
                if reader.unsigned(1) == 0:
                    break
    return Glyph(encoding, pixels, x_offset, y_offset, advance)


def parse_font(data):
    """Decode the 8-bit glyphs of a u8g2 font."""
    if len(data) < HEADER_SIZE:
        raise FontError("shorter than the 23 byte header")
    widths = tuple(data[2:9])
    if any(not 1 <= w <= 8 for w in widths):
        raise FontError("header bit widths out of range")
    glyphs = {}
    position = HEADER_SIZE
    while True:
        if position + 1 >= len(data):
            raise FontError("glyph list runs off the end")
        size = data[position + 1]
        if size == 0:
            break
        if size < 3 or position + size > len(data):
            raise FontError("bad glyph record size")
        encoding = data[position]
        glyphs[encoding] = _decode_glyph(encoding, data[position + 2 : position + size], widths)
        position += size
    return Font(data[:HEADER_SIZE], glyphs)


# ---------------------------------------------------------------------------
# Encoding
# ---------------------------------------------------------------------------


def _runs(glyph):
    """The (zeros, ones) pairs that draw the glyph, runs longer than a pair can hold cut up later."""
    flat = [p for row in glyph.pixels for p in row]
    runs = []  # alternating zero, one, zero, one ... lengths
    current, length = 0, 0
    for p in flat:
        if p == current:
            length += 1
        else:
            runs.append(length)
            current, length = p, 1
    runs.append(length)
    if len(runs) % 2:
        runs.append(0)  # always zeros then ones
    return [(runs[i], runs[i + 1]) for i in range(0, len(runs), 2)]


def _split_pairs(pairs, max0, max1):
    """Cut runs that are too long into several pairs: a zero run of 20 with room for 15 is (15,0)
    then (5, ...), and the same for ones as (0,max) pairs."""
    out = []
    for zeros, ones in pairs:
        while zeros > max0:
            out.append((max0, 0))
            zeros -= max0
        while ones > max1:
            out.append((zeros, max1))
            zeros = 0
            ones -= max1
        out.append((zeros, ones))
    return out


def _glyph_bits(glyph, bits, pair_bits):
    bits_w, bits_h, bits_x, bits_y, bits_d = bits
    bits0, bits1 = pair_bits
    writer = _BitWriter()
    writer.unsigned(glyph.width, bits_w)
    writer.unsigned(glyph.height, bits_h)
    writer.signed(glyph.x_offset, bits_x)
    writer.signed(glyph.y_offset, bits_y)
    writer.signed(glyph.advance, bits_d)
    if glyph.width > 0 and glyph.height > 0:
        pairs = _split_pairs(_runs(glyph), (1 << bits0) - 1, (1 << bits1) - 1)
        i = 0
        while i < len(pairs):
            repeat = 1
            while i + repeat < len(pairs) and pairs[i + repeat] == pairs[i]:
                repeat += 1
            writer.unsigned(pairs[i][0], bits0)
            writer.unsigned(pairs[i][1], bits1)
            for _ in range(repeat - 1):
                writer.unsigned(1, 1)
            writer.unsigned(0, 1)
            i += repeat
    data = writer.to_bytes()
    return data if len(data) >= 1 else b"\0"


def _signed_bits(values):
    bits = 1
    while not all(-(1 << (bits - 1)) <= v < (1 << (bits - 1)) for v in values):
        bits += 1
    return bits


def _unsigned_bits(values):
    return max(1, max(values).bit_length())


def serialize_font(font):
    """Encode a Font as u8g2 reads it. The header's layout fields (box, ascent, descent) are kept
    as they are: set them on font.header first if the glyphs need a different box."""
    glyphs = [font.glyphs[e] for e in sorted(font.glyphs)]
    if not glyphs:
        raise FontError("no glyphs")
    for g in glyphs:
        if not 0 <= g.encoding <= 255:
            raise FontError(f"encoding {g.encoding} is outside 0-255")
        if g.width and len({len(r) for r in g.pixels}) != 1:
            raise FontError(f"glyph {g.encoding} has rows of different lengths")

    bits = (
        _unsigned_bits([g.width for g in glyphs]),
        _unsigned_bits([g.height for g in glyphs]),
        _signed_bits([g.x_offset for g in glyphs]),
        _signed_bits([g.y_offset for g in glyphs]),
        _signed_bits([g.advance for g in glyphs]),
    )
    if any(b > 8 for b in bits):
        raise FontError("a glyph field needs more than 8 bits")

    best = None
    for bits0 in range(1, 9):
        for bits1 in range(1, 9):
            records = [_glyph_bits(g, bits, (bits0, bits1)) for g in glyphs]
            total = sum(len(r) + 2 for r in records)
            if best is None or total < best[0]:
                best = (total, bits0, bits1, records)
    _, bits0, bits1, records = best
    if any(len(r) + 2 > 255 for r in records):
        raise FontError("a glyph record is longer than 255 bytes")

    body = bytearray()
    upper_a = lower_a = None
    for g, record in zip(glyphs, records):
        if upper_a is None and g.encoding >= ord("A"):
            upper_a = len(body)
        if lower_a is None and g.encoding >= ord("a"):
            lower_a = len(body)
        body += bytes([g.encoding, len(record) + 2]) + record
    end = len(body)
    upper_a = end if upper_a is None else upper_a
    lower_a = end if lower_a is None else lower_a
    body += bytes([0, 0])
    unicode_at = len(body)

    header = bytearray(font.header)
    header[0] = len(glyphs)
    header[2], header[3] = bits0, bits1
    header[4:9] = bytes(bits)
    for offset, value in ((17, upper_a), (19, lower_a), (21, unicode_at)):
        if value > 0xFFFF:
            raise FontError("font too large")
        header[offset], header[offset + 1] = value >> 8, value & 0xFF
    return bytes(header) + bytes(body) + UNICODE_SECTION
