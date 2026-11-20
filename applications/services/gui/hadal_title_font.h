#pragma once

#include <stdint.h>

/** Hadal's title font (u8g2, for canvas_set_custom_u8g2_font()): bold capitals, 9 pixels tall */
extern const uint8_t hadal_title_font[];

/** Height of its capitals, which is all it has: the baseline is this far below the top */
#define HADAL_TITLE_FONT_HEIGHT 9
