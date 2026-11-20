#include "variable_item_list.h"
#include <gui/elements.h>
#include <gui/canvas.h>
#include <furi.h>
#include <assets_icons.h>
#include <m-array.h>
#include <stdint.h>
#include <strings.h>

#define SCROLL_INTERVAL (333)

// Controls: how an item's value is drawn, chosen from what the item is (see the header)
#define VALUE_RIGHT       122 // the value area's right end: the selection's last column
#define CONTROL_RIGHT     (VALUE_RIGHT - 3) // a control's last column: two clear inside the selection
#define SWITCH_W          17
#define SWITCH_H          9
#define METER_MIN_VALUES  5 // a numeric value with this many steps or more gets a meter
#define SEGMENTS_MAX      3
#define SEGMENT_PAD       3 // either side of a segment's text
#define SEGMENTS_WIDTH_MAX 64
#define DROPDOWN_ROWS     3
// a dropdown row: its selection, two clear rows over the capitals and one under the descenders
#define DROPDOWN_ROW_H    12
#define TEXT_PAD          3 // either side of a text, inside what holds it

typedef enum {
    VariableItemControlArrows, // < value >, as Flipper always drew it
    VariableItemControlSwitch, // on or off
    VariableItemControlMeter, // a number, with how far along its range it is
    VariableItemControlSegments, // two or three short choices side by side
    VariableItemControlDropdown, // OK opens the choices as a list
} VariableItemControl;

struct VariableItem {
    const char* label;
    uint8_t current_value_index;
    FuriString* current_value_text;
    const char* const* values_text; // every value's text, when the app gave them
    uint8_t values_count;
    uint8_t control; // VariableItemControl, as last drawn: what OK does

    VariableItemChangeCallback change_callback;
    bool locked;
    FuriString* locked_message;
    void* context;
};

ARRAY_DEF(VariableItemArray, VariableItem, M_POD_OPLIST); //-V658

struct VariableItemList {
    View* view;
    VariableItemListEnterCallback callback;
    void* context;
    FuriTimer* scroll_timer;
    FuriTimer* locked_timer;
};

typedef struct {
    VariableItemArray_t items;
    uint8_t position;
    uint8_t window_position;
    size_t scroll_counter;
    bool locked_message_visible;
    // a dropdown's list of choices, while it is open, over the item at position
    bool dropdown_open;
    uint8_t dropdown_index;
    // the selected item's label or value is too long and scrolls: the timer redraws only then
    bool selected_scrolls;
    uint8_t dropdown_window;
} VariableItemListModel;

static void variable_item_list_process_up(VariableItemList* variable_item_list);
static void variable_item_list_process_down(VariableItemList* variable_item_list);
static void variable_item_list_process_left(VariableItemList* variable_item_list);
static void variable_item_list_process_right(VariableItemList* variable_item_list);
static void variable_item_list_process_ok(VariableItemList* variable_item_list);

VariableItem* variable_item_list_get_selected_item(VariableItemListModel* model);

/* ---- controls ---- */

static bool variable_item_text_is(const char* text, const char* const* words) {
    for(; *words; words++) {
        if(strcasecmp(text, *words) == 0) return true;
    }
    return false;
}

static const char* const switch_on_words[] = {"on", "yes", "enabled", "enable", "true", NULL};
static const char* const switch_off_words[] = {"off", "no", "disabled", "disable", "false", NULL};

/** Whether an item's two values are an on/off pair (whichever comes first), and whether the one
 * chosen is "on". Only from the texts the app gave: what one value says cannot tell what the
 * other is ("Off" may pair with "Phone") */
static bool variable_item_is_switch(const VariableItem* item, bool* on) {
    if(!item->values_text || item->values_count != 2) return false;
    const char* first = item->values_text[0];
    const char* second = item->values_text[1];
    const bool pair = (variable_item_text_is(first, switch_off_words) &&
                       variable_item_text_is(second, switch_on_words)) ||
                      (variable_item_text_is(first, switch_on_words) &&
                       variable_item_text_is(second, switch_off_words));
    if(pair && on) {
        const uint8_t index = item->current_value_index < 2 ? item->current_value_index : 0;
        *on = variable_item_text_is(item->values_text[index], switch_on_words);
    }
    return pair;
}

static bool variable_item_text_is_number(const char* text) {
    if(text[0] == '-' || text[0] == '+') text++;
    return text[0] >= '0' && text[0] <= '9';
}

