#include "variable_item_list.h"
#include <gui/elements.h>
#include <gui/canvas.h>
#include "../canvas_i.h"
#include <furi.h>
#include <assets_icons.h>
#include <m-array.h>
#include <stdint.h>
#include <strings.h>

#define SCROLL_INTERVAL (333)

// Controls: how an item's value is drawn, chosen from what the item is (see the header)
#define VALUE_RIGHT        122 // the value area's right end: the selection's last column
#define CONTROL_RIGHT      (VALUE_RIGHT - 3) // a control's last column: two clear inside the selection
#define SWITCH_W           17
#define SWITCH_H           9
#define METER_MIN_VALUES   5 // a numeric value with this many steps or more gets a meter
#define SEGMENTS_MAX       3
#define LEVELS_MAX         10 // levels (bare 1..N after an Off) drawn as bars, at most this many
#define SEGMENT_PAD        3 // either side of a segment's text
#define SEGMENTS_WIDTH_MAX 64
#define DROPDOWN_ROWS      3
// a dropdown row: its selection, two clear rows over the capitals and one under the descenders
#define DROPDOWN_ROW_H     12
#define TEXT_PAD           3 // either side of a text, inside what holds it
#define ITEM_H             16
#define ITEMS_ON_SCREEN    4
#define INDENT_W           8 // an indented item's label, per level
#define INDENT_MAX         2
#define SLIDE_INTERVAL     20 // ms between the frames of a row sliding in or out
#define OPENS_VALUE_RIGHT  112 // a row that opens a page: its value ends here, before the ">"
#define OPENS_VALUE_MAX    72 // and takes at most this much of the row; a longer one scrolls

typedef enum {
    VariableItemControlArrows, // < value >, as Flipper always drew it
    VariableItemControlSwitch, // on or off
    VariableItemControlMeter, // a number, with how far along its range it is
    VariableItemControlSegments, // two or three short choices side by side
    VariableItemControlDropdown, // OK opens the choices as a list
    VariableItemControlOpens, // OK opens a page for it: value then ">" (variable_item_set_opens)
    VariableItemControlLevels, // Off, 1, 2... N: rising bars, as a volume or a signal
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
    bool hidden;
    bool opens;
    uint8_t indent;
    uint8_t height; // as drawn: ITEM_H when shown, 0 when hidden, between while it slides
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
    FuriTimer* slide_timer;
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
    // rows take their heights at once on the next draw (a new list, or one coming on screen)
    bool snap;
    FuriTimer* slide_timer; // the list's: the draw starts it while a row slides
    FuriString* header; // Hadal's header over the rows, as a submenu has; empty: none
} VariableItemListModel;

/** Rows a screen holds: one fewer under a header */
static size_t variable_item_list_rows(const VariableItemListModel* model) {
    return furi_string_empty(model->header) ? ITEMS_ON_SCREEN : ITEMS_ON_SCREEN - 1;
}

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

/** Off (or 0), then 1, 2... with no unit: levels, drawn as rising bars. A unit or a percentage
 * (a brightness, a time) keeps the meter's line, which shows how far along a range it is */
static bool variable_item_is_levels(const VariableItem* item) {
    if(!item->values_text || item->values_count < 3 || item->values_count > LEVELS_MAX + 1) {
        return false;
    }
    const char* first = item->values_text[0];
    if(!variable_item_text_is(first, switch_off_words) && strcmp(first, "0") != 0) return false;
    for(uint8_t i = 1; i < item->values_count; i++) {
        const char* text = item->values_text[i];
        if(atoi(text) != i) return false;
        for(; *text; text++) {
            if(*text < '0' || *text > '9') return false;
        }
    }
    return true;
}

/** The bars of a level, centred between x0 and x1, their feet on the baseline: as many as there
 * are levels, rising; the ones up to the level filled, the others a dot at their foot */
