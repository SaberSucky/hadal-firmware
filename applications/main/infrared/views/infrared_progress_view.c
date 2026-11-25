#include "infrared_progress_view.h"

#include <assets_icons.h>
#include <gui/canvas.h>
#include <gui/view.h>
#include <gui/elements.h>
#include <gui/modules/button_panel.h>
#include <input/input.h>

#include <furi.h>
#include <furi_hal_resources.h>
#include <core/check.h>
#include <stdint.h>

struct InfraredProgressView {
    View* view;
    InfraredProgressViewInputCallback input_callback;
    void* context;
};

typedef struct {
    size_t progress;
    size_t progress_total;
    bool is_paused;
} InfraredProgressViewModel;

/* The remote's screen is upright: 64 wide, 128 tall. */
#define PANEL_X              0
#define PANEL_WIDTH          63
/* Sending: title, bar, percentage, two hints */
#define PANEL_Y_SENDING      25
#define PANEL_HEIGHT_SENDING 81
/* Paused: title, bar, count, then six hints, so the panel starts higher and runs to the bottom */
#define PANEL_Y_PAUSED       14
#define PANEL_HEIGHT_PAUSED  114
#define HINT_PITCH           10

static void draw_hint(Canvas* canvas, uint8_t x, uint8_t top, const char* text) {
    canvas_draw_str(canvas, x + 14, top + 8, text);
}

static void infrared_progress_view_draw_callback(Canvas* canvas, void* _model) {
    InfraredProgressViewModel* model = (InfraredProgressViewModel*)_model;

    const uint8_t x = PANEL_X;
    const uint8_t y = model->is_paused ? PANEL_Y_PAUSED : PANEL_Y_SENDING;
    const uint8_t width = PANEL_WIDTH;
    const uint8_t height = model->is_paused ? PANEL_HEIGHT_PAUSED : PANEL_HEIGHT_SENDING;

    // Clear what is under the panel, so the remote's buttons cannot show through its edges
    canvas_set_color(canvas, ColorWhite);
    canvas_draw_box(canvas, x, y, width, height);
    canvas_set_color(canvas, ColorBlack);
    elements_bold_rounded_frame(canvas, x, y, width, height);

    canvas_set_font(canvas, FontSecondary);
    elements_multiline_text_aligned(
        canvas,
        x + 32,
        y + 9,
        AlignCenter,
        AlignCenter,
        model->is_paused ? "Paused" : "Sending...");

    float progress_value = (float)model->progress / model->progress_total;
    elements_progress_bar(canvas, x + 4, y + 19, width - 7, progress_value);

    char progress_string[16] = {0};
    if(model->is_paused) {
        snprintf(
            progress_string,
            sizeof(progress_string),
            "%zu/%zu",
            model->progress,
            model->progress_total);
    } else {
        uint8_t percent_value = 100 * model->progress / model->progress_total;
        snprintf(progress_string, sizeof(progress_string), "%d%%", percent_value);
    }
    elements_multiline_text_aligned(
        canvas, x + 33, y + (model->is_paused ? 36 : 37), AlignCenter, AlignCenter, progress_string);

    if(model->is_paused) {
        // Six hints at an even pitch, the last one well inside the bottom border
        const uint8_t bx = x + 8;
        const uint8_t top = y + 44;

        // Back resumes; held, it stops (a held key is the one that ends)
        canvas_draw_icon(canvas, bx + 0, top + 0 * HINT_PITCH, &I_Pin_back_arrow_10x8);
        draw_hint(canvas, bx, top + 0 * HINT_PITCH, "resume");

        canvas_draw_icon(canvas, bx + 0, top + 1 * HINT_PITCH, &I_Pin_back_arrow_10x8);
        draw_hint(canvas, bx, top + 1 * HINT_PITCH, "hold: stop");

        canvas_draw_icon(canvas, bx + 1, top + 2 * HINT_PITCH - 1, &I_Ok_btn_9x9);
        draw_hint(canvas, bx, top + 2 * HINT_PITCH, "send 1");

        canvas_draw_icon(canvas, bx + 2, top + 3 * HINT_PITCH + 2, &I_ButtonLeftSmall_3x5);
        canvas_draw_icon(canvas, bx + 7, top + 3 * HINT_PITCH + 2, &I_ButtonRightSmall_3x5);
        draw_hint(canvas, bx, top + 3 * HINT_PITCH, "select");

        canvas_draw_icon(canvas, bx + 4, top + 4 * HINT_PITCH + 2, &I_SmallArrowUp_3x5);
        draw_hint(canvas, bx, top + 4 * HINT_PITCH, "save all");

        canvas_draw_icon(canvas, bx + 4, top + 5 * HINT_PITCH + 2, &I_SmallArrowDown_3x5);
        draw_hint(canvas, bx, top + 5 * HINT_PITCH, "save 1");
    } else {
        const uint8_t bx = x + 14;
        const uint8_t by = y + 50;

        canvas_draw_icon(canvas, bx + 0, by + 0, &I_Pin_back_arrow_10x8);
        canvas_draw_str(canvas, bx + 14, by + 8, "stop");

        canvas_draw_icon(canvas, bx + 1, by + 10, &I_Ok_btn_9x9);
        canvas_draw_str(canvas, bx + 14, by + 17, "pause");
    }
}

