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
    item->height = ITEM_H;
    model.snap = true; // as variable_item_list_add does
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
static const char* const on_off[] = {"OFF", "ON"};
static const char* const percent[] = {"0%",  "5%",  "10%", "15%", "20%", "25%", "30%",
                                      "35%", "40%", "45%", "50%", "55%", "60%", "65%",
                                      "70%", "75%", "80%", "85%", "90%", "95%", "100%"};
static const char* const keep_skip[] = {"Keep", "Skip"};
static const char* const location[] = {"Off", "Phone"};
static const char* const hopping[] = {
    "OFF", "-90dBm", "-85dBm", "-80dBm", "-75dBm", "-70dBm",
    "-65dBm", "-60dBm", "-55dBm", "-50dBm", "-45dBm", "-40dBm"};
static const char* const frequencies[] = {
    "300.00", "302.75", "303.87", "304.25", "307.00", "307.50", "307.80", "309.00",
    "310.00", "312.00", "312.10", "313.00", "313.85", "314.00", "314.35", "315.00",
    "318.00", "330.00", "345.00", "348.00", "387.00", "390.00", "418.00", "433.07",
    "433.42", "433.66", "433.89", "433.92", "434.07", "434.17"};

int main(int argc, char** argv) {
    if(argc > 1) preview_set_dir(argv[1]);
    VariableItemArray_init(model.items);

    add("Bluetooth", 2, 1, "ON")->values_text = on_off;
    add("Vibration", 2, 0, "OFF")->values_text = on_off;
    add("Brightness", 21, 14, "70%")->values_text = percent;
    add("Volume", 21, 7, "35%")->values_text = percent;
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
    model.window_position = 1; // scrolled up, as OK does, out from under the hint bar
    model.dropdown_open = true;
    model.dropdown_index = 3;
    model.dropdown_window = 1; // the cursor kept in the box, as the input does
    preview_draw("list_dropdown_open", draw, NULL);
    model.dropdown_open = false;
    model.window_position = 0;

    // an app that gives no texts (one on the SD card): arrows, whatever its value says
    clear();
    add("Auto lock", 8, 0, "Off");
    add("Log level", 6, 2, "Info");
    add("Sleep", 2, 0, "Default");
    add("Hand", 2, 0, "Left")->values_text = hand;
    add("Frequency", 30, 12, "433.92");
    model.position = 0;
    preview_draw("list_mixed", draw, NULL);

    // Sub-GHz's config: the same item looks the same whichever value it shows
    clear();
    add("Frequency", COUNT_OF(frequencies), 27, "433.92")->values_text = frequencies;
    add("Hopping", COUNT_OF(hopping), 0, "OFF")->values_text = hopping;
    add("Duplicates", 2, 0, "Keep")->values_text = keep_skip;
    add("Location", 2, 1, "Phone")->values_text = location;
    model.position = 3;
    model.window_position = 0;
    preview_draw("list_subghz_phone", draw, NULL);
    VariableItemArray_get(model.items, 3)->current_value_index = 0;
    furi_string_set(VariableItemArray_get(model.items, 3)->current_value_text, "Off");
    preview_draw("list_subghz_off", draw, NULL);
    model.position = 0;
    model.dropdown_open = true;
    model.dropdown_index = 27;
    model.dropdown_window = 25;
    preview_draw("list_subghz_frequencies", draw, NULL);
    model.dropdown_open = false;

    // settings that mean nothing while another is off: hidden, then sliding out, indented
    static const char* const night_shift[] = {"OFF", "-10%", "-20%", "-30%", "-40%", "-50%", "-60%"};
    static const char* const hours[] = {"18:00", "19:00", "20:00", "21:00", "22:00", "23:00"};
    static const char* const effects[] = {"OFF", "Rainbow", "Wave"};
    static const char* const steps[] = {"1", "2", "3"};
    clear();
    add("Backlight Time", 2, 0, "30s");
    add("Night Shift", COUNT_OF(night_shift), 0, "OFF")->values_text = night_shift;
    VariableItem* start = add("Start", COUNT_OF(hours), 3, "21:00");
    start->values_text = hours;
    start->indent = 1;
    VariableItem* end = add("End", COUNT_OF(hours), 1, "19:00");
    end->values_text = hours;
    end->indent = 1;
    add("LED Brightness", COUNT_OF(percent), 14, "70%")->values_text = percent;
    add("Volume", COUNT_OF(percent), 7, "35%")->values_text = percent;
    start->hidden = end->hidden = true;
    model.position = 1;
    model.window_position = 0;
    preview_draw("list_children_hidden", draw, NULL);
    // Night Shift turned on: each frame as the slide timer steps it
    VariableItem* night = VariableItemArray_get(model.items, 1);
    night->current_value_index = 3;
    furi_string_set(night->current_value_text, "-30%");
    start->hidden = end->hidden = false;
    const uint8_t frames[] = {8, 12, 14};
    for(size_t f = 0; f < COUNT_OF(frames); f++) {
        start->height = end->height = frames[f];
        char name[32];
        snprintf(name, sizeof(name), "list_children_slide_%u", (unsigned)f);
        preview_draw(name, draw, NULL);
    }
    start->height = end->height = ITEM_H;
    preview_draw("list_children_shown", draw, NULL);
    model.position = 2;
    preview_draw("list_children_child_selected", draw, NULL);
    // hidden while the cursor was on one: it goes to the item they belong to
    start->hidden = end->hidden = true;
    preview_draw("list_children_cursor_moved", draw, NULL);
    furi_check(model.position == 1);

    clear();
    add("RGB backlight installed", 2, 1, "ON")->values_text = on_off;
    VariableItem* fx = add("Effects", COUNT_OF(effects), 1, "Rainbow");
    fx->values_text = effects;
    fx->indent = 1;
    VariableItem* speed = add("Speed", 21, 4, "20%");
    speed->values_text = percent;
    speed->indent = 2;
    VariableItem* step = add("Color step", COUNT_OF(steps), 1, "2");
    step->values_text = steps;
    step->indent = 2;
    VariableItem* sat = add("Saturation", COUNT_OF(percent), 20, "100%");
    sat->values_text = percent;
    sat->indent = 2;
    model.position = 1;
    model.window_position = 0;
    preview_draw("list_children_nested", draw, NULL);

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
