#include "updater_descent.h"

#include <stdio.h>

#define SCENE_TOP    12
#define SCENE_BOTTOM 50
#define SUB_X        52 /* the sphere's centre */
#define SEABED_TOP   46
#define SUB_REST     (SEABED_TOP - 22) /* its top when it rests on the seabed */

// Marine snow: where each speck starts; it rises as the bathyscaphe sinks
static const uint8_t snow[][2] = {
    {14, 3},  {22, 17}, {31, 8},  {38, 29}, {47, 2},  {52, 21}, {61, 33}, {69, 12},
    {76, 26}, {84, 6},  {90, 31}, {97, 15}, {105, 1}, {111, 24}, {118, 10}, {124, 35},
    {18, 34}, {27, 25}, {43, 13}, {80, 18}, {101, 30}, {114, 4}, {35, 37}, {66, 1},
};

static const char* zone_name(uint32_t depth) {
    if(depth < 200) return "Sunlight zone";
    if(depth < 1000) return "Twilight zone";
    if(depth < 4000) return "Midnight zone";
    if(depth < 6000) return "The abyss";
    return "Hadal zone";
}

// The bathyscaphe, its top at `top`: 22 rows from the rail to the bottom of the sphere
static void draw_bathyscaphe(Canvas* canvas, int32_t top, uint32_t frame) {
    // the float, its conning tower and rail
    canvas_draw_rbox(canvas, SUB_X - 18, top + 4, 37, 9, 4);
    canvas_draw_box(canvas, SUB_X - 4, top + 1, 9, 4);
    canvas_draw_line(canvas, SUB_X - 6, top, SUB_X + 7, top);
    // the crew's sphere and its porthole
    canvas_draw_box(canvas, SUB_X - 2, top + 13, 5, 2);
    canvas_draw_disc(canvas, SUB_X, top + 17, 5);
    canvas_set_color(canvas, ColorWhite);
    canvas_draw_line(canvas, SUB_X - 14, top + 8, SUB_X + 14, top + 8);
    for(int32_t x = SUB_X - 12; x <= SUB_X + 12; x += 6) {
        canvas_draw_dot(canvas, x, top + 6);
    }
    canvas_draw_disc(canvas, SUB_X - 1, top + 16, 2);
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_dot(canvas, SUB_X - 1, top + 16);
    // the propeller, turning
    canvas_draw_line(canvas, SUB_X + 19, top + 8, SUB_X + 21, top + 8);
    const int32_t blade = (frame % 2) ? 4 : 1;
    canvas_draw_line(canvas, SUB_X + 22, top + 8 - blade, SUB_X + 22, top + 8 + blade);
    // the searchlight: a beam reaching down and forward, flickering a little
    canvas_draw_box(canvas, SUB_X - 8, top + 19, 3, 2);
    for(int32_t k = 2; k < 24; k += 2) {
        const int32_t x = SUB_X - 8 - k;
        if(top + 21 + k / 3 < SCENE_BOTTOM) canvas_draw_dot(canvas, x, top + 21 + k / 3);
        if(top + 21 + k < SCENE_BOTTOM && (k + frame) % 6) canvas_draw_dot(canvas, x, top + 21 + k);
    }
    // bubbles rising off the float
    for(uint32_t b = 0; b < 3; b++) {
        const int32_t rise = (int32_t)((frame + b * 4) % 12);
        const int32_t y = top - 1 - rise;
        if(y > SCENE_TOP + 1) canvas_draw_circle(canvas, SUB_X + 10 + (int32_t)b * 3 + rise % 2, y, 1);
    }
}