static void variable_item_draw_levels(
    Canvas* canvas,
    const VariableItem* item,
    int32_t x0,
    int32_t x1,
    int32_t baseline) {
    const int32_t count = item->values_count - 1;
    const int32_t width = count * 3 - 1;
    int32_t x = x0 + (x1 - x0 + 1 - width) / 2;
    for(int32_t i = 1; i <= count; i++, x += 3) {
        const int32_t height = 2 + (i - 1) * 6 / (count > 1 ? count - 1 : 1);
        if(i <= item->current_value_index) {
            canvas_draw_box(canvas, x, baseline - height + 1, 2, height);
        } else {
            canvas_draw_dot(canvas, x, baseline);
        }
    }
}

/** The segments' total width, or 0 when they do not fit in width */
static uint16_t
    variable_item_segments_width(Canvas* canvas, const VariableItem* item, uint16_t width) {
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
    if(item->opens) return VariableItemControlOpens;
    if(!item->values_text || item->values_count < 2) return VariableItemControlArrows;
    if(variable_item_is_switch(item, NULL)) return VariableItemControlSwitch;
    if(variable_item_is_levels(item)) return VariableItemControlLevels;
    if(item->values_count >= METER_MIN_VALUES && variable_item_is_number(item)) {
        return VariableItemControlMeter;
    }
    if(item->values_count <= SEGMENTS_MAX && variable_item_segments_width(canvas, item, width)) {
        return VariableItemControlSegments;
    }
    return VariableItemControlDropdown;
}

/** The guides of a group: a dotted line from just under the first letter of the item the group
 * is under, down through its rows, ending at the last one's text with a tick to it. Dots on odd
 * rows, the same in every row, so they line up from row to row */
#define GUIDE_TICK_Y 9 // the tick, in the middle of the text's lowercase

static int32_t variable_item_guide_x(uint8_t level) {
    return 6 + (level - 1) * INDENT_W + 1; // under the first letter of a label at level - 1
}

/** The top of a group's line, in the row of the item it is under: under its first letter */
static void variable_item_draw_guide_start(
    Canvas* canvas,
    const VariableItem* item,
    int32_t item_y,
    Color ink) {
    const int32_t x = variable_item_guide_x(item->indent + 1);
    canvas_set_color(canvas, ink); // in the selection, if it is selected
    canvas_draw_dot(canvas, x, item_y + 13);
    canvas_set_color(canvas, ColorBlack); // under the selection, whatever
    canvas_draw_dot(canvas, x, item_y + 15);
    canvas_set_color(canvas, ink);
}

static void variable_item_draw_guides(
    Canvas* canvas,
    const VariableItem* item,
    uint8_t next_indent,
    int32_t item_y) {
    const int32_t tick = item_y + GUIDE_TICK_Y;
    for(uint8_t level = 1; level <= item->indent; level++) {
        const int32_t x = variable_item_guide_x(level);
        const int32_t bottom = next_indent >= level ? item_y + ITEM_H - 1 : tick;
        for(int32_t y = item_y + 1; y <= bottom; y += 2) {
            canvas_draw_dot(canvas, x, y);
        }
        if(level == item->indent) {
            for(int32_t dx = 2; dx < INDENT_W - 2; dx += 2) {
                canvas_draw_dot(canvas, x + dx, tick);
            }
        }
    }
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
    const int32_t fill =
        item->values_count > 1 ? width * item->current_value_index / (item->values_count - 1) : 0;
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
    canvas_draw_rframe(
        canvas, left + 1, y, width - 2, height, 3); // a heavier side edge: it floats

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
            canvas,
            right - 2,
            y + 2,
            rows * DROPDOWN_ROW_H,
            model->dropdown_index,
            item->values_count);
    }
    // the control centre's picker's words: Up and Down browse, OK chooses
    elements_hint_bar(canvas, "ud=Browse;ok=Choose;back=Cancel");
}

/* ---- hidden items ----
 * The cursor and the window are items' indexes, as before, so the apps' indexes still hold;
 * where they sit among the shown items (their rank) is what moving and the window go by */

