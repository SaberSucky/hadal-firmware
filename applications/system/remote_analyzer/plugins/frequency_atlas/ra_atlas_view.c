#include "ra_atlas_view.h"
#include "ra_atlas_plan.h"

#include <furi.h>
#include <gui/elements.h>
#include <input/input.h>

#define RA_ATLAS_ROWS       4
#define RA_ATLAS_ROW_HEIGHT 10
#define RA_ATLAS_ROWS_TOP   11
#define RA_ATLAS_FOOTER_Y   62

struct RaAtlasView {
    View* view;
    RaAtlasViewCallback callback;
    void* context;
};

typedef struct {
    const char* region;
    RaAtlasRow rows[RA_ATLAS_SCAN_BANDS_MAX];
    size_t count;
    size_t selected;
    size_t top;
    bool held;
    bool signal;
} RaAtlasViewModel;

/** "now", "12s", "5m", "3h", "-" or "n/a": at most four characters. */
static void ra_atlas_view_status(const RaAtlasRow* row, char* out, size_t size) {
    switch(row->status) {
    case RaAtlasStatusOutOfRange:
        snprintf(out, size, "n/a");
        break;
    case RaAtlasStatusActive:
        snprintf(out, size, "now");
        break;
    case RaAtlasStatusSeen:
        if(row->ago < 60) {
            snprintf(out, size, "%lus", (unsigned long)row->ago);
        } else if(row->ago < 3600) {
            snprintf(out, size, "%lum", (unsigned long)(row->ago / 60));
        } else {
            snprintf(out, size, "%luh", (unsigned long)(row->ago / 3600));
        }
        break;
    default:
        snprintf(out, size, "-");
        break;
    }
}

static void ra_atlas_view_draw(Canvas* canvas, void* _model) {
    RaAtlasViewModel* model = _model;
    char buffer[48];

    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);
    canvas_set_font(canvas, FontPrimary);
    snprintf(buffer, sizeof(buffer), "%s Atlas", model->region ? model->region : "");
    canvas_draw_str(canvas, 1, 9, buffer);

    canvas_set_font(canvas, FontSecondary);
    // Held on one band is the mode worth noticing; a carrier there fills the tag in
    const char* mode = model->held ? "HOLD" : "SCAN";
    const uint16_t mode_w = canvas_string_width(canvas, mode);
    if(model->signal) {
        canvas_draw_box(canvas, 126 - mode_w - 3, 0, mode_w + 4, 10);
        canvas_set_color(canvas, ColorWhite);
    } else {
        canvas_draw_frame(canvas, 126 - mode_w - 3, 0, mode_w + 4, 10);
    }
    canvas_draw_str(canvas, 126 - mode_w - 1, 8, mode);
    canvas_set_color(canvas, ColorBlack);

    for(size_t slot = 0; slot < RA_ATLAS_ROWS; slot++) {
        const size_t i = model->top + slot;
        if(i >= model->count) break;
        const RaAtlasRow* row = &model->rows[i];
        const uint8_t y = RA_ATLAS_ROWS_TOP + slot * RA_ATLAS_ROW_HEIGHT;

        if(i == model->selected) {
            canvas_draw_box(canvas, 0, y, 123, RA_ATLAS_ROW_HEIGHT);
            canvas_set_color(canvas, ColorWhite);
        }
        // Where the receiver is now
        if(row->listening) canvas_draw_box(canvas, 2, y + 3, 3, 4);

        ra_atlas_view_status(row, buffer, sizeof(buffer));
        const uint16_t status_w = canvas_string_width(canvas, buffer);
        canvas_draw_str(canvas, 121 - status_w, y + 8, buffer);

        FuriString* label = furi_string_alloc_set_str(row->label ? row->label : "");
        // A band something decoded on is marked, so the list says where to look
        if(row->protocol[0]) furi_string_cat_str(label, "*");
        elements_string_fit_width(canvas, label, 121 - status_w - 4 - 8);
        canvas_draw_str(canvas, 8, y + 8, furi_string_get_cstr(label));
        furi_string_free(label);

        canvas_set_color(canvas, ColorBlack);
    }

    if(model->count > RA_ATLAS_ROWS) {
        elements_scrollbar_pos(
            canvas,
            127,
            RA_ATLAS_ROWS_TOP,
            RA_ATLAS_ROWS * RA_ATLAS_ROW_HEIGHT,
            model->selected,
            model->count);
    }

    canvas_draw_line(canvas, 0, 52, 127, 52);
    if(model->selected < model->count) {
        const RaAtlasRow* row = &model->rows[model->selected];
        char start[12];
        char end[12];
        ra_atlas_format_mhz(row->start, start, sizeof(start));
        ra_atlas_format_mhz(row->end, end, sizeof(end));
        FuriString* footer = furi_string_alloc_printf("%s-%s", start, end);
        if(row->protocol[0]) {
            furi_string_cat_printf(footer, " %s", row->protocol);
        } else {
            furi_string_cat_str(footer, " MHz");
        }
        elements_string_fit_width(canvas, footer, 126);
        canvas_draw_str(canvas, 1, RA_ATLAS_FOOTER_Y, furi_string_get_cstr(footer));
        furi_string_free(footer);
    } else {
        canvas_draw_str(canvas, 1, RA_ATLAS_FOOTER_Y, "No bands");
    }
}

