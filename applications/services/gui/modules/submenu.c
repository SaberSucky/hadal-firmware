#include "submenu.h"

#include <assets_icons.h>
#include <gui/elements.h>
#include <furi.h>
#include <m-array.h>

// A selected label too long for its row scrolls, a step this often, after a pause at its start
#define SUBMENU_SCROLL_INTERVAL_MS (333U)
#define SUBMENU_SCROLL_PAUSE       (2U)

struct Submenu {
    View* view;

    FuriTimer* locked_timer;
    FuriTimer* scroll_timer;
};

typedef struct {
    FuriString* label;
    uint32_t index;
    union {
        SubmenuItemCallback callback;
        SubmenuItemCallbackEx callback_ex;
    };
    void* callback_context;
    bool has_extended_events;

    bool locked;
    FuriString* locked_message;
} SubmenuItem;

static void SubmenuItem_init(SubmenuItem* item) {
    item->label = furi_string_alloc();
    item->index = 0;
    item->callback = NULL;
    item->callback_context = NULL;
    item->locked = false;
    item->locked_message = furi_string_alloc();
}

static void SubmenuItem_init_set(SubmenuItem* item, const SubmenuItem* src) {
    item->label = furi_string_alloc_set(src->label);
    item->index = src->index;
    item->callback = src->callback;
    item->callback_context = src->callback_context;
    item->locked = src->locked;
    item->locked_message = furi_string_alloc_set(src->locked_message);
}

static void SubmenuItem_set(SubmenuItem* item, const SubmenuItem* src) {
    furi_string_set(item->label, src->label);
    item->index = src->index;
    item->callback = src->callback;
    item->callback_context = src->callback_context;
    item->locked = src->locked;
    furi_string_set(item->locked_message, src->locked_message);
}

static void SubmenuItem_clear(SubmenuItem* item) {
    furi_string_free(item->label);
    furi_string_free(item->locked_message);
}

ARRAY_DEF(
    SubmenuItemArray,
    SubmenuItem,
    (INIT(API_2(SubmenuItem_init)),
     SET(API_6(SubmenuItem_set)),
     INIT_SET(API_6(SubmenuItem_init_set)),
     CLEAR(API_2(SubmenuItem_clear))))

typedef struct {
    SubmenuItemArray_t items;
    FuriString* header;
    FuriString* hints; /**< the hint bar's hints; empty: no hint bar */
    size_t position;
    size_t window_position;

    bool locked_message_visible;
    bool is_vertical;

    size_t scroll_counter; /**< steps since the selection last moved */
    bool selected_scrolls; /**< the selected label is longer than its row */
} SubmenuModel;

static void submenu_process_up(Submenu* submenu);
static void submenu_process_down(Submenu* submenu);
static void submenu_process_page(Submenu* submenu, bool down);
static void submenu_process_ok(Submenu* submenu, InputType input_type);

// With a hint bar the rows are shorter, so that as many still fit above it: a 12 pixel selection
// with two clear rows over the capitals and one under the descenders, and a row between rows. The
// header then has no rule of its own (the hint bar has one), only clear rows under it.
#define SUBMENU_ITEM_HEIGHT         16
#define SUBMENU_COMPACT_ITEM_HEIGHT 13
#define SUBMENU_COMPACT_HEADER      12

static bool submenu_has_hints(const SubmenuModel* model) {
    return !model->is_vertical && !furi_string_empty(model->hints);
}

static size_t submenu_items_on_screen(SubmenuModel* model) {
    if(submenu_has_hints(model)) {
        // above the hint bar, with a clear row over its rule
        const size_t top = furi_string_empty(model->header) ? 0 : SUBMENU_COMPACT_HEADER;
        return (64 - ELEMENTS_HINT_BAR_HEIGHT - 1 - top) / SUBMENU_COMPACT_ITEM_HEIGHT;
    }
    size_t res = (model->is_vertical) ? 8 : 4;
    return (furi_string_empty(model->header)) ? res : res - 1;
}

