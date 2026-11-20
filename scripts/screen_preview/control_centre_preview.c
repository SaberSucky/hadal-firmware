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
    s->app_icon[0] = &A_NFC_14;
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

    // the app picker, as the scene fills it
    static const DesktopApp apps[] = {
        {.label = "None", .action = NULL, .icon = &I_cc_none_12x12},
        {.label = "Apps", .action = "Apps", .icon = &A_Plugins_14},
        {.label = "Sub-GHz", .action = "Sub-GHz", .icon = &A_Sub1ghz_14},
        {.label = "125 kHz RFID", .action = "125 kHz RFID", .icon = &A_125khz_14},
        {.label = "NFC", .action = "NFC", .icon = &A_NFC_14},
        {.label = "Infrared", .action = "Infrared", .icon = &A_Infrared_14},
        {.label = "Snake Game", .action = "/ext/apps/Games/snake.fap", .icon = NULL},
    };
    model.picking = true;
    model.pick_slot = 1;
    model.apps = apps;
    model.app_count = COUNT_OF(apps);
    model.pick_index = 4;
    preview_draw("control_centre_picker", draw, NULL);
    model.pick_index = 0;
    preview_draw("control_centre_picker_none", draw, NULL);
    model.app_count = 2;
    model.pick_index = 1;
    preview_draw("control_centre_picker_two", draw, NULL);
    for(size_t count = 1; count <= COUNT_OF(apps); count++) {
        model.app_count = count;
        for(size_t i = 0; i < count; i++) {
            model.pick_index = i;
            draw(preview_canvas(), NULL);
        }
    }
    model.picking = false;

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