static bool variable_item_list_hidden(const VariableItemListModel* model, size_t index) {
    return VariableItemArray_cget(model->items, index)->hidden;
}

/** How many shown items come before index */
static size_t variable_item_list_rank(const VariableItemListModel* model, size_t index) {
    size_t rank = 0;
    const size_t count = MIN(index, VariableItemArray_size(model->items));
    for(size_t i = 0; i < count; i++) {
        rank += !variable_item_list_hidden(model, i);
    }
    return rank;
}

/** The shown item of that rank (the last shown one past the end) */
static size_t variable_item_list_at_rank(const VariableItemListModel* model, size_t rank) {
    const size_t count = VariableItemArray_size(model->items);
    size_t last = 0;
    for(size_t i = 0; i < count; i++) {
        if(variable_item_list_hidden(model, i)) continue;
        if(!rank--) return i;
        last = i;
    }
    return last;
}

/** The window over the cursor as Up and Down always kept it: a row above the cursor and one
 * under it while there are more, and no empty rows at the bottom (bar under an open dropdown,
 * which moves the cursor up out from under its hint bar) */
static void variable_item_list_fit_window(VariableItemListModel* model) {
    const size_t shown = variable_item_list_rank(model, VariableItemArray_size(model->items));
    const size_t cursor = variable_item_list_rank(model, model->position);
    size_t window = variable_item_list_rank(model, model->window_position);
    const size_t rows = variable_item_list_rows(model);
    size_t last = shown > rows ? shown - rows : 0;
    // the cursor's row: never the last on screen while more follow, nor under a dropdown's hints
    const size_t low = rows - 2;
    if(model->dropdown_open && cursor > low) last = MAX(last, cursor - low);
    if(cursor > window + low) window = cursor - low;
    if(cursor < window + 1) window = cursor ? cursor - 1 : 0;
    window = MIN(window, last);
    model->window_position = variable_item_list_at_rank(model, window);
}

/** Before a draw: rows snapped to their heights if the list is new, the cursor off a hidden item
 * (to the one above it: an item hides those under it), the window over it. Returns whether a row
 * is still sliding */
static bool variable_item_list_settle(VariableItemListModel* model) {
    const size_t count = VariableItemArray_size(model->items);
    bool sliding = false;
    for(size_t i = 0; i < count; i++) {
        VariableItem* item = VariableItemArray_get(model->items, i);
        const uint8_t height = item->hidden ? 0 : ITEM_H;
        if(model->snap) item->height = height;
        sliding |= item->height != height;
    }
    model->snap = false;
    if(!count) return false;
    if(model->position >= count) model->position = 0;
    size_t position = model->position;
    while(position > 0 && variable_item_list_hidden(model, position))
        position--;
    while(position + 1 < count && variable_item_list_hidden(model, position))
        position++;
    model->position = position;
    variable_item_list_fit_window(model);
    return sliding;
}

/** A frame of the rows sliding: each a step nearer its height, quick at first, then slowing */
static void variable_item_list_slide_timer_callback(void* context) {
    VariableItemList* variable_item_list = context;
    with_view_model(
        variable_item_list->view,
        VariableItemListModel * model,
        {
            VariableItemArray_it_t it;
            for(VariableItemArray_it(it, model->items); !VariableItemArray_end_p(it);
                VariableItemArray_next(it)) {
                VariableItem* item = VariableItemArray_ref(it);
                const uint8_t height = item->hidden ? 0 : ITEM_H;
                if(item->height < height) {
                    item->height += (height - item->height + 1) / 2;
                } else if(item->height > height) {
                    item->height -= (item->height - height + 1) / 2;
                }
            }
        },
        true);
}

