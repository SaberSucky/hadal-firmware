#include "asset_pack_format.h"

#include <string.h>

#define U8G2_FONT_HEADER_SIZE 23u

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

bool asset_pack_font_metrics(const uint8_t* font, size_t size, AssetPackFontMetrics* out) {
    if(size < U8G2_FONT_HEADER_SIZE) return false;
    out->max_char_height = (int8_t)font[10];
    out->ascent_a = (int8_t)font[13];
    out->descent_g = (int8_t)font[14];
    return true;
}