static void submenu_view_draw_callback(Canvas* canvas, void* _model) {
    SubmenuModel* model = _model;

    const bool hints = submenu_has_hints(model);
    const uint8_t item_height = hints ? SUBMENU_COMPACT_ITEM_HEIGHT : SUBMENU_ITEM_HEIGHT;
    const uint8_t header_height = hints ? SUBMENU_COMPACT_HEADER : SUBMENU_ITEM_HEIGHT;
    uint8_t item_width = canvas_width(canvas) - 5;

    canvas_clear(canvas);

    if(!furi_string_empty(model->header)) {
        // Hadal's header: the bold title, a line out to Abyss's light
        elements_header(canvas, furi_string_get_cstr(model->header));
    }

    canvas_set_font(canvas, FontSecondary);

    size_t position = 0;
    SubmenuItemArray_it_t it;
    for(SubmenuItemArray_it(it, model->items); !SubmenuItemArray_end_p(it);
        SubmenuItemArray_next(it)) {
        const size_t item_position = position - model->window_position;
        const size_t items_on_screen = submenu_items_on_screen(model);
        uint8_t y_offset = furi_string_empty(model->header) ? 0 : header_height;
        bool is_locked = SubmenuItemArray_cref(it)->locked;

        if(item_position < items_on_screen) {
            if(position == model->position) {
                canvas_set_color(canvas, ColorBlack);
                canvas_draw_rbox(
                    canvas,
                    0,
                    y_offset + (item_position * item_height) + 1,
                    item_width,
                    item_height - (hints ? 1 : 2),
                    3);
                canvas_set_color(canvas, ColorWhite);
            } else {
                canvas_set_color(canvas, ColorBlack);
            }

            if(is_locked) {
                canvas_draw_icon(
                    canvas,
                    item_width - 10,
                    y_offset + (item_position * item_height) + item_height - (hints ? 10 : 12),
                    &I_Lock_7x8);
            }

            const size_t text_width = item_width - (is_locked ? 21 : 11);
            const char* label = furi_string_get_cstr(SubmenuItemArray_cref(it)->label);
            const uint8_t text_y =
                y_offset + (item_position * item_height) + item_height - (hints ? 3 : 4);
            // (compact: capitals 3 rows under the row's top, descenders a row above its selection's
            // bottom; as before: the stock layout)
            if(position == model->position && canvas_string_width(canvas, label) > text_width) {
                // the selected one scrolls, so the whole of it can be read
                model->selected_scrolls = true;
                const size_t counter = elements_scroll_position(model->scroll_counter);
                const size_t scroll =
                    counter > SUBMENU_SCROLL_PAUSE ? counter - SUBMENU_SCROLL_PAUSE : 0;
                elements_scrollable_text_line_str(
                    canvas, 6, text_y, text_width, label, scroll, false, false);
            } else {
                if(position == model->position) model->selected_scrolls = false;
                FuriString* disp_str = furi_string_alloc_set_str(label);
                elements_string_fit_width(canvas, disp_str, text_width);
                canvas_draw_str(canvas, 6, text_y, furi_string_get_cstr(disp_str));
                furi_string_free(disp_str);
            }
        }

        position++;
    }

    if(hints) {
        const uint8_t top = furi_string_empty(model->header) ? 0 : header_height;
        elements_scrollbar_pos(
            canvas,
            canvas_width(canvas),
            top,
            canvas_height(canvas) - ELEMENTS_HINT_BAR_HEIGHT - 1 - top,
            model->position,
            SubmenuItemArray_size(model->items));
        elements_hint_bar(canvas, furi_string_get_cstr(model->hints));
    } else {
        elements_scrollbar(canvas, model->position, SubmenuItemArray_size(model->items));
    }

    if(model->locked_message_visible) {
        const uint8_t frame_x = 7;
        const uint8_t frame_width = canvas_width(canvas) - frame_x * 2;
        const uint8_t frame_y = 7;
        const uint8_t frame_height = canvas_height(canvas) - frame_y * 2;

        canvas_set_color(canvas, ColorWhite);
        canvas_draw_box(canvas, frame_x + 2, frame_y + 2, frame_width - 4, frame_height - 4);

        canvas_set_color(canvas, ColorBlack);
        canvas_draw_icon(
            canvas, frame_x + 2, canvas_height(canvas) - frame_y - 2 - 42, &I_WarningDolphin_45x42);

        canvas_draw_rframe(canvas, frame_x, frame_y, frame_width, frame_height, 3);
        canvas_draw_rframe(canvas, frame_x + 1, frame_y + 1, frame_width - 2, frame_height - 2, 2);
        elements_multiline_text_aligned(
            canvas,
            (model->is_vertical) ? 32 : 84,
            (model->is_vertical) ? 42 : 32,
            AlignCenter,
            AlignCenter,
            furi_string_get_cstr(
                SubmenuItemArray_get(model->items, model->position)->locked_message));
    }
}

