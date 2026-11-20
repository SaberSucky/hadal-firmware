#include "preview.h"
#include "../../applications/services/desktop/views/desktop_view_control_centre.c"

static ControlCentreModel model;

static void draw(Canvas* canvas, void* context) {
    UNUSED(context);
    control_centre_draw_callback(canvas, &model);
}

int main(int argc, char** argv) {
    if(argc > 1) preview_set_dir(argv[1]);
    ControlCentreState* s = &model.state;
    s->on[ControlCentreTileBluetooth] = true;
    s->pin_set = true;
    s->brightness = 0.7f;
    s->volume = 0.35f;
    s->vibro = true;
    snprintf(s->app_label[0], sizeof(s->app_label[0]), "NFC");
    s->hour = 21;
    s->minute = 41;
    s->battery = 87;
    preview_draw("control_centre_lock", draw, NULL);

    model.column = 2;
    preview_draw("control_centre_bluetooth", draw, NULL);

    s->on[ControlCentreTileDark] = true;
    s->on[ControlCentreTileStealth] = true;
    s->charging = true;
    s->format_12 = true;
    model.column = 2;
    model.row = 1;
    preview_draw("control_centre_app", draw, NULL);

    model.column = 5;
    preview_draw("control_centre_brightness", draw, NULL);
    model.column = 6;
    preview_draw("control_centre_volume", draw, NULL);

    model.column = 3;
    s->app_label[1][0] = '\0';
    preview_draw("control_centre_no_app", draw, NULL);

    // every focus and value, for the sanitizers
    Canvas* canvas = preview_canvas();
    for(int column = 0; column < COLUMNS; column++) {
        for(int row = 0; row < TILE_ROWS; row++) {
            model.column = column;
            model.row = row;
            for(int v = 0; v <= 20; v++) {
                s->brightness = s->volume = v / 20.0f;
                s->battery = v * 5;
                draw(canvas, NULL);
            }
        }
    }
    return 0;
}
