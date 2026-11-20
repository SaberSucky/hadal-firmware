#include "asset_pack_format.h"

#include <string.h>

uint32_t asset_pack_path_hash(const char* path) {
    uint32_t hash = 0x811C9DC5u;
    for(const uint8_t* p = (const uint8_t*)path; *p; p++) {
        hash ^= *p;
        hash *= 0x01000193u;
    }
    return hash;
}

size_t asset_pack_raw_size(uint32_t width, uint32_t height) {
    return (size_t)((width + 7u) / 8u) * height;
}

static uint32_t read_u32_le(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

AssetPackStatus asset_pack_bmx_parse(const uint8_t* data, size_t size, AssetPackBmx* out) {
    // 8 byte header plus at least the one byte of a frame
    if(size < 9) return AssetPackErrTooShort;
    out->width = read_u32_le(data);
    out->height = read_u32_le(data + 4);
    out->blob = data + 8;
    out->blob_size = size - 8;
    return AssetPackOk;
}

AssetPackStatus
    asset_pack_anim_meta_parse(const uint8_t* data, size_t size, AssetPackAnimMeta* out) {
    if(size != 16) return AssetPackErrLength;
    out->width = read_u32_le(data);
    out->height = read_u32_le(data + 4);
    out->frame_rate = read_u32_le(data + 8);
    out->frame_count = read_u32_le(data + 12);
    return AssetPackOk;
}

AssetPackStatus asset_pack_blob_check(
    const uint8_t* blob,
    size_t size,
    uint32_t width,
    uint32_t height,
    AssetPackDecodeFn decode,
    void* decode_context,
    uint8_t* scratch) {
    if(width < 1 || width > ASSET_PACK_MAX_ICON_DIM || height < 1 ||
       height > ASSET_PACK_MAX_ICON_DIM) {
        return AssetPackErrSize;
    }
    const size_t raw = asset_pack_raw_size(width, height);
    if(raw > ASSET_PACK_MAX_RAW_BYTES) return AssetPackErrSize;
    if(size < 1) return AssetPackErrTooShort;

    if(blob[0] == 0) {
        // Stored: the bitmap follows the type byte and nothing else
        return size == 1 + raw ? AssetPackOk : AssetPackErrLength;
    }
    if(blob[0] != 1) return AssetPackErrBadType;

    if(size < 4 || blob[1] != 0) return AssetPackErrBadHeader;
    const size_t payload = (size_t)blob[2] | ((size_t)blob[3] << 8);
    if(size != 4 + payload) return AssetPackErrLength;

    size_t produced = 0;
    if(!decode(blob, size, scratch, ASSET_PACK_DECODE_BUFFER_SIZE, &produced, decode_context)) {
        return AssetPackErrDecode;
    }
    return produced == raw ? AssetPackOk : AssetPackErrSize;
}

size_t asset_pack_icon_ram_cost(size_t frame_count, size_t total_blob_bytes) {
    return ASSET_PACK_ICON_OVERHEAD + frame_count * ASSET_PACK_FRAME_OVERHEAD +
           total_blob_bytes;
}

static const char* const font_slot_names[ASSET_PACK_FONT_SLOTS] = {
    "Primary",
    "Secondary",
    "Keyboard",
    "BigNumbers",
    "BatteryPercent",
};

const char* asset_pack_font_slot_name(unsigned slot) {
    return slot < ASSET_PACK_FONT_SLOTS ? font_slot_names[slot] : NULL;
}

bool asset_pack_font_metrics(const uint8_t* font, size_t size, AssetPackFontMetrics* out) {
    if(size < ASSET_PACK_FONT_HEADER_SIZE) return false;
    out->max_char_height = (int8_t)font[10];
    out->ascent_a = (int8_t)font[13];
    out->descent_g = (int8_t)font[14];
    return true;
}

/* ------------------------------------------------------------------------------------------ */
/* u8g2 font validation                                                                        */
/* ------------------------------------------------------------------------------------------ */

/** Glyph entry: encoding byte, size byte, then at least one byte of glyph data. */
#define U8G2_MIN_GLYPH_ENTRY    3u
/** The Unicode section of a font without Unicode glyphs: table (length 4, last code 0xFFFF), end. */
static const uint8_t empty_unicode_section[6] = {0x00, 0x04, 0xFF, 0xFF, 0x00, 0x00};

typedef struct {
    const uint8_t* data;
    size_t bit_count;
    size_t position;
    bool exhausted;
} BitReader;

/** Same value u8g2_font_decode_get_unsigned_bits() returns, but never reads past the glyph. */
static uint8_t bit_read(BitReader* reader, uint8_t count) {
    uint8_t value = 0;
    for(uint8_t i = 0; i < count; i++) {
        if(reader->position >= reader->bit_count) {
            reader->exhausted = true;
            return 0;
        }
        const uint8_t bit = (reader->data[reader->position >> 3] >> (reader->position & 7u)) & 1u;
        value |= (uint8_t)(bit << i);
        reader->position++;
    }
    return value;
}

/** u8g2_font_decode_len() without the drawing: how a run of pixels moves the glyph cursor. */
static void run_step(uint8_t length, int8_t glyph_width, int8_t* x, int8_t* y) {
    uint8_t count = length;
    uint8_t local_x = (uint8_t)*x, local_y = (uint8_t)*y;
    for(;;) {
        uint8_t remaining = (uint8_t)glyph_width;
        remaining -= local_x;
        if(count < remaining) break;
        count -= remaining;
        local_x = 0;
        local_y++;
    }
    local_x += count;
    *x = (int8_t)local_x;
    *y = (int8_t)local_y;
}

/**
 * u8g2_font_decode_glyph() with a bit budget. The bit widths come from the font header and have
 * been checked to be 1..8; the data is exactly the glyph's bytes.
 */
static bool glyph_decodes_in_bounds(const uint8_t* font, const uint8_t* data, size_t length) {
    BitReader reader = {data, length * 8u, 0, false};
    const uint8_t bits_per_0 = font[2], bits_per_1 = font[3];

    const int8_t glyph_width = (int8_t)bit_read(&reader, font[4]);
    const int8_t glyph_height = (int8_t)bit_read(&reader, font[5]);
    bit_read(&reader, font[6]); // x offset
    bit_read(&reader, font[7]); // y offset
    bit_read(&reader, font[8]); // advance
    if(reader.exhausted) return false;

    if(glyph_width > 0) {
        int8_t x = 0, y = 0;
        for(;;) {
            const uint8_t zeros = bit_read(&reader, bits_per_0);
            const uint8_t ones = bit_read(&reader, bits_per_1);
            if(reader.exhausted) return false;
            do {
                run_step(zeros, glyph_width, &x, &y);
                run_step(ones, glyph_width, &x, &y);
            } while(bit_read(&reader, 1) != 0 && !reader.exhausted);
            if(reader.exhausted) return false;
            if(y >= glyph_height) break;
        }
    }
    return true;
}

static uint16_t read_u16_be(const uint8_t* p) {
    return (uint16_t)((p[0] << 8) | p[1]);
}

bool asset_pack_font_metrics_fit(
    const AssetPackFontMetrics* font,
    const AssetPackFontMetrics* expected) {
    // Packs made for other firmwares (Momentum's) use fonts a little smaller or larger than the
    // stock ones; screens are laid out with some room, so a few pixels either way still fit
    return font->max_char_height >= 4 &&
           font->max_char_height <= expected->max_char_height + ASSET_PACK_FONT_SLACK &&
           font->ascent_a >= 3 && font->ascent_a <= expected->ascent_a + ASSET_PACK_FONT_SLACK &&
           font->descent_g <= 0 && font->descent_g >= expected->descent_g - ASSET_PACK_FONT_SLACK;
}

AssetPackStatus
    asset_pack_font_check(const uint8_t* font, size_t size, const AssetPackFontMetrics* expected) {
    if(size < ASSET_PACK_FONT_HEADER_SIZE + 2 + sizeof(empty_unicode_section)) return AssetPackErrTooShort;
    if(size > ASSET_PACK_MAX_FONT_BYTES) return AssetPackErrSize;

    // Bit widths: u8g2 reads them as counts of 1..8 bits (0 would shift by 255)
    for(size_t i = 2; i <= 8; i++) {
        if(font[i] < 1 || font[i] > 8) return AssetPackErrBadHeader;
    }

    AssetPackFontMetrics metrics;
    asset_pack_font_metrics(font, size, &metrics);
    if(!asset_pack_font_metrics_fit(&metrics, expected)) return AssetPackErrMetrics;

    const size_t upper_a = read_u16_be(font + 17);
    const size_t lower_a = read_u16_be(font + 19);
    const size_t unicode = read_u16_be(font + 21);
    bool upper_a_found = upper_a == 0, lower_a_found = lower_a == 0;

    // The glyph chain: [encoding][size] entries until one with size 0
    size_t position = ASSET_PACK_FONT_HEADER_SIZE;
    for(;;) {
        if(position + 1 >= size) return AssetPackErrLength;
        const size_t entry_size = font[position + 1];
        if(position - ASSET_PACK_FONT_HEADER_SIZE == upper_a) upper_a_found = true;
        if(position - ASSET_PACK_FONT_HEADER_SIZE == lower_a) lower_a_found = true;
        if(entry_size == 0) break; // end of the 8-bit section
        if(entry_size < U8G2_MIN_GLYPH_ENTRY || position + entry_size > size) {
            return AssetPackErrLength;
        }
        if(!glyph_decodes_in_bounds(font, font + position + 2, entry_size - 2)) {
            return AssetPackErrDecode;
        }
        position += entry_size;
    }
    if(!upper_a_found || !lower_a_found) return AssetPackErrBadHeader;

    // After the end marker comes the Unicode section; only the empty one is accepted
    const size_t section = position + 2;
    if(unicode != section - ASSET_PACK_FONT_HEADER_SIZE) return AssetPackErrBadHeader;
    if(section + sizeof(empty_unicode_section) > size) return AssetPackErrLength;
    for(size_t i = 0; i < sizeof(empty_unicode_section); i++) {
        if(font[section + i] != empty_unicode_section[i]) return AssetPackErrBadHeader;
    }
    return AssetPackOk;
}