static bool submenu_view_input_callback(InputEvent* event, void* context) {
    Submenu* submenu = context;
    furi_assert(submenu);
    bool consumed = false;

    bool locked_message_visible = false;
    with_view_model(
        submenu->view,
        SubmenuModel * model,
        { locked_message_visible = model->locked_message_visible; },
        false);

    if(locked_message_visible && (event->type == InputTypeShort || event->type == InputTypeLong)) {
        with_view_model(
            submenu->view, SubmenuModel * model, { model->locked_message_visible = false; }, true);
        consumed = true;
    } else if(event->key == InputKeyOk) {
        consumed = true;
        submenu_process_ok(submenu, event->type);
    } else if(event->type == InputTypeShort || event->type == InputTypeRepeat) {
        switch(event->key) {
        case InputKeyUp:
            consumed = true;
            submenu_process_up(submenu);
            break;
        case InputKeyDown:
            consumed = true;
            submenu_process_down(submenu);
            break;
        case InputKeyLeft:
            // a page at a time through a long list
            consumed = true;
            submenu_process_page(submenu, false);
            break;
        case InputKeyRight:
            consumed = true;
            submenu_process_page(submenu, true);
            break;
        default:
            break;
        }
    }

    return consumed;
}

void submenu_timer_callback(void* context) {
    furi_assert(context);
    Submenu* submenu = context;

    with_view_model(
        submenu->view, SubmenuModel * model, { model->locked_message_visible = false; }, true);
}

static void submenu_scroll_timer_callback(void* context) {
    Submenu* submenu = context;
    // redrawn only when there is something to scroll, and not once it has come to rest
    bool redraw = false;
    with_view_model(
        submenu->view,
        SubmenuModel * model,
        { redraw = elements_scroll_step(&model->scroll_counter) && model->selected_scrolls; },
        redraw);
}

static void submenu_view_enter(void* context) {
    Submenu* submenu = context;
    with_view_model(submenu->view, SubmenuModel * model, { model->scroll_counter = 0; }, false);
    furi_timer_start(submenu->scroll_timer, furi_ms_to_ticks(SUBMENU_SCROLL_INTERVAL_MS));
}

static void submenu_view_exit(void* context) {
    Submenu* submenu = context;
    furi_timer_stop(submenu->scroll_timer);
}

Submenu* submenu_alloc(void) {
    Submenu* submenu = malloc(sizeof(Submenu));
    submenu->view = view_alloc();
    view_set_context(submenu->view, submenu);
    view_allocate_model(submenu->view, ViewModelTypeLocking, sizeof(SubmenuModel));
    view_set_draw_callback(submenu->view, submenu_view_draw_callback);
    view_set_input_callback(submenu->view, submenu_view_input_callback);

    submenu->locked_timer = furi_timer_alloc(submenu_timer_callback, FuriTimerTypeOnce, submenu);
    submenu->scroll_timer =
        furi_timer_alloc(submenu_scroll_timer_callback, FuriTimerTypePeriodic, submenu);
    view_set_enter_callback(submenu->view, submenu_view_enter);
    view_set_exit_callback(submenu->view, submenu_view_exit);

    with_view_model(
        submenu->view,
        SubmenuModel * model,
        {
            SubmenuItemArray_init(model->items);
            model->position = 0;
            model->window_position = 0;
            model->scroll_counter = 0;
            model->header = furi_string_alloc();
            model->hints = furi_string_alloc();
        },
        true);

    return submenu;
}

void submenu_free(Submenu* submenu) {
    furi_check(submenu);

    with_view_model(
        submenu->view,
        SubmenuModel * model,
        {
            furi_string_free(model->header);
            furi_string_free(model->hints);
            SubmenuItemArray_clear(model->items);
        },
        true);
    furi_timer_stop(submenu->locked_timer);
    furi_timer_free(submenu->locked_timer);
    furi_timer_stop(submenu->scroll_timer);
    furi_timer_free(submenu->scroll_timer);
    view_free(submenu->view);
    free(submenu);
}

View* submenu_get_view(Submenu* submenu) {
    furi_check(submenu);
    return submenu->view;
}

void submenu_add_item(
    Submenu* submenu,
    const char* label,
    uint32_t index,
    SubmenuItemCallback callback,
    void* callback_context) {
    submenu_add_lockable_item(submenu, label, index, callback, callback_context, false, NULL);
}

void submenu_add_lockable_item(
    Submenu* submenu,
    const char* label,
    uint32_t index,
    SubmenuItemCallback callback,
    void* callback_context,
    bool locked,
    const char* locked_message) {
    SubmenuItem* item = NULL;
    furi_check(label);
    furi_check(submenu);
    if(locked) {
        furi_check(locked_message);
    }

    with_view_model(
        submenu->view,
        SubmenuModel * model,
        {
            item = SubmenuItemArray_push_new(model->items);
            furi_string_set_str(item->label, label);
            item->index = index;
            item->callback = callback;
            item->callback_context = callback_context;
            item->has_extended_events = false;
            item->locked = locked;
            if(locked) {
                furi_string_set_str(item->locked_message, locked_message);
            }
        },
        true);
}

