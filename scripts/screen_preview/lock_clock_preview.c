#include "preview.h"
#include "../../applications/services/desktop/helpers/desktop_lock_clock.h"

static DateTime when;
static bool hour_12;

static void draw(Canvas* canvas, void* context) {
    UNUSED(context);
    // something behind it, as the animation would be
    for(int y = 12; y < 64; y += 3) {
        for(int x = (y / 3) % 2; x < 128; x += 5) canvas_draw_dot(canvas, x, y);
    }
    canvas_draw_disc(canvas, 96, 40, 18);
    desktop_lock_clock_draw(canvas, &when, hour_12, !hour_12);
}

int main(int argc, char** argv) {
    if(argc > 1) preview_set_dir(argv[1]);
    when = (DateTime){.hour = 9, .minute = 41, .second = 0, .day = 20, .month = 11, .year = 2026, .weekday = 5};
    preview_draw("lock_clock_24h", draw, NULL);
    hour_12 = true;
    when.hour = 21;
    preview_draw("lock_clock_12h", draw, NULL);
    when = (DateTime){.hour = 23, .minute = 59, .day = 31, .month = 12, .year = 2026, .weekday = 4};
    hour_12 = false;
    preview_draw("lock_clock_late", draw, NULL);
    // anything the RTC could hold, for the sanitizers
    Canvas* canvas = preview_canvas();
    for(unsigned v = 0; v < 256; v++) {
        DateTime odd = {.hour = v, .minute = v, .day = v, .month = v, .weekday = v, .year = 2026};
        desktop_lock_clock_draw(canvas, &odd, v % 2, v % 2);
    }
    return 0;
}
