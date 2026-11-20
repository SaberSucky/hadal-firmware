#include "preview.h"
#include "../../applications/services/gui/modules/variable_item_list.c"

static VariableItemListModel model;

static void draw(Canvas* canvas, void* context) {
    UNUSED(context);
    variable_item_list_draw_callback(canvas, &model);
}

static VariableItem* add(const char* label, uint8_t count, uint8_t index, const char* text) {
    VariableItem* item = VariableItemArray_push_new(model.items);
    memset(item, 0, sizeof(VariableItem));
    item->label = label;
    item->values_count = count;
    item->current_value_index = index;
    item->current_value_text = furi_string_alloc_set_str(text);
    item->locked_message = furi_string_alloc();
    return item;
}

static void clear(void) {
    VariableItemArray_it_t it;
    for(VariableItemArray_it(it, model.items); !VariableItemArray_end_p(it);
        VariableItemArray_next(it)) {
        furi_string_free(VariableItemArray_ref(it)->current_value_text);
        furi_string_free(VariableItemArray_ref(it)->locked_message);
    }
    VariableItemArray_reset(model.items);
}

static const char* const time_format[] = {"12h", "24h"};
static const char* const hand[] = {"Left", "Right"};
static const char* const units[] = {"Metric", "Imperial"};
static const char* const regions[] = {"Europe", "USA", "Japan", "Australia", "Other"};
static const char* const styles[] = {"Wii", "List", "Grid", "Compact"};

int main(int argc, char** argv) {
    if(argc > 1) preview_set_dir(argv[1]);
    VariableItemArray_init(model.items);

    add("Bluetooth", 2, 1, "ON");
    add("Vibration", 2, 0, "OFF");
    add("Brightness", 21, 14, "70%");
    add("Volume", 21, 7, "35%");
    model.position = 0;
    preview_draw("list_switch_on", draw, NULL);
    model.position = 1;
    preview_draw("list_switch_off", draw, NULL);
    model.position = 2;
    preview_draw("list_meter", draw, NULL);

    clear();
    add("Time format", 2, 1, "24h")->values_text = time_format;
    add("Units", 2, 0, "Metric")->values_text = units;
    add("Main menu", 4, 1, "List")->values_text = styles;
    add("Region", 5, 2, "Japan")->values_text = regions;
    model.position = 0;
    preview_draw("list_segments", draw, NULL);
    model.position = 2;
    preview_draw("list_dropdown", draw, NULL);
    model.position = 3;
    model.dropdown_open = true;
    model.dropdown_index = 3;
    model.dropdown_window = 0;
    preview_draw("list_dropdown_open", draw, NULL);
    model.dropdown_open = false;

    clear();
    add("Auto lock", 8, 0, "Off");
    add("Log level", 6, 2, "Info");
    add("Sleep", 2, 0, "Default");
    add("Hand", 2, 0, "Left")->values_text = hand;
    add("Frequency", 30, 12, "433.92");
    model.position = 0;
    preview_draw("list_mixed", draw, NULL);

    // every position, with and without the dropdown, for the sanitizers
    Canvas* canvas = preview_canvas();
    for(size_t p = 0; p < 5; p++) {
        model.position = p;
        model.window_position = p > 2 ? p - 2 : 0;
        for(size_t step = 0; step < 30; step++) {
            model.scroll_counter = step;
            draw(canvas, NULL);
        }
    }
    clear();
    VariableItemArray_clear(model.items);
    return 0;
}
