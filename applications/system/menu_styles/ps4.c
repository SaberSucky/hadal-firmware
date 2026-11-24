/**
 * @file ps4.c
 * PS4 main menu style.
 *
 * Ported from Momentum Firmware (GPL-3.0), originally by Willy-JL (@WillyJL):
 * https://github.com/Next-Flip/Momentum-Firmware/commit/cef4a004f74fecca7556ea7aeb947897a1f392b5
 */
#include "menu_style_helpers.h"
#include <dolphin/dolphin.h>
#include <furi_hal_version.h>

// dolphin_stats() is a blocking round trip to another service, and this runs in the draw
// callback, on the GUI thread, under the model lock - cache it so it cannot stall every frame.
// A MenuStyle has no lifecycle hook to fetch it from instead. `known` is not redundant with
// next_refresh: that starts at 0, so on a first draw past ~24.8 days of uptime the tick
// comparison is already negative and the level would sit at 0 until the tick counter wrapped.
static uint8_t menu_style_ps4_level(void) {
    static uint32_t next_refresh;
    static uint8_t level;
    static bool known;
    if(!known || (int32_t)(furi_get_tick() - next_refresh) >= 0) {
        Dolphin* dolphin = furi_record_open(RECORD_DOLPHIN);
        level = dolphin_stats(dolphin).level;
        furi_record_close(RECORD_DOLPHIN);
        next_refresh = furi_get_tick() + furi_ms_to_ticks(5000);
        known = true;
    }
    return level;
}

#define PS4_PILL_HEIGHT 10 // the name's pill: a clear row over its capitals, one under descenders
#define PS4_PILL_PAD    3 // clear columns either side of the name

static void menu_style_ps4_draw(Canvas* canvas, MenuModel* model) {
    size_t position = model->position;
    size_t count = model->count;

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 1, 1, AlignLeft, AlignTop, furi_hal_version_get_name_ptr());
    char level[12];
    snprintf(level, sizeof(level), "Level %u", menu_style_ps4_level());
    canvas_draw_str_aligned(canvas, 127, 1, AlignRight, AlignTop, level);

    for(int32_t i = -1; i <= 4; i++) {
        size_t item_i = position + i;
        if(item_i >= count) continue;
        const MenuItem* item = &model->items[item_i];
        int32_t width = 20;
        int32_t height = 20;
        int32_t x = 36;
        int32_t y = 27;
        if(i == 0) {
            width += 10;
            height += 10;
            y += 2;
            // The app's name in the pill hanging from the selected tile (it said "Start", the
            // name beside it): as wide as the name, at least the tile's width, at most to the
            // screen's edge, where a longer name scrolls
            const char* label = menu_style_label(item, true);
            const int32_t left = x - width / 2;
            const int32_t top = y + height / 2 - 1; // over the tile's own bottom edge
            const int32_t room = canvas_width(canvas) - 1 - left;
            int32_t pill = (int32_t)canvas_string_width(canvas, label) + PS4_PILL_PAD * 2 + 1;
            if(pill < width) pill = width;
            if(pill > room) pill = room;
            canvas_draw_rbox(canvas, left, top, pill, PS4_PILL_HEIGHT, 2);
            canvas_set_color(canvas, ColorWhite);
            // a short name sits in the middle of a pill as wide as the tile
            const bool centred = pill == width;
            elements_scrollable_text_line_str(
                canvas,
                centred ? x : left + PS4_PILL_PAD,
                top + PS4_PILL_HEIGHT - 2,
                pill - PS4_PILL_PAD * 2,
                label,
                menu_style_scroll(model, true),
                false,
                centred);
            canvas_set_color(canvas, ColorBlack);
        } else {
            x += (width + 1) * i + (i < 0 ? -6 : 6);
        }
        canvas_draw_frame(canvas, x - width / 2, y - height / 2, width, height);
        menu_style_icon_centered(canvas, item->icon, x - 7, y - 7, 14, 14);
    }

    menu_style_scrollbar_horizontal(canvas, 0, 64, 128, position, count);
}

static size_t menu_style_ps4_navigate(MenuModel* model, InputKey key) {
    return menu_style_navigate_wrap(model, key);
}

static const MenuStyle menu_style_ps4 = {
    .draw = menu_style_ps4_draw,
    .navigate = menu_style_ps4_navigate,
};

MENU_STYLE_PLUGIN(menu_style_ps4, menu_style_ps4_ep)