static bool ra_atlas_view_input(InputEvent* event, void* context) {
    RaAtlasView* instance = context;
    if(event->key == InputKeyBack) return false;

    const bool press = (event->type == InputTypePress) || (event->type == InputTypeRepeat);
    bool consumed = true;
    size_t selected = 0;
    bool have = false;

    with_view_model(
        instance->view,
        RaAtlasViewModel * model,
        {
            if(model->count) {
                if(press && (event->key == InputKeyUp)) {
                    model->selected = model->selected ? model->selected - 1 : model->count - 1;
                } else if(press && (event->key == InputKeyDown)) {
                    model->selected = (model->selected + 1) % model->count;
                }
                if(model->selected < model->top) model->top = model->selected;
                if(model->selected >= model->top + RA_ATLAS_ROWS) {
                    model->top = model->selected + 1 - RA_ATLAS_ROWS;
                }
                selected = model->selected;
                have = true;
            }
        },
        true);

    if(!instance->callback) return consumed;

    if((event->type == InputTypeShort) && (event->key == InputKeyOk) && have) {
        instance->callback(RaAtlasViewEventDetails, selected, instance->context);
    } else if((event->type == InputTypeLong) && (event->key == InputKeyOk)) {
        instance->callback(RaAtlasViewEventRegion, selected, instance->context);
    } else if((event->type == InputTypeShort) && (event->key == InputKeyRight) && have) {
        instance->callback(RaAtlasViewEventHold, selected, instance->context);
    } else if((event->type == InputTypeShort) && (event->key == InputKeyLeft)) {
        instance->callback(RaAtlasViewEventRelease, selected, instance->context);
    }
    return consumed;
}

RaAtlasView* ra_atlas_view_alloc(void) {
    RaAtlasView* instance = malloc(sizeof(RaAtlasView));
    instance->callback = NULL;
    instance->context = NULL;
    instance->view = view_alloc();
    view_allocate_model(instance->view, ViewModelTypeLocking, sizeof(RaAtlasViewModel));
    view_set_context(instance->view, instance);
    view_set_draw_callback(instance->view, ra_atlas_view_draw);
    view_set_input_callback(instance->view, ra_atlas_view_input);
    with_view_model(
        instance->view, RaAtlasViewModel * model, { memset(model, 0, sizeof(*model)); }, false);
    return instance;
}

void ra_atlas_view_free(RaAtlasView* instance) {
    furi_assert(instance);
    view_free(instance->view);
    free(instance);
}

View* ra_atlas_view_get_view(RaAtlasView* instance) {
    furi_assert(instance);
    return instance->view;
}

void ra_atlas_view_set_callback(RaAtlasView* instance, RaAtlasViewCallback callback, void* context) {
    furi_assert(instance);
    instance->callback = callback;
    instance->context = context;
}

void ra_atlas_view_reset(RaAtlasView* instance, const char* region, size_t count) {
    furi_assert(instance);
    if(count > RA_ATLAS_SCAN_BANDS_MAX) count = RA_ATLAS_SCAN_BANDS_MAX;
    with_view_model(
        instance->view,
        RaAtlasViewModel * model,
        {
            memset(model, 0, sizeof(*model));
            model->region = region;
            model->count = count;
        },
        true);
}

void ra_atlas_view_update(
    RaAtlasView* instance,
    const RaAtlasRow* rows,
    size_t count,
    bool held,
    bool signal) {
    furi_assert(instance);
    with_view_model(
        instance->view,
        RaAtlasViewModel * model,
        {
            if(count > model->count) count = model->count;
            memcpy(model->rows, rows, count * sizeof(RaAtlasRow));
            model->held = held;
            model->signal = signal;
        },
        true);
}