/** Every value a number ("70%", "-3 dB", "30s", "433.92"), the first one perhaps an "Off" */
static bool variable_item_is_number(const VariableItem* item) {
    if(!item->values_text) return false;
    for(uint8_t i = 0; i < item->values_count; i++) {
        const char* text = item->values_text[i];
        if(i == 0 && variable_item_text_is(text, switch_off_words)) continue;
        if(!variable_item_text_is_number(text)) return false;
    }
    return true;
}

/** The segments' total width, or 0 when they do not fit in width */
static uint16_t variable_item_segments_width(Canvas* canvas, const VariableItem* item, uint16_t width) {
    uint16_t total = 0;
    for(uint8_t i = 0; i < item->values_count; i++) {
        total += canvas_string_width(canvas, item->values_text[i]) + 2 * SEGMENT_PAD;
    }
    return total <= width ? total : 0;
}

/** The control an item is drawn as. It depends only on the item's values, never on the one
 * chosen, so an item looks the same whichever value it shows. An item without its values' texts
 * (an app on the SD card written for stock lists) is always arrows: guessing from the one value
 * on show turned "Off" into a switch and its other value back into arrows */
static VariableItemControl
    variable_item_control(Canvas* canvas, const VariableItem* item, uint16_t width) {
    if(!item->values_text || item->values_count < 2) return VariableItemControlArrows;
    if(variable_item_is_switch(item, NULL)) return VariableItemControlSwitch;
    if(item->values_count >= METER_MIN_VALUES && variable_item_is_number(item)) {
        return VariableItemControlMeter;
    }
    if(item->values_count <= SEGMENTS_MAX && variable_item_segments_width(canvas, item, width)) {
        return VariableItemControlSegments;
    }
    return VariableItemControlDropdown;
}

/** A switch, its right end at x, its top at y: filled with the knob on the right when on */
static void variable_item_draw_switch(Canvas* canvas, int32_t x, int32_t y, bool on, Color ink) {
    const Color paper = ink == ColorBlack ? ColorWhite : ColorBlack;
    x -= SWITCH_W;
    canvas_set_color(canvas, ink);
    if(on) {
        canvas_draw_rbox(canvas, x, y, SWITCH_W, SWITCH_H, 4);
        canvas_set_color(canvas, paper);
        canvas_draw_disc(canvas, x + SWITCH_W - 5, y + 4, 2);
    } else {
        canvas_draw_rframe(canvas, x, y, SWITCH_W, SWITCH_H, 4);
        canvas_draw_disc(canvas, x + 4, y + 4, 2);
    }
    canvas_set_color(canvas, ink);
}

/** Segments, ending at the control column: the chosen one a filled pill (a clear row over its
 * capitals, one under its descenders, TEXT_PAD either side), the others plain, with dividers */
static void variable_item_draw_segments(
    Canvas* canvas,
    const VariableItem* item,
    int32_t item_y,
    uint16_t total,
    Color ink) {
    const Color paper = ink == ColorBlack ? ColorWhite : ColorBlack;
    int32_t x = CONTROL_RIGHT + 1 - total;
    const int32_t top = item_y + 2, baseline = item_y + 11;
    canvas_set_color(canvas, ink);
    for(uint8_t i = 0; i < item->values_count; i++) {
        const uint16_t width = canvas_string_width(canvas, item->values_text[i]) + 2 * SEGMENT_PAD;
        if(i == item->current_value_index) {
            canvas_draw_rbox(canvas, x, top, width, 12, 3);
            canvas_set_color(canvas, paper);
        } else if(i > 0 && i != item->current_value_index + 1) {
            // a divider, where neither side is the chosen one
            canvas_draw_line(canvas, x, top + 3, x, top + 8);
        }
        canvas_draw_str(canvas, x + SEGMENT_PAD, baseline, item->values_text[i]);
        canvas_set_color(canvas, ink);
        x += width;
    }
}

/** How far along its values an item is, as a line under its text: solid as far as it has come,
 * dotted for the rest */
static void variable_item_draw_meter(
    Canvas* canvas,
    const VariableItem* item,
    int32_t x0,
    int32_t x1,
    int32_t y) {
    const int32_t width = x1 - x0 + 1;
    const int32_t fill = item->values_count > 1 ?
                             width * item->current_value_index / (item->values_count - 1) :
                             0;
    if(fill) canvas_draw_line(canvas, x0, y, x0 + fill - 1, y);
    for(int32_t x = x0 + fill + 1; x <= x1; x += 2) {
        canvas_draw_dot(canvas, x, y);
    }
}

