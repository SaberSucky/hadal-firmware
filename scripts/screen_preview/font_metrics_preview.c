/* The fonts' rows around the baseline, as the real canvas draws them: for laying out with room.
 * Prints, for each font, the rows (relative to the y given to canvas_draw_str) that capitals,
 * descenders and brackets reach. */
#include "preview.h"
#include <gui/hadal_title_font.h>
#include <stdio.h>

static void rows(Canvas* canvas, const char* name, const char* text) {
    canvas_clear(canvas);
    canvas_draw_str(canvas, 2, 30, text);
    preview_save(canvas, "metric_tmp");
    // read back the frame buffer through the canvas: u8g2 tile buffer
    uint8_t* buf = canvas_get_buffer(canvas);
    int top = 99, bottom = -99;
    for(int y = 0; y < 64; y++) {
        for(int x = 0; x < 128; x++) {
            if(buf[(y / 8) * 128 + x] & (1 << (y % 8))) {
                if(y - 30 < top) top = y - 30;
                if(y - 30 > bottom) bottom = y - 30;
            }
        }
    }
    printf("%s %-8s rows %d..%d\n", name, text, top, bottom);
}

int main(int argc, char** argv) {
    if(argc > 1) preview_set_dir(argv[1]);
    Canvas* canvas = preview_canvas();
    const char* samples[] = {"HWAM", "dlkth", "gjpqy", "()|", "Ok"};
    canvas_set_font(canvas, FontSecondary);
    for(size_t i = 0; i < 5; i++) rows(canvas, "Secondary", samples[i]);
    canvas_set_font(canvas, FontPrimary);
    for(size_t i = 0; i < 5; i++) rows(canvas, "Primary", samples[i]);
    canvas_set_font(canvas, FontBigNumbers);
    rows(canvas, "BigNumbers", "0123456789:");
    canvas_set_custom_u8g2_font(canvas, hadal_title_font);
    rows(canvas, "Title", "HADAL");
    return 0;
}