static void variable_item_list_draw_callback(Canvas* canvas, void* _model) {
    VariableItemListModel* model = _model;

    const uint8_t item_height = ITEM_H;
    const uint8_t item_width = 123;

    canvas_clear(canvas);
    // the next frame, if a row is still on its way: started here, where every change shows
    if(variable_item_list_settle(model) && model->slide_timer) {
        furi_timer_start(model->slide_timer, furi_ms_to_ticks(SLIDE_INTERVAL));
    }

    int32_t selected_y = -1;
    // Hadal's header, as a submenu's: the bold title, a line out to Abyss's light
    const int32_t top = furi_string_empty(model->header) ? 0 : ELEMENTS_HEADER_HEIGHT;
    if(top) elements_header(canvas, furi_string_get_cstr(model->header));
    int32_t rows_end = top; // where the rows drawn so far end
    const int32_t screen_y = canvas->offset_y;

    canvas_set_font(canvas, FontSecondary);
    for(size_t position = model->window_position;
        position < VariableItemArray_size(model->items) && rows_end < 64;
        position++) {
        {
            VariableItem* item = VariableItemArray_get(model->items, position);
            if(!item->height) continue;
            // a row sliding in comes out from under the one above: drawn whole, ending where its
            // row ends, and only its row shows
            const int32_t row_top = rows_end;
            rows_end += item->height;
            const int32_t item_y = rows_end - item_height;
            const bool clipped = item->height < item_height;
            if(clipped) {
                u8g2_SetClipWindow(
                    &canvas->fb, 0, screen_y + row_top, 128, screen_y + MIN(rows_end, 64));
            }
            const int32_t item_text_y = item_y + item_height - 4;
            const int32_t indent = item->indent * INDENT_W;
            // whether the group goes on under this row: the next row's indent
            uint8_t next_indent = 0;
            for(size_t next = position + 1; next < VariableItemArray_size(model->items); next++) {
                const VariableItem* below = VariableItemArray_cget(model->items, next);
                if(below->height) {
                    next_indent = below->indent;
                    break;
                }
            }
            size_t scroll_counter = 0;

            if(position == model->position) {
                canvas_set_color(canvas, ColorBlack);
                canvas_draw_rbox(canvas, 0, item_y + 1, item_width, item_height - 2, 3);
                canvas_set_color(canvas, ColorWhite);
                scroll_counter = elements_scroll_position(model->scroll_counter);
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
            uint16_t opens_value_w = 0;
            if(control == VariableItemControlOpens) {
                // the value right-aligned before the ">", the label in the room it leaves
                opens_value_w = MIN(
                    canvas_string_width(canvas, furi_string_get_cstr(item->current_value_text)),
                    OPENS_VALUE_MAX);
                temp_w_default = OPENS_VALUE_RIGHT - opens_value_w - 4 - 6;
            } else if(control == VariableItemControlSwitch) {
                // the label has the room the arrows took
                temp_w_default = CONTROL_RIGHT + 1 - SWITCH_W - 4 - 6;
            } else if(control == VariableItemControlSegments) {
                segments = variable_item_segments_width(canvas, item, SEGMENTS_WIDTH_MAX);
                temp_w_default = CONTROL_RIGHT + 1 - segments - 4 - 6;
            }

            temp_w_default -= indent; // an indented label starts further in, ending where it did

            if(position == model->position) {
                selected_y = item_y;
                const size_t value_room =
                    control == VariableItemControlOpens ?
                        (size_t)OPENS_VALUE_MAX :
                    control == VariableItemControlDropdown ?
                        (size_t)(CONTROL_RIGHT - 9 - temp_x_default) :
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
                canvas_draw_str(canvas, 6 + indent, item_text_y, item->label);
            } else {
                elements_scrollable_text_line_str(
                    canvas,
                    6 + indent,
                    item_text_y,
                    temp_w_default,
                    item->label,
                    scroll_counter,
                    false,
                    false);
            }

            if(item->indent) variable_item_draw_guides(canvas, item, next_indent, item_y);
            if(next_indent > item->indent) {
                variable_item_draw_guide_start(canvas, item, item_y, ink);
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
            } else if(control == VariableItemControlLevels) {
                // "Off" in words at nothing; else the bars, between the arrows as always
                if(item->current_value_index > 0) {
                    canvas_draw_str(canvas, temp_x_default, item_text_y, "<");
                    variable_item_draw_levels(
                        canvas, item, temp_x_default + 6, 112, item_text_y - 1);
                } else {
                    canvas_draw_str_aligned(
                        canvas,
                        (115 + temp_x_default) / 2 + 1,
                        item_text_y,
                        AlignCenter,
                        AlignBottom,
                        furi_string_get_cstr(item->current_value_text));
                }
                if(item->current_value_index < item->values_count - 1) {
                    canvas_draw_str(canvas, 115, item_text_y, ">");
                }
            } else if(control == VariableItemControlOpens) {
                elements_scrollable_text_line_str(
                    canvas,
                    OPENS_VALUE_RIGHT - opens_value_w,
                    item_text_y,
                    opens_value_w,
                    furi_string_get_cstr(item->current_value_text),
                    scroll_counter,
                    false,
                    false);
                canvas_draw_str(canvas, 115, item_text_y, ">");
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
                canvas_draw_line(
                    canvas, chevron + 3, item_text_y - 2, chevron + 6, item_text_y - 5);
                canvas_draw_line(canvas, chevron, item_text_y - 4, chevron + 3, item_text_y - 1);
                canvas_draw_line(
                    canvas, chevron + 3, item_text_y - 1, chevron + 6, item_text_y - 4);
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
            if(clipped) u8g2_SetMaxClipWindow(&canvas->fb);
        }
    }

    if(top) {
        elements_scrollbar_pos(
            canvas,
            canvas_width(canvas),
            top,
            canvas_height(canvas) - top,
            variable_item_list_rank(model, model->position),
            variable_item_list_rank(model, VariableItemArray_size(model->items)));
    } else {
        elements_scrollbar(
            canvas,
            variable_item_list_rank(model, model->position),
            variable_item_list_rank(model, VariableItemArray_size(model->items)));
    }

    if(model->dropdown_open && model->position < VariableItemArray_size(model->items)) {
        const VariableItem* item = VariableItemArray_cget(model->items, model->position);
        const uint8_t item_y = selected_y >= 0 ? selected_y : 0;
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
            const size_t count = VariableItemArray_size(model->items);
            model->position = index < count ? index : 0;
            // off a hidden one, then the window from a row above it
            variable_item_list_settle(model);
            const size_t cursor = variable_item_list_rank(model, model->position);
            model->window_position = variable_item_list_at_rank(model, cursor ? cursor - 1 : 0);
            variable_item_list_fit_window(model);
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
static bool
    variable_item_list_dropdown_input(VariableItemList* variable_item_list, InputEvent* event) {
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
    bool empty = false;
    with_view_model(
        variable_item_list->view,
        VariableItemListModel * model,
        {
            dropdown_open = model->dropdown_open;
            empty = VariableItemArray_empty_p(model->items);
        },
        false);
    if(empty) return false; // nothing to move to or change: Back still leaves
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

/** The cursor to the shown item before or after it, round from either end; the window follows */
static void variable_item_list_move(VariableItemList* variable_item_list, bool down) {
    with_view_model(
        variable_item_list->view,
        VariableItemListModel * model,
        {
            const size_t shown =
                variable_item_list_rank(model, VariableItemArray_size(model->items));
            if(shown) {
                const size_t cursor = variable_item_list_rank(model, model->position);
                const size_t next = down ? (cursor + 1) % shown :
                                           (cursor ? cursor - 1 : shown - 1);
                model->position = variable_item_list_at_rank(model, next);
                if(!next) model->window_position = 0;
                variable_item_list_fit_window(model);
            }
            model->scroll_counter = 0;
        },
        true);
}

void variable_item_list_process_up(VariableItemList* variable_item_list) {
    variable_item_list_move(variable_item_list, false);
}

void variable_item_list_process_down(VariableItemList* variable_item_list) {
    variable_item_list_move(variable_item_list, true);
}

VariableItem* variable_item_list_get_selected_item(VariableItemListModel* model) {
    furi_check(model->position < VariableItemArray_size(model->items));
    return VariableItemArray_get(model->items, model->position);
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
            } else if(
                item->control == VariableItemControlSwitch && variable_item_is_switch(item, &on)) {
                item->current_value_index = !item->current_value_index;
                model->scroll_counter = 0;
                if(item->change_callback) item->change_callback(item);
            } else if(item->control == VariableItemControlSegments && item->values_count > 1) {
                item->current_value_index = (item->current_value_index + 1) % item->values_count;
                model->scroll_counter = 0;
                if(item->change_callback) item->change_callback(item);
            } else if(
                (item->control == VariableItemControlDropdown ||
                 item->control == VariableItemControlMeter ||
                 item->control == VariableItemControlLevels) &&
                item->values_text) {
                model->dropdown_open = true;
                model->dropdown_index = item->current_value_index;
                model->dropdown_window = 0;
                // the hint bar covers the bottom row: the item scrolls up to stay in sight
                variable_item_list_fit_window(model);
                const uint8_t rows = MIN(item->values_count, DROPDOWN_ROWS);
                if(model->dropdown_index >= rows)
                    model->dropdown_window = model->dropdown_index - rows + 1;
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
        { redraw = elements_scroll_step(&model->scroll_counter) && model->selected_scrolls; },
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
            model->snap = true; // what changed off screen is simply there
        },
        false);
    furi_timer_start(variable_item_list->scroll_timer, SCROLL_INTERVAL);
}

static void variable_item_list_exit_callback(void* context) {
    VariableItemList* variable_item_list = context;
    furi_timer_stop(variable_item_list->scroll_timer);
    furi_timer_stop(variable_item_list->slide_timer);
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
    variable_item_list->slide_timer = furi_timer_alloc(
        variable_item_list_slide_timer_callback, FuriTimerTypeOnce, variable_item_list);

    with_view_model(
        variable_item_list->view,
        VariableItemListModel * model,
        {
            VariableItemArray_init(model->items);
            model->position = 0;
            model->window_position = 0;
            model->scroll_counter = 0;
            model->snap = true;
            model->slide_timer = variable_item_list->slide_timer;
            model->header = furi_string_alloc();
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
            furi_string_free(model->header);
        },
        false);
    furi_timer_stop(variable_item_list->scroll_timer);
    furi_timer_free(variable_item_list->scroll_timer);
    furi_timer_stop(variable_item_list->locked_timer);
    furi_timer_free(variable_item_list->locked_timer);
    furi_timer_stop(variable_item_list->slide_timer);
    furi_timer_free(variable_item_list->slide_timer);
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
            furi_string_reset(model->header); // as a submenu's: each page sets its own
            model->dropdown_open = false;
            model->snap = true;
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
            item->hidden = false;
            item->opens = false;
            item->indent = 0;
            item->height = ITEM_H;
            item->locked_message = furi_string_alloc();
            model->snap = true; // the list is being built: its rows show as they are
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

void variable_item_list_set_header(VariableItemList* variable_item_list, const char* header) {
    furi_check(variable_item_list);
    with_view_model(
        variable_item_list->view,
        VariableItemListModel * model,
        {
            furi_string_set_str(model->header, header ? header : "");
            // a row fewer: the window settles over the cursor again
            if(VariableItemArray_size(model->items)) variable_item_list_fit_window(model);
        },
        true);
}

void variable_item_set_opens(VariableItem* item, bool opens) {
    furi_check(item);
    item->opens = opens;
}

void variable_item_set_hidden(VariableItem* item, bool hidden) {
    furi_check(item);
    item->hidden = hidden; // the next draw slides it, and moves the cursor off it
}

void variable_item_set_indent(VariableItem* item, uint8_t level) {
    furi_check(item);
    item->indent = MIN(level, INDENT_MAX);
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