/** The open dropdown: the item's choices in a box over the list, the one under the cursor
 * filled. A clear pixel all round outside the box and inside its double-edged frame; each text
 * TEXT_PAD from its selection's sides */
static void variable_item_list_draw_dropdown(
    Canvas* canvas,
    const VariableItemListModel* model,
    const VariableItem* item,
    uint8_t item_y) {
    canvas_set_font(canvas, FontSecondary);
    uint16_t text_width = 0;
    for(uint8_t i = 0; i < item->values_count; i++) {
        text_width = MAX(text_width, canvas_string_width(canvas, item->values_text[i]));
    }
    const uint8_t rows = MIN(item->values_count, DROPDOWN_ROWS);
    const bool scrolls = item->values_count > rows;
    // columns, left to right: frame (2), clear, selection (text with TEXT_PAD each side), clear,
    // and when it scrolls the scrollbar (3) and a clear one; then the frame (2)
    const int32_t chrome = 2 + 1 + 2 * TEXT_PAD + 1 + (scrolls ? 4 : 0) + 2;
    const int32_t width = MIN((int32_t)text_width + chrome, 112);
    const int32_t right = 125;
    const int32_t left = right - width + 1;
    const int32_t select_left = left + 3;
    const int32_t select_right = right - 3 - (scrolls ? 4 : 0);
    const int32_t select_width = select_right - select_left + 1;
    // rows: frame, clear, the rows, clear, frame
    const int32_t height = rows * DROPDOWN_ROW_H + 4;
    int32_t y = item_y + 8 - height / 2;
    y = CLAMP(y, 64 - ELEMENTS_HINT_BAR_HEIGHT - 1 - height, 1);

    // the rows draw no controls while it is open (their labels only): the box needs only a
    // clear edge, and the list's scrollbar under it
    canvas_set_color(canvas, ColorWhite);
    canvas_draw_box(canvas, left - 1, y - 1, 128 - (left - 1), height + 2);
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_rframe(canvas, left, y, width, height, 3);
    canvas_draw_rframe(canvas, left + 1, y, width - 2, height, 3); // a heavier side edge: it floats

    for(uint8_t row = 0; row < rows; row++) {
        const uint8_t index = model->dropdown_window + row;
        const int32_t row_y = y + 2 + row * DROPDOWN_ROW_H;
        if(index == model->dropdown_index) {
            canvas_draw_rbox(canvas, select_left, row_y, select_width, DROPDOWN_ROW_H, 3);
            canvas_set_color(canvas, ColorWhite);
        }
        FuriString* text = furi_string_alloc_set_str(item->values_text[index]);
        elements_string_fit_width(canvas, text, select_width - 2 * TEXT_PAD);
        canvas_draw_str(canvas, select_left + TEXT_PAD, row_y + 9, furi_string_get_cstr(text));
        furi_string_free(text);
        canvas_set_color(canvas, ColorBlack);
    }
    if(scrolls) {
        elements_scrollbar_pos(
            canvas, right - 2, y + 2, rows * DROPDOWN_ROW_H, model->dropdown_index, item->values_count);
    }
    elements_hint_bar(canvas, "ud=Choose;ok=Set;back=Cancel");
}

