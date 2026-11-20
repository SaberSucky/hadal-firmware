#include "preview.h"
#include "../../applications/services/gui/modules/submenu.c"

static SubmenuModel model;

static void draw(Canvas* canvas, void* context) {
    UNUSED(context);
    submenu_view_draw_callback(canvas, &model);
}

static void add(const char* label) {
    SubmenuItem* item = SubmenuItemArray_push_new(model.items);
    furi_string_set_str(item->label, label);
}

int main(int argc, char** argv) {
    if(argc > 1) preview_set_dir(argv[1]);
    SubmenuItemArray_init(model.items);
    model.hints = furi_string_alloc();
    model.header = furi_string_alloc_set_str("Main Menu");
    furi_string_set_str(model.hints, "ok=Move or remove;back=Done");
    add("Sub-GHz");
    add("125 kHz RFID");
    add("NFC");
    add("Infrared");
    model.position = 1;
    preview_draw("submenu_hints", draw, NULL);
    furi_string_set_str(model.header, "Spoofing");
    furi_string_set_str(model.hints, "=Changes apply after a restart");
    model.position = 0;
    preview_draw("submenu_hints_text", draw, NULL);
    model.position = 3;
    model.window_position = 1;
    for(size_t step = 0; step < 30; step++) {
        model.scroll_counter = step;
        draw(preview_canvas(), NULL);
    }
    SubmenuItemArray_reset(model.items);
    furi_string_reset(model.hints);
    model.position = 0;
    model.window_position = 0;
    furi_string_set_str(model.header, "Desktop Keys");
    add("Up: Lock Menu");
    add("Up held: battery_history_with_a_long_name");
    add("Down: Archive");
    add("Down held: /ext/subghz/Garage_door_opener_left.sub");
    add("Left: default");
    model.position = 1;
    model.scroll_counter = 0;
    preview_draw("submenu_long_start", draw, NULL);
    model.scroll_counter = 8;
    preview_draw("submenu_long_scrolled", draw, NULL);
    model.position = 3;
    model.window_position = 1;
    model.scroll_counter = 14;
    preview_draw("submenu_long_path", draw, NULL);
    // every scroll step of every item, for the sanitizers
    Canvas* canvas = preview_canvas();
    for(size_t p = 0; p < 5; p++) {
        model.position = p;
        model.window_position = p > 2 ? p - 2 : 0;
        for(size_t step = 0; step < 80; step++) {
            model.scroll_counter = step;
            submenu_view_draw_callback(canvas, &model);
        }
    }
    return 0;
}