void submenu_add_item_ex(
    Submenu* submenu,
    const char* label,
    uint32_t index,
    SubmenuItemCallbackEx callback,
    void* callback_context) {
    SubmenuItem* item = NULL;
    furi_check(label);
    furi_check(submenu);

    with_view_model(
        submenu->view,
        SubmenuModel * model,
        {
            item = SubmenuItemArray_push_new(model->items);
            furi_string_set_str(item->label, label);
            item->index = index;
            item->callback_ex = callback;
            item->callback_context = callback_context;
            item->has_extended_events = true;
        },
        true);
}

void submenu_change_item_label(Submenu* submenu, uint32_t index, const char* label) {
    furi_check(submenu);
    furi_check(label);

    with_view_model(
        submenu->view,
        SubmenuModel * model,
        {
            SubmenuItemArray_it_t it;
            for(SubmenuItemArray_it(it, model->items); !SubmenuItemArray_end_p(it);
                SubmenuItemArray_next(it)) {
                if(index == SubmenuItemArray_cref(it)->index) {
                    furi_string_set_str(SubmenuItemArray_cref(it)->label, label);
                    break;
                }
            }
        },
        true);
}

void submenu_remove_item(Submenu* submenu, uint32_t index) {
    furi_check(submenu);

    with_view_model(
        submenu->view,
        SubmenuModel * model,
        {
            SubmenuItemArray_it_t it;
            for(SubmenuItemArray_it(it, model->items); !SubmenuItemArray_end_p(it);
                SubmenuItemArray_next(it)) {
                if(index == SubmenuItemArray_cref(it)->index) {
                    SubmenuItemArray_remove(model->items, it);
                    break;
                }
            }
        },
        true);
}

void submenu_reset(Submenu* submenu) {
    furi_check(submenu);
    view_set_orientation(submenu->view, ViewOrientationHorizontal);

    with_view_model(
        submenu->view,
        SubmenuModel * model,
        {
            SubmenuItemArray_reset(model->items);
            model->position = 0;
            model->window_position = 0;
            model->is_vertical = false;
            furi_string_reset(model->header);
            furi_string_reset(model->hints);
        },
        true);
}

uint32_t submenu_get_selected_item(Submenu* submenu) {
    furi_check(submenu);

    uint32_t selected_item_index = 0;

    with_view_model(
        submenu->view,
        SubmenuModel * model,
        {
            if(model->position < SubmenuItemArray_size(model->items)) {
                const SubmenuItem* item = SubmenuItemArray_cget(model->items, model->position);
                selected_item_index = item->index;
            }
        },
        false);

    return selected_item_index;
}

void submenu_set_selected_item(Submenu* submenu, uint32_t index) {
    furi_check(submenu);
    with_view_model(
        submenu->view,
        SubmenuModel * model,
        {
            size_t position = 0;
            SubmenuItemArray_it_t it;
            for(SubmenuItemArray_it(it, model->items); !SubmenuItemArray_end_p(it);
                SubmenuItemArray_next(it)) {
                if(index == SubmenuItemArray_cref(it)->index) {
                    break;
                }
                position++;
            }

            const size_t items_size = SubmenuItemArray_size(model->items);

            if(position >= items_size) {
                position = 0;
            }

            model->position = position;
            model->window_position = position;
            model->scroll_counter = 0;

            if(model->window_position > 0) {
                model->window_position -= 1;
            }

            const size_t items_on_screen = submenu_items_on_screen(model);

            if(items_size <= items_on_screen) {
                model->window_position = 0;
            } else {
                const size_t pos = items_size - items_on_screen;
                if(model->window_position > pos) {
                    model->window_position = pos;
                }
            }
        },
        true);
}

void submenu_process_up(Submenu* submenu) {
    with_view_model(
        submenu->view,
        SubmenuModel * model,
        {
            const size_t items_on_screen = submenu_items_on_screen(model);
            const size_t items_size = SubmenuItemArray_size(model->items);

            model->scroll_counter = 0;
            if(model->position > 0) {
                model->position--;
                if((model->position == model->window_position) && (model->window_position > 0)) {
                    model->window_position--;
                }
            } else {
                model->position = items_size - 1;
                if(model->position > items_on_screen - 1) {
                    model->window_position = model->position - (items_on_screen - 1);
                }
            }
        },
        true);
}

