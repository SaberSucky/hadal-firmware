/**
 * @file asset_pack_format.h
 * Asset pack file formats: parsing and validation.
 *
 * Pure C with no firmware dependencies, so it can be compiled and tested on a host. See
 * documentation/AssetPacks.md for the formats and scripts/flipper/assets/pack.py for the
 * packer that writes them; the limits below are mirrored there.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Icons up to a full screen, as the canvas can decode them (its decode buffer is 1 KB). */
#define ASSET_PACK_MAX_ICON_DIM       128u
#define ASSET_PACK_MAX_RAW_BYTES      1024u
#define ASSET_PACK_MAX_ICON_FRAMES    32u
/** Decode buffer the canvas uses for icons (ICON_DECOMPRESSOR_BUFFER_SIZE + 4). */
#define ASSET_PACK_DECODE_BUFFER_SIZE (ASSET_PACK_MAX_RAW_BYTES + 4u)

/** RAM the firmware spends on pack icons: blobs plus this bookkeeping, at most the budget. */
#define ASSET_PACK_ICON_RAM_BUDGET    (24u * 1024u)
#define ASSET_PACK_ICON_OVERHEAD      24u
#define ASSET_PACK_FRAME_OVERHEAD     4u

typedef enum {
    AssetPackOk = 0,
    AssetPackErrTooShort,
    AssetPackErrBadType,
    AssetPackErrBadHeader,
    AssetPackErrLength,
    AssetPackErrDecode,
    AssetPackErrSize,
    AssetPackErrMetrics,
} AssetPackStatus;

/** FNV-1a 32 of a pack path such as "StatusBar/Battery_26x8". Matches the generated icon index. */
uint32_t asset_pack_path_hash(const char* path);

/** Bytes of a raw 1bpp bitmap: rows padded to whole bytes. */
size_t asset_pack_raw_size(uint32_t width, uint32_t height);

/** Static icon file: <u32 LE width> <u32 LE height> then one frame blob. */
typedef struct {
    uint32_t width;
    uint32_t height;
    const uint8_t* blob;
    size_t blob_size;
} AssetPackBmx;

AssetPackStatus asset_pack_bmx_parse(const uint8_t* data, size_t size, AssetPackBmx* out);

/** Animated icon `meta` file: four u32 LE values. */
typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t frame_rate;
    uint32_t frame_count;
} AssetPackAnimMeta;

AssetPackStatus
    asset_pack_anim_meta_parse(const uint8_t* data, size_t size, AssetPackAnimMeta* out);

/**
 * Decode a compressed frame (header included) into out.
 * @return true and the produced size in out_len on success, false if the data is invalid.
 * Must behave exactly like the decoder used to draw the icon.
 */
typedef bool (*AssetPackDecodeFn)(
    const uint8_t* blob,
    size_t blob_size,
    uint8_t* out,
    size_t out_capacity,
    size_t* out_len,
    void* context);

/**
 * Check that a frame blob will draw correctly at width x height.
 *
 * A stored frame (0x00 + bitmap) must be exactly that long. A compressed frame
 * (0x01 0x00 <u16 LE n> + n bytes) must have an accurate length and actually decode, into the
 * canvas's decode buffer, to exactly the bitmap size: drawing a frame that fails to decode
 * crashes the firmware, so this is the gate that keeps a bad file out of memory.
 *
 * @param scratch ASSET_PACK_DECODE_BUFFER_SIZE bytes of working space (compressed frames only)
 */
AssetPackStatus asset_pack_blob_check(
    const uint8_t* blob,
    size_t size,
    uint32_t width,
    uint32_t height,
    AssetPackDecodeFn decode,
    void* decode_context,
    uint8_t* scratch);

/** RAM a pack icon costs: bookkeeping plus its frames' blobs. */
size_t asset_pack_icon_ram_cost(size_t frame_count, size_t total_blob_bytes);

/** Font slots a pack can replace, in the order of the firmware's Font enum. */
#define ASSET_PACK_FONT_SLOTS        5u
/** One font file may not be larger than this, and a pack's fonts together stay within the budget. */
#define ASSET_PACK_MAX_FONT_BYTES    6144u
#define ASSET_PACK_FONT_RAM_BUDGET   12288u

/** Size of the u8g2 font header, which holds the bit widths and layout metrics. */
#define ASSET_PACK_FONT_HEADER_SIZE  23u

/** File name stem of a font slot ("Primary" for Fonts/Primary.u8f); NULL past the last slot. */
const char* asset_pack_font_slot_name(unsigned slot);

/** Font header fields that decide text layout (u8g2 font header offsets 9, 10, 13, 14). */
typedef struct {
    int8_t max_char_height;
    int8_t ascent_a;
    int8_t descent_g;
} AssetPackFontMetrics;

/** Read the metrics of a u8g2 font; false if it is shorter than a header. */
bool asset_pack_font_metrics(const uint8_t* font, size_t size, AssetPackFontMetrics* out);

/**
 * Check that a u8g2 font is safe to hand to the text renderer in place of the built-in font
 * it replaces.
 *
 * u8g2 trusts font data completely: a corrupt glyph makes its decoder read on past the end of
 * the font, up to a bus fault. So this walks the font the way u8g2 does and refuses anything
 * that could read outside it:
 *  - the layout metrics (height, ascent, descent) must equal the replaced font's, or every
 *    screen's layout would change;
 *  - the glyph chain must stay inside the data and end properly, and the header's shortcut
 *    offsets must point at glyphs;
 *  - every glyph is decoded with u8g2's own algorithm against a bit budget of exactly the bytes
 *    the glyph occupies, so a glyph that wants more bits than it has is rejected;
 *  - the Unicode section must be the empty one the built-in fonts have: characters 0-255 only.
 *
 * @param expected metrics of the built-in font this one replaces
 */
AssetPackStatus
    asset_pack_font_check(const uint8_t* font, size_t size, const AssetPackFontMetrics* expected);

#ifdef __cplusplus
}
#endif
