#include "preview.h"
#include "../../applications/system/updater/views/updater_descent.h"

static uint8_t progress;
static const char* status;
static uint32_t frame;

static void draw(Canvas* canvas, void* context) {
    UNUSED(context);
    updater_descent_draw(canvas, progress, status, frame);
}

int main(int argc, char** argv) {
    if(argc > 1) preview_set_dir(argv[1]);
    static const struct {
        uint8_t progress;
        const char* status;
        const char* name;
    } shots[] = {
        {0, "Waiting for SD card", "updater_descent_start"},
        {12, "Writing flash", "updater_descent_writing"},
        {47, "Radio stack: writing", "updater_descent_radio"},
        {71, "Extracting resources", "updater_descent_resources"},
        {93, "Cleaning up directories", "updater_descent_cleanup"},
        {100, "Completed!", "updater_descent_done"},
    };
    for(size_t i = 0; i < sizeof(shots) / sizeof(shots[0]); i++) {
        progress = shots[i].progress;
        status = shots[i].status;
        frame = (uint32_t)i * 3;
        preview_draw(shots[i].name, draw, NULL);
    }
    // every progress and many frames, unsaved: for the sanitizers
    Canvas* canvas = preview_canvas();
    for(unsigned p = 0; p <= 255; p++) {
        for(uint32_t f = 0; f < 40; f++) {
            canvas_reset(canvas);
            updater_descent_draw(canvas, (uint8_t)p, p % 3 ? "Extracting resources, a very long status" : NULL, f * 97);
        }
    }
    updater_descent_draw(canvas, 50, NULL, 0xFFFFFFFFu);
    return 0;
}