void submenu_process_down(Submenu* submenu) {
    with_view_model(
        submenu->view,
        SubmenuModel * model,
        {
            const size_t items_on_screen = submenu_items_on_screen(model);
            const size_t items_size = SubmenuItemArray_size(model->items);

            model->scroll_counter = 0;
            if(model->position < items_size - 1) {
                model->position++;
                if((model->position - model->window_position > items_on_screen - 2) &&
                   (model->window_position < items_size - items_on_screen)) {
                    model->window_position++;
                }
            } else {
                model->position = 0;
                model->window_position = 0;
            }
        },
        true);
}

void submenu_process_page(Submenu* submenu, bool down) {
    with_view_model(
        submenu->view,
        SubmenuModel * model,
        {
            const size_t items_on_screen = submenu_items_on_screen(model);
            const size_t items_size = SubmenuItemArray_size(model->items);
            model->scroll_counter = 0;
            if(items_size > 0) {
                // a screenful on, stopping at the ends rather than wrapping
                if(down) {
                    model->position = MIN(model->position + items_on_screen, items_size - 1);
                } else {
                    model->position =
                        model->position > items_on_screen ? model->position - items_on_screen : 0;
                }
                if(model->position < model->window_position) {
                    model->window_position = model->position;
                } else if(model->position >= model->window_position + items_on_screen) {
                    model->window_position = model->position - (items_on_screen - 1);
                }
                if(items_size > items_on_screen &&
                   model->window_position > items_size - items_on_screen) {
                    model->window_position = items_size - items_on_screen;
                }
            }
        },
        true);
}

void submenu_process_ok(Submenu* submenu, InputType input_type) {
    SubmenuItem* item = NULL;

    with_view_model(
        submenu->view,
        SubmenuModel * model,
        {
            const size_t items_size = SubmenuItemArray_size(model->items);
            if(model->position < items_size) {
                item = SubmenuItemArray_get(model->items, model->position);
            }
            if(item && item->locked &&
               (input_type == InputTypeShort || input_type == InputTypeLong)) {
                model->locked_message_visible = true;
                furi_timer_start(submenu->locked_timer, furi_kernel_get_tick_frequency() * 3);
            }
        },
        true);

    if(!item) return;
    if(item->locked) {
        return;
    }

    if(!item->has_extended_events && input_type == InputTypeShort && item->callback) {
        item->callback(item->callback_context, item->index);
    } else if(item->has_extended_events && item->callback_ex) {
        item->callback_ex(item->callback_context, input_type, item->index);
    }
}

void submenu_set_header(Submenu* submenu, const char* header) {
    furi_check(submenu);

    with_view_model(
        submenu->view,
        SubmenuModel * model,
        {
            if(header == NULL) {
                furi_string_reset(model->header);
            } else {
                furi_string_set_str(model->header, header);
            }
        },
        true);
}

void submenu_set_hints(Submenu* submenu, const char* hints) {
    furi_check(submenu);
    with_view_model(
        submenu->view,
        SubmenuModel * model,
        {
            if(hints == NULL) {
                furi_string_reset(model->hints);
            } else {
                furi_string_set_str(model->hints, hints);
            }
            // fewer rows fit: keep the selection on screen
            const size_t items_on_screen = submenu_items_on_screen(model);
            if(model->position >= model->window_position + items_on_screen) {
                model->window_position = model->position - items_on_screen + 1;
            }
        },
        true);
}

void submenu_set_orientation(Submenu* submenu, ViewOrientation orientation) {
    furi_check(submenu);
    const bool is_vertical = orientation == ViewOrientationVertical ||
                             orientation == ViewOrientationVerticalFlip;

    view_set_orientation(submenu->view, orientation);

    with_view_model(
        submenu->view,
        SubmenuModel * model,
        {
            model->is_vertical = is_vertical;

            // Recalculating the position
            // Need if _set_orientation is called after _set_selected_item
            size_t position = model->position;
            const size_t items_size = SubmenuItemArray_size(model->items);
            const size_t items_on_screen = submenu_items_on_screen(model);

            if(position >= items_size) {
                position = 0;
            }

            model->position = position;
            model->window_position = position;

            if(model->window_position > 0) {
                model->window_position -= 1;
            }

            if(items_size <= items_on_screen) {
                model->window_position = 0;
            } else {
                const size_t pos = items_size - items_on_screen;
                if(model->window_position > pos) {
                    model->window_position = pos;
                }
            }
        },
        true);
}