static void variable_item_list_draw_callback(Canvas* canvas, void* _model) {
    VariableItemListModel* model = _model;

    const uint8_t item_height = 16;
    const uint8_t item_width = 123;

    canvas_clear(canvas);

    uint8_t position = 0;
    VariableItemArray_it_t it;

    canvas_set_font(canvas, FontSecondary);
    for(VariableItemArray_it(it, model->items); !VariableItemArray_end_p(it);
        VariableItemArray_next(it)) {
        uint8_t item_position = position - model->window_position;
        uint8_t items_on_screen = 4;
        uint8_t y_offset = 0;

        if(item_position < items_on_screen) {
            VariableItem* item = VariableItemArray_ref(it);
            uint8_t item_y = y_offset + (item_position * item_height);
            uint8_t item_text_y = item_y + item_height - 4;
            size_t scroll_counter = 0;

            if(position == model->position) {
                canvas_set_color(canvas, ColorBlack);
                canvas_draw_rbox(canvas, 0, item_y + 1, item_width, item_height - 2, 3);
                canvas_set_color(canvas, ColorWhite);
                scroll_counter = model->scroll_counter;
                if(scroll_counter < 1) {
                    scroll_counter = 0;
                } else {
                    scroll_counter -= 1;
                }
            } else {
                canvas_set_color(canvas, ColorBlack);
            }

            uint8_t temp_x_default = 73;
            uint8_t temp_w_default = 66;
            if(furi_string_size(item->current_value_text) < (size_t)4) {
                temp_x_default = 80;
                temp_w_default = 71;
            }

            const Color ink = position == model->position ? ColorWhite : ColorBlack;
            const VariableItemControl control =
                item->locked ? VariableItemControlArrows :
                               variable_item_control(canvas, item, SEGMENTS_WIDTH_MAX);
            item->control = control; // segments or dropdown hangs on the texts' widths
            uint16_t segments = 0;
            if(control == VariableItemControlSwitch) {
                // the label has the room the arrows took
                temp_w_default = CONTROL_RIGHT + 1 - SWITCH_W - 4 - 6;
            } else if(control == VariableItemControlSegments) {
                segments = variable_item_segments_width(canvas, item, SEGMENTS_WIDTH_MAX);
                temp_w_default = CONTROL_RIGHT + 1 - segments - 4 - 6;
            }

            if(position == model->position) {
                const size_t value_room =
                    control == VariableItemControlDropdown ? (size_t)(CONTROL_RIGHT - 9 - temp_x_default) :
                    (control == VariableItemControlArrows || control == VariableItemControlMeter) ?
                                                             37 :
                                                             SIZE_MAX;
                model->selected_scrolls =
                    canvas_string_width(canvas, item->label) > temp_w_default ||
                    canvas_string_width(canvas, furi_string_get_cstr(item->current_value_text)) >
                        value_room;
            }

            if(item->current_value_index == 0 && furi_string_empty(item->current_value_text)) {
                // Only left text, no right text
                canvas_draw_str(canvas, 6, item_text_y, item->label);
            } else {
                elements_scrollable_text_line_str(
                    canvas,
                    6,
                    item_text_y,
                    temp_w_default,
                    item->label,
                    scroll_counter,
                    false,
                    false);
            }

            bool on = false;
            if(model->dropdown_open) {
                // the dropdown's list takes the right of the screen: rows show their labels
            } else if(item->locked) {
                canvas_draw_icon(canvas, 110, item_text_y - 8, &I_Lock_7x8);
            } else if(control == VariableItemControlSwitch) {
                variable_item_is_switch(item, &on);
                variable_item_draw_switch(canvas, CONTROL_RIGHT + 1, item_y + 4, on, ink);
            } else if(control == VariableItemControlSegments) {
                variable_item_draw_segments(canvas, item, item_y, segments, ink);
            } else if(control == VariableItemControlDropdown) {
                elements_scrollable_text_line_str(
                    canvas,
                    (CONTROL_RIGHT - 9 + temp_x_default) / 2,
                    item_text_y,
                    CONTROL_RIGHT - 9 - temp_x_default,
                    furi_string_get_cstr(item->current_value_text),
                    scroll_counter,
                    false,
                    true);
                // a chevron: OK opens the choices
                const int32_t chevron = CONTROL_RIGHT - 6;
                canvas_draw_line(canvas, chevron, item_text_y - 5, chevron + 3, item_text_y - 2);
                canvas_draw_line(canvas, chevron + 3, item_text_y - 2, chevron + 6, item_text_y - 5);
                canvas_draw_line(canvas, chevron, item_text_y - 4, chevron + 3, item_text_y - 1);
                canvas_draw_line(canvas, chevron + 3, item_text_y - 1, chevron + 6, item_text_y - 4);
            } else {
                if(control == VariableItemControlMeter) {
                    variable_item_draw_meter(canvas, item, temp_x_default + 7, 112, item_y + 13);
                }
                if(item->current_value_index > 0) {
                    canvas_draw_str(canvas, temp_x_default, item_text_y, "<");
                }

                elements_scrollable_text_line_str(
                    canvas,
                    (115 + temp_x_default) / 2 + 1,
                    item_text_y,
                    37,
                    furi_string_get_cstr(item->current_value_text),
                    scroll_counter,
                    false,
                    true);

                if(item->current_value_index < (item->values_count - 1)) {
                    canvas_draw_str(canvas, 115, item_text_y, ">");
                }
            }
        }

        position++;
    }

    elements_scrollbar(canvas, model->position, VariableItemArray_size(model->items));

    if(model->dropdown_open && model->position < VariableItemArray_size(model->items)) {
        const VariableItem* item = VariableItemArray_cget(model->items, model->position);
        const uint8_t item_y = (model->position - model->window_position) * 16;
        if(item->values_text) variable_item_list_draw_dropdown(canvas, model, item, item_y);
    }

    if(model->locked_message_visible) {
        canvas_set_color(canvas, ColorWhite);
        canvas_draw_box(canvas, 8, 10, 110, 48);
        canvas_set_color(canvas, ColorBlack);
        canvas_draw_icon(canvas, 10, 14, &I_WarningDolphin_45x42);
        canvas_draw_rframe(canvas, 8, 8, 112, 50, 3);
        canvas_draw_rframe(canvas, 9, 9, 110, 48, 2);
        elements_multiline_text_aligned(
            canvas,
            84,
            32,
            AlignCenter,
            AlignCenter,
            furi_string_get_cstr(
                VariableItemArray_get(model->items, model->position)->locked_message));
    }
}