void updater_descent_draw(Canvas* canvas, uint8_t progress, const char* status, uint32_t frame) {
    if(progress > 100) progress = 100;
    const uint32_t depth = (uint32_t)progress * UPDATER_DESCENT_DEPTH_M / 100U;
    char text[24];

    canvas_set_color(canvas, ColorBlack);
    // header: the name and how far along
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 9, "HADAL");
    const int32_t name_width = canvas_string_width(canvas, "HADAL");
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2 + name_width + 4, 9, "updating");
    snprintf(text, sizeof(text), "%u%%", progress);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 126, 9, AlignRight, AlignBottom, text);

    // marine snow rising past (it moves with the frames, and with the descent)
    const uint32_t height = SCENE_BOTTOM - SCENE_TOP - 1;
    for(size_t i = 0; i < sizeof(snow) / sizeof(snow[0]); i++) {
        const uint32_t shift = frame / (1 + i % 2) + depth / 120U;
        const int32_t y = SCENE_TOP + 1 + (int32_t)((snow[i][1] + height * 8 - shift % height) % height);
        canvas_draw_dot(canvas, snow[i][0], y);
    }

    // the depth gauge on the left: ticks, and a pointer at the depth
    canvas_draw_line(canvas, 6, SCENE_TOP + 1, 6, SCENE_BOTTOM - 1);
    for(int32_t y = SCENE_TOP + 1; y < SCENE_BOTTOM; y += 4) {
        canvas_draw_line(canvas, 3 + ((y - SCENE_TOP - 1) % 12 ? 1 : 0), y, 6, y);
    }
    const int32_t pointer = SCENE_TOP + 1 + (int32_t)progress * (SCENE_BOTTOM - SCENE_TOP - 2) / 100;
    canvas_draw_line(canvas, 8, pointer, 10, pointer - 2);
    canvas_draw_line(canvas, 8, pointer, 10, pointer + 2);
    canvas_draw_line(canvas, 10, pointer - 2, 10, pointer + 2);

    // the surface, while it is still in sight
    const int32_t surface = SCENE_TOP + 2 - (int32_t)(depth / 40U);
    if(surface > SCENE_TOP) {
        for(int32_t x = 12; x < 128; x++) {
            canvas_draw_dot(canvas, x, surface + (((x + (int32_t)frame) / 3) % 2));
        }
    }

    // the bathyscaphe: from just under the surface to resting on the bottom
    const int32_t sub_top = SCENE_TOP + 4 + (int32_t)progress * (SUB_REST - SCENE_TOP - 4) / 100;
    canvas_set_color(canvas, ColorWhite);
    canvas_draw_box(canvas, SUB_X - 19, sub_top - 1, 43, 24);
    canvas_set_color(canvas, ColorBlack);

    // the seabed rises into view for the last fifth, and the bathyscaphe settles on it
    if(progress > 80) {
        const int32_t rise = (progress - 80) * (SCENE_BOTTOM - SEABED_TOP) / 20;
        for(int32_t x = 12; x < 128; x++) {
            const int32_t bump = (x * 7 % 11 < 3) ? 1 : 0;
            const int32_t top = SCENE_BOTTOM - rise - bump;
            if(top < SCENE_BOTTOM) canvas_draw_line(canvas, x, top, x, SCENE_BOTTOM - 1);
        }
    }
    draw_bathyscaphe(canvas, sub_top, frame);

    // the depth, and the zone it is in
    canvas_set_color(canvas, ColorWhite);
    canvas_draw_box(canvas, 79, 13, 49, 31);
    canvas_set_color(canvas, ColorBlack);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 126, 15, AlignRight, AlignTop, "depth");
    if(depth >= 1000) {
        snprintf(text, sizeof(text), "%lu,%03lu m", (unsigned long)(depth / 1000), (unsigned long)(depth % 1000));
    } else {
        snprintf(text, sizeof(text), "%lu m", (unsigned long)depth);
    }
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 126, 25, AlignRight, AlignTop, text);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 126, 35, AlignRight, AlignTop, zone_name(depth));

    // what is being done, under a thin bar of the progress
    canvas_draw_line(canvas, 0, SCENE_BOTTOM + 1, 127, SCENE_BOTTOM + 1);
    canvas_draw_box(canvas, 0, SCENE_BOTTOM + 2, (size_t)progress * 128U / 100U, 2);
    if(status) canvas_draw_str_aligned(canvas, 64, 63, AlignCenter, AlignBottom, status);
}
