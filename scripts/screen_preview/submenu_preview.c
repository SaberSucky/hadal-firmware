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
    model.header = furi_string_alloc_set_str("Desktop Keys");
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