void variable_item_list_set_selected_item(VariableItemList* variable_item_list, uint8_t index) {
    furi_check(variable_item_list);
    with_view_model(
        variable_item_list->view,
        VariableItemListModel * model,
        {
            uint8_t position = index;
            if(position >= VariableItemArray_size(model->items)) {
                position = 0;
            }

            model->position = position;
            model->window_position = position;

            if(model->window_position > 0) {
                model->window_position -= 1;
            }

            if(VariableItemArray_size(model->items) <= 4) {
                model->window_position = 0;
            } else {
                if(model->window_position >= (VariableItemArray_size(model->items) - 4)) {
                    model->window_position = (VariableItemArray_size(model->items) - 4);
                }
            }
        },
        true);
}

uint8_t variable_item_list_get_selected_item_index(VariableItemList* variable_item_list) {
    furi_check(variable_item_list);
    VariableItemListModel* model = view_get_model(variable_item_list->view);
    uint8_t idx = model->position;
    view_commit_model(variable_item_list->view, false);
    return idx;
}

/** Up and Down go through the open dropdown's choices, OK sets the item to one, Back leaves it */
static bool variable_item_list_dropdown_input(VariableItemList* variable_item_list, InputEvent* event) {
    if(event->type != InputTypeShort && event->type != InputTypeRepeat) return true;
    with_view_model(
        variable_item_list->view,
        VariableItemListModel * model,
        {
            VariableItem* item = variable_item_list_get_selected_item(model);
            const uint8_t count = item->values_count;
            const uint8_t rows = MIN(count, DROPDOWN_ROWS);
            switch(event->key) {
            case InputKeyUp:
                model->dropdown_index = model->dropdown_index ? model->dropdown_index - 1 :
                                                                count - 1;
                break;
            case InputKeyDown:
                model->dropdown_index = (model->dropdown_index + 1) % count;
                break;
            case InputKeyOk:
                if(event->type != InputTypeShort) break;
                model->dropdown_open = false;
                if(item->current_value_index != model->dropdown_index) {
                    item->current_value_index = model->dropdown_index;
                    model->scroll_counter = 0;
                    if(item->change_callback) item->change_callback(item);
                }
                break;
            case InputKeyBack:
                if(event->type == InputTypeShort) model->dropdown_open = false;
                break;
            default:
                break;
            }
            // keep the cursor in the box
            if(model->dropdown_index < model->dropdown_window) {
                model->dropdown_window = model->dropdown_index;
            } else if(model->dropdown_index >= model->dropdown_window + rows) {
                model->dropdown_window = model->dropdown_index - rows + 1;
            }
        },
        true);
    return true;
}

