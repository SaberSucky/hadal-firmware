#pragma once
/* previews draw the stock assets */
#include <stdint.h>
#include <gui/icon.h>
#define ASSET_PACK_FONT_SLOTS 5
static inline void asset_packs_set_stock_fonts(const uint8_t* const* fonts) {
    (void)fonts;
}
static inline const uint8_t* asset_packs_font(int font, const uint8_t* stock) {
    (void)font;
    return stock;
}
static inline const Icon* asset_packs_icon_acquire(const Icon* icon) {
    return icon;
}
static inline void asset_packs_icon_release(void) {
}
