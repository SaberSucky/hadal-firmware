/**
 * @file sonar.c
 * Sonar: Hadal's own main menu style. The chosen app in the middle at twice its size, inside a
 * sonar ring that pings outward; the apps either side on an orbit that dips toward the viewer;
 * its name in a pill under it. Left and Right (and Up and Down) turn the orbit.
 */
#include "menu_style_helpers.h"

#include <gui/icon_i.h>
#include <gui/icon_animation_i.h>
#include <toolbox/compress.h>

/** cos(a) * 1024 for a = 0, 6, 12 ... 354 degrees (sin(a) is cos(a - 90)): no maths library */
static const int16_t sonar_cos[60] = {
    1024,  1018,  1002,  974,  935,  887,  828,  761,  685,  602,  512,  416,  316,  213,   107,
    0,     -107,  -213,  -316, -416, -512, -602, -685, -761, -828, -887, -935, -974, -1002, -1018,
    -1024, -1018, -1002, -974, -935, -887, -828, -761, -685, -602, -512, -416, -316, -213,  -107,
    0,     107,   213,   316,  416,  512,  602,  685,  761,  828,  887,  935,  974,  1002,  1018};

static int32_t sonar_x(int32_t degrees, int32_t r) {
    return (sonar_cos[((degrees % 360 + 360) % 360) / 6] * r + (r >= 0 ? 512 : -512)) / 1024;
}

static int32_t sonar_y(int32_t degrees, int32_t r) {
    return sonar_x(degrees - 90, r);
}

#define SONAR_CX         64 // the centre of the orbit and of the chosen app
#define SONAR_CY         26
#define SONAR_ORBIT_RX   56 // the orbit, an ellipse: wide, shallow
#define SONAR_ORBIT_RY   16
#define SONAR_RING       18 // the ring round the chosen app
#define SONAR_PILL_TOP   52
#define SONAR_PILL_H     11
#define SONAR_ICON_MAX   32 // the largest icon drawn twice its size
#define SONAR_PING_STEPS 4 // a ping's rings, one each 333 ms

/** The chosen app's icon, twice its size, centred on (cx, cy) */
static void
    sonar_draw_icon_large(Canvas* canvas, IconAnimation* animation, int32_t cx, int32_t cy) {
    const Icon* icon = animation->icon;
    const int32_t width = icon->width;
    const int32_t height = icon->height;
    if(width > SONAR_ICON_MAX || height > SONAR_ICON_MAX) {
        canvas_draw_icon_animation(canvas, cx - width / 2, cy - height / 2, animation);
        return;
    }
    const uint8_t* data = icon->frames[animation->frame % icon->frame_count];
    CompressIcon* compress = compress_icon_alloc(SONAR_ICON_MAX * SONAR_ICON_MAX / 8);
    uint8_t* bitmap = NULL;
    compress_icon_decode(compress, data, &bitmap);
    if(bitmap) {
        const int32_t row_bytes = (width + 7) / 8;
        const int32_t left = cx - width;
        const int32_t top = cy - height;
        for(int32_t y = 0; y < height; y++) {
            for(int32_t x = 0; x < width; x++) {
                if(bitmap[y * row_bytes + x / 8] & (1 << (x % 8))) {
                    canvas_draw_box(canvas, left + x * 2, top + y * 2, 2, 2);
                }
            }
        }
    }
    compress_icon_free(compress);
}

/** A dotted circle, a dot every few degrees (a multiple of 6) */
static void sonar_dotted_circle(Canvas* canvas, int32_t cx, int32_t cy, int32_t r, int32_t step) {
    for(int32_t a = 0; a < 360; a += step) {
        canvas_draw_dot(canvas, cx + sonar_x(a, r), cy + sonar_y(a, r));
    }
}

static void menu_style_sonar_draw(Canvas* canvas, MenuModel* model) {
    const size_t count = model->count;
    const size_t position = model->position;

    // the orbit: dotted, its far half (behind the chosen app) left out
    for(int32_t a = 0; a <= 180; a += 6) {
        canvas_draw_dot(
            canvas, SONAR_CX + sonar_x(a, SONAR_ORBIT_RX), SONAR_CY + sonar_y(a, SONAR_ORBIT_RY));
    }

    // the apps either side: the nearer ones low on the orbit, the farther ones at its ends
    if(count > 1) {
        static const struct {
            int8_t offset;
            int16_t angle; // degrees along the orbit from its right end
        } places[] = {{-2, 180}, {2, 0}, {-1, 132}, {1, 48}};
        for(size_t i = 0; i < COUNT_OF(places); i++) {
            const int32_t offset = places[i].offset;
            if((size_t)(offset < 0 ? -offset : offset) * 2 >= count + (offset < 0 ? 0 : 1))
                continue;
            const size_t index = (position + count + offset) % count;
            const int32_t x = SONAR_CX + sonar_x(places[i].angle, SONAR_ORBIT_RX);
            const int32_t y = SONAR_CY + sonar_y(places[i].angle, SONAR_ORBIT_RY);
            // a clear patch, so the orbit does not run through the icon
            canvas_set_color(canvas, ColorWhite);
            canvas_draw_box(canvas, x - 8, y - 8, 16, 16);
            canvas_set_color(canvas, ColorBlack);
            menu_style_icon_centered(canvas, model->items[index].icon, x - 7, y - 7, 14, 14);
        }
    }

    // the chosen app: a clear disc, its ring, a ping going out from it, the icon twice its size
    canvas_set_color(canvas, ColorWhite);
    canvas_draw_disc(canvas, SONAR_CX, SONAR_CY, SONAR_RING);
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_circle(canvas, SONAR_CX, SONAR_CY, SONAR_RING);
    const size_t ping = model->scroll_counter % SONAR_PING_STEPS;
    if(ping) sonar_dotted_circle(canvas, SONAR_CX, SONAR_CY, SONAR_RING + ping * 3, 12 + ping * 6);
    sonar_draw_icon_large(canvas, model->items[position].icon, SONAR_CX, SONAR_CY);

    // its name, centred in a pill
    const char* label = menu_style_label(&model->items[position], true);
    canvas_set_font(canvas, FontSecondary);
    const int32_t text_width = canvas_string_width(canvas, label);
    const int32_t pill = MIN(text_width + 10, 124);
    const int32_t left = SONAR_CX - pill / 2;
    canvas_draw_rbox(canvas, left, SONAR_PILL_TOP, pill, SONAR_PILL_H, 3);
    canvas_set_color(canvas, ColorWhite);
    elements_scrollable_text_line_str(
        canvas,
        SONAR_CX,
        SONAR_PILL_TOP + SONAR_PILL_H - 2,
        pill - 8,
        label,
        menu_style_scroll(model, true),
        false,
        true);
    canvas_set_color(canvas, ColorBlack);
}

static size_t menu_style_sonar_navigate(MenuModel* model, InputKey key) {
    // the orbit turns with Left and Right, and with Up and Down as well
    if(key == InputKeyUp) key = InputKeyLeft;
    if(key == InputKeyDown) key = InputKeyRight;
    return menu_style_navigate_wrap(model, key);
}

static const MenuStyle menu_style_sonar = {
    .draw = menu_style_sonar_draw,
    .navigate = menu_style_sonar_navigate,
};

MENU_STYLE_PLUGIN(menu_style_sonar, menu_style_sonar_ep)