static bool variable_item_list_input_callback(InputEvent* event, void* context) {
    VariableItemList* variable_item_list = context;
    furi_assert(variable_item_list);
    bool consumed = false;

    bool locked_message_visible = false;
    with_view_model(
        variable_item_list->view,
        VariableItemListModel * model,
        { locked_message_visible = model->locked_message_visible; },
        false);

    bool dropdown_open = false;
    with_view_model(
        variable_item_list->view,
        VariableItemListModel * model,
        { dropdown_open = model->dropdown_open; },
        false);
    if(dropdown_open) return variable_item_list_dropdown_input(variable_item_list, event);

    if((!(event->type == InputTypePress) && !(event->type == InputTypeRelease)) &&
       locked_message_visible) {
        with_view_model(
            variable_item_list->view,
            VariableItemListModel * model,
            { model->locked_message_visible = false; },
            true);
        consumed = true;
    } else if(event->type == InputTypeShort) {
        switch(event->key) {
        case InputKeyUp:
            consumed = true;
            variable_item_list_process_up(variable_item_list);
            break;
        case InputKeyDown:
            consumed = true;
            variable_item_list_process_down(variable_item_list);
            break;
        case InputKeyLeft:
            consumed = true;
            variable_item_list_process_left(variable_item_list);
            break;
        case InputKeyRight:
            consumed = true;
            variable_item_list_process_right(variable_item_list);
            break;
        case InputKeyOk:
            variable_item_list_process_ok(variable_item_list);
            break;
        default:
            break;
        }
    } else if(event->type == InputTypeRepeat) {
        switch(event->key) {
        case InputKeyUp:
            consumed = true;
            variable_item_list_process_up(variable_item_list);
            break;
        case InputKeyDown:
            consumed = true;
            variable_item_list_process_down(variable_item_list);
            break;
        case InputKeyLeft:
            consumed = true;
            variable_item_list_process_left(variable_item_list);
            break;
        case InputKeyRight:
            consumed = true;
            variable_item_list_process_right(variable_item_list);
            break;
        default:
            break;
        }
    }

    return consumed;
}

void variable_item_list_process_up(VariableItemList* variable_item_list) {
    with_view_model(
        variable_item_list->view,
        VariableItemListModel * model,
        {
            uint8_t items_on_screen = 4;
            if(model->position > 0) {
                model->position--;

                if((model->position == model->window_position) && (model->window_position > 0)) {
                    model->window_position--;
                }
            } else {
                model->position = VariableItemArray_size(model->items) - 1;
                if(model->position > (items_on_screen - 1)) {
                    model->window_position = model->position - (items_on_screen - 1);
                }
            }
            model->scroll_counter = 0;
        },
        true);
}

void variable_item_list_process_down(VariableItemList* variable_item_list) {
    with_view_model(
        variable_item_list->view,
        VariableItemListModel * model,
        {
            uint8_t items_on_screen = 4;
            if(model->position < (VariableItemArray_size(model->items) - 1)) {
                model->position++;
                if((model->position - model->window_position) > (items_on_screen - 2) &&
                   model->window_position <
                       (VariableItemArray_size(model->items) - items_on_screen)) {
                    model->window_position++;
                }
            } else {
                model->position = 0;
                model->window_position = 0;
            }
            model->scroll_counter = 0;
        },
        true);
}

VariableItem* variable_item_list_get_selected_item(VariableItemListModel* model) {
    VariableItem* item = NULL;

    VariableItemArray_it_t it;
    uint8_t position = 0;
    for(VariableItemArray_it(it, model->items); !VariableItemArray_end_p(it);
        VariableItemArray_next(it)) {
        if(position == model->position) {
            break;
        }
        position++;
    }

    item = VariableItemArray_ref(it);

    furi_assert(item);
    return item;
}

void variable_item_list_process_left(VariableItemList* variable_item_list) {
    with_view_model(
        variable_item_list->view,
        VariableItemListModel * model,
        {
            VariableItem* item = variable_item_list_get_selected_item(model);
            if(item->locked) {
                model->locked_message_visible = true;
                furi_timer_start(
                    variable_item_list->locked_timer, furi_kernel_get_tick_frequency() * 3);
            } else if(item->current_value_index > 0) {
                item->current_value_index--;
                model->scroll_counter = 0;
                if(item->change_callback) {
                    item->change_callback(item);
                }
            }
        },
        true);
}

void variable_item_list_process_right(VariableItemList* variable_item_list) {
    with_view_model(
        variable_item_list->view,
        VariableItemListModel * model,
        {
            VariableItem* item = variable_item_list_get_selected_item(model);
            if(item->locked) {
                model->locked_message_visible = true;
                furi_timer_start(
                    variable_item_list->locked_timer, furi_kernel_get_tick_frequency() * 3);
            } else if(item->current_value_index < (item->values_count - 1)) {
                item->current_value_index++;
                model->scroll_counter = 0;
                if(item->change_callback) {
                    item->change_callback(item);
                }
            }
        },
        true);
}