bool infrared_progress_view_set_progress(InfraredProgressView* instance, uint16_t progress) {
    bool result;
    with_view_model(
        instance->view,
        InfraredProgressViewModel * model,
        {
            result = progress <= model->progress_total;
            if(result) model->progress = progress;
        },
        true);
    return result;
}

void infrared_progress_view_set_progress_total(
    InfraredProgressView* progress,
    uint16_t progress_total) {
    furi_assert(progress);
    InfraredProgressViewModel* model = view_get_model(progress->view);
    model->progress = 0;
    model->progress_total = progress_total;
    view_commit_model(progress->view, false);
}

void infrared_progress_view_set_paused(InfraredProgressView* instance, bool is_paused) {
    with_view_model(
        instance->view, InfraredProgressViewModel * model, { model->is_paused = is_paused; }, true);
}

bool infrared_progress_view_input_callback(InputEvent* event, void* context) {
    InfraredProgressView* instance = context;

    if(event->type == InputTypePress || event->type == InputTypeRelease) {
        return false;
    }

    if(!instance->input_callback) return false;

    with_view_model(
        instance->view,
        InfraredProgressViewModel * model,
        {
            if(model->is_paused) {
                if(event->key == InputKeyLeft)
                    instance->input_callback(
                        instance->context, InfraredProgressViewInputPreviousSignal);
                else if(event->key == InputKeyRight)
                    instance->input_callback(
                        instance->context, InfraredProgressViewInputNextSignal);
                else if(event->key == InputKeyOk)
                    instance->input_callback(
                        instance->context, InfraredProgressViewInputSendSingle);
                else if(event->key == InputKeyDown)
                    instance->input_callback(instance->context, InfraredProgressViewInputSave);
                else if(event->key == InputKeyUp)
                    instance->input_callback(
                        instance->context, InfraredProgressViewInputSaveDevice);
                else if(event->key == InputKeyBack && event->type == InputTypeShort)
                    instance->input_callback(instance->context, InfraredProgressViewInputResume);
                else if(event->key == InputKeyBack && event->type == InputTypeLong)
                    instance->input_callback(instance->context, InfraredProgressViewInputStop);
            } else {
                if(event->key == InputKeyOk)
                    instance->input_callback(instance->context, InfraredProgressViewInputPause);
                else if(event->key == InputKeyBack)
                    instance->input_callback(instance->context, InfraredProgressViewInputStop);
            }
        },
        false);

    return true;
}

InfraredProgressView* infrared_progress_view_alloc(void) {
    InfraredProgressView* instance = malloc(sizeof(InfraredProgressView));
    instance->view = view_alloc();
    view_allocate_model(instance->view, ViewModelTypeLocking, sizeof(InfraredProgressViewModel));
    InfraredProgressViewModel* model = view_get_model(instance->view);
    model->progress = 0;
    model->progress_total = 0;
    view_commit_model(instance->view, false);
    view_set_draw_callback(instance->view, infrared_progress_view_draw_callback);
    view_set_input_callback(instance->view, infrared_progress_view_input_callback);
    view_set_context(instance->view, instance);

    return instance;
}

void infrared_progress_view_free(InfraredProgressView* progress) {
    view_free(progress->view);
    free(progress);
}

void infrared_progress_view_set_input_callback(
    InfraredProgressView* instance,
    InfraredProgressViewInputCallback callback,
    void* context) {
    furi_assert(instance);
    instance->input_callback = callback;
    instance->context = context;
}

View* infrared_progress_view_get_view(InfraredProgressView* instance) {
    furi_assert(instance);
    furi_assert(instance->view);
    return instance->view;
}