void variable_item_list_process_ok(VariableItemList* variable_item_list) {
    with_view_model(
        variable_item_list->view,
        VariableItemListModel * model,
        {
            VariableItem* item = variable_item_list_get_selected_item(model);
            // OK belongs to the control of an item whose values the list knows: it turns a
            // switch over, moves to the next button, opens the list of a dropdown or a meter.
            // Arrows (an app that gave no texts) leave it to the app, as always
            bool on = false;
            if(item->locked) {
                model->locked_message_visible = true;
                furi_timer_start(
                    variable_item_list->locked_timer, furi_kernel_get_tick_frequency() * 3);
            } else if(item->control == VariableItemControlSwitch && variable_item_is_switch(item, &on)) {
                item->current_value_index = !item->current_value_index;
                model->scroll_counter = 0;
                if(item->change_callback) item->change_callback(item);
            } else if(item->control == VariableItemControlSegments && item->values_count > 1) {
                item->current_value_index = (item->current_value_index + 1) % item->values_count;
                model->scroll_counter = 0;
                if(item->change_callback) item->change_callback(item);
            } else if(
                (item->control == VariableItemControlDropdown ||
                 item->control == VariableItemControlMeter) &&
                item->values_text) {
                model->dropdown_open = true;
                model->dropdown_index = item->current_value_index;
                model->dropdown_window = 0;
                // the hint bar covers the bottom row: the item scrolls up to stay in sight
                if(model->position > model->window_position + 2) {
                    model->window_position = model->position - 2;
                }
                const uint8_t rows = MIN(item->values_count, DROPDOWN_ROWS);
                if(model->dropdown_index >= rows) model->dropdown_window = model->dropdown_index - rows + 1;
            } else if(variable_item_list->callback) {
                variable_item_list->callback(variable_item_list->context, model->position);
            }
        },
        true);
}

static void variable_item_list_scroll_timer_callback(void* context) {
    VariableItemList* variable_item_list = context;
    // three times a second: drawn again only when something on screen moves
    bool redraw = false;
    with_view_model(
        variable_item_list->view,
        VariableItemListModel * model,
        {
            model->scroll_counter++;
            redraw = model->selected_scrolls;
        },
        redraw);
}

static void variable_item_list_enter_callback(void* context) {
    VariableItemList* variable_item_list = context;
    with_view_model(
        variable_item_list->view,
        VariableItemListModel * model,
        {
            model->scroll_counter = 0;
            model->dropdown_open = false;
        },
        false);
    furi_timer_start(variable_item_list->scroll_timer, SCROLL_INTERVAL);
}

static void variable_item_list_exit_callback(void* context) {
    VariableItemList* variable_item_list = context;
    furi_timer_stop(variable_item_list->scroll_timer);
}

void variable_item_list_locked_timer_callback(void* context) {
    furi_assert(context);
    VariableItemList* variable_item_list = context;

    with_view_model(
        variable_item_list->view,
        VariableItemListModel * model,
        { model->locked_message_visible = false; },
        true);
}

VariableItemList* variable_item_list_alloc(void) {
    VariableItemList* variable_item_list = malloc(sizeof(VariableItemList));
    variable_item_list->view = view_alloc();
    view_set_context(variable_item_list->view, variable_item_list);
    view_allocate_model(
        variable_item_list->view, ViewModelTypeLocking, sizeof(VariableItemListModel));
    view_set_draw_callback(variable_item_list->view, variable_item_list_draw_callback);
    view_set_input_callback(variable_item_list->view, variable_item_list_input_callback);
    view_set_enter_callback(variable_item_list->view, variable_item_list_enter_callback);
    view_set_exit_callback(variable_item_list->view, variable_item_list_exit_callback);

    variable_item_list->locked_timer = furi_timer_alloc(
        variable_item_list_locked_timer_callback, FuriTimerTypeOnce, variable_item_list);

    with_view_model(
        variable_item_list->view,
        VariableItemListModel * model,
        {
            VariableItemArray_init(model->items);
            model->position = 0;
            model->window_position = 0;
            model->scroll_counter = 0;
        },
        true);
    // Started on view enter, not here: a list that is allocated but not on screen asked the GUI
    // to repaint whatever was, three times a second, for as long as its owner lived
    variable_item_list->scroll_timer = furi_timer_alloc(
        variable_item_list_scroll_timer_callback, FuriTimerTypePeriodic, variable_item_list);

    return variable_item_list;
}

void variable_item_list_free(VariableItemList* variable_item_list) {
    furi_check(variable_item_list);

    with_view_model(
        variable_item_list->view,
        VariableItemListModel * model,
        {
            VariableItemArray_it_t it;
            for(VariableItemArray_it(it, model->items); !VariableItemArray_end_p(it);
                VariableItemArray_next(it)) {
                furi_string_free(VariableItemArray_ref(it)->current_value_text);
                furi_string_free(VariableItemArray_ref(it)->locked_message);
            }
            VariableItemArray_clear(model->items);
        },
        false);
    furi_timer_stop(variable_item_list->scroll_timer);
    furi_timer_free(variable_item_list->scroll_timer);
    furi_timer_stop(variable_item_list->locked_timer);
    furi_timer_free(variable_item_list->locked_timer);
    view_free(variable_item_list->view);
    free(variable_item_list);
}

void variable_item_list_reset(VariableItemList* variable_item_list) {
    furi_check(variable_item_list);

    with_view_model(
        variable_item_list->view,
        VariableItemListModel * model,
        {
            VariableItemArray_it_t it;
            for(VariableItemArray_it(it, model->items); !VariableItemArray_end_p(it);
                VariableItemArray_next(it)) {
                furi_string_free(VariableItemArray_ref(it)->current_value_text);
                furi_string_free(VariableItemArray_ref(it)->locked_message);
            }
            VariableItemArray_reset(model->items);
            model->dropdown_open = false;
        },
        false);
}

View* variable_item_list_get_view(VariableItemList* variable_item_list) {
    furi_check(variable_item_list);
    return variable_item_list->view;
}

VariableItem* variable_item_list_add(
    VariableItemList* variable_item_list,
    const char* label,
    uint8_t values_count,
    VariableItemChangeCallback change_callback,
    void* context) {
    VariableItem* item = NULL;
    furi_check(label);
    furi_check(variable_item_list);

    with_view_model(
        variable_item_list->view,
        VariableItemListModel * model,
        {
            item = VariableItemArray_push_new(model->items);
            item->label = label;
            item->values_count = values_count;
            item->change_callback = change_callback;
            item->context = context;
            item->current_value_index = 0;
            item->current_value_text = furi_string_alloc();
            item->values_text = NULL;
            item->control = VariableItemControlArrows;
            item->locked = false;
            item->locked_message = furi_string_alloc();
        },
        true);

    return item;
}

VariableItem* variable_item_list_get(VariableItemList* variable_item_list, uint8_t position) {
    VariableItem* item = NULL;
    furi_assert(variable_item_list);

    with_view_model(
        variable_item_list->view,
        VariableItemListModel * model,
        {
            if(position < VariableItemArray_size(model->items)) {
                item = VariableItemArray_get(model->items, position);
            }
        },
        true);

    return item;
}

void variable_item_list_set_enter_callback(
    VariableItemList* variable_item_list,
    VariableItemListEnterCallback callback,
    void* context) {
    furi_check(callback);
    with_view_model(
        variable_item_list->view,
        VariableItemListModel * model,
        {
            UNUSED(model);
            variable_item_list->callback = callback;
            variable_item_list->context = context;
        },
        false);
}

void variable_item_set_current_value_index(VariableItem* item, uint8_t current_value_index) {
    furi_check(item);
    item->current_value_index = current_value_index;
}

void variable_item_set_values_count(VariableItem* item, uint8_t values_count) {
    furi_check(item);
    item->values_count = values_count;
}

void variable_item_set_item_label(VariableItem* item, const char* label) {
    furi_check(item);
    furi_check(label);
    item->label = label;
}

void variable_item_set_current_value_text(VariableItem* item, const char* current_value_text) {
    furi_check(item);
    furi_string_set(item->current_value_text, current_value_text);
}

void variable_item_set_locked(VariableItem* item, bool locked, const char* locked_message) {
    item->locked = locked;
    if(locked) {
        furi_assert(locked_message);
        furi_string_set(item->locked_message, locked_message);
    }
}

uint8_t variable_item_get_current_value_index(VariableItem* item) {
    furi_check(item);
    return item->current_value_index;
}

void* variable_item_get_context(VariableItem* item) {
    furi_check(item);
    return item->context;
}

void variable_item_set_values_text(VariableItem* item, const char* const* values_text) {
    furi_check(item);
    item->values_text = values_text;
}
