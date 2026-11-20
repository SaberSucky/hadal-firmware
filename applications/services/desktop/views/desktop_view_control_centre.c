#include "desktop_view_control_centre.h"

#include <furi.h>
#include <gui/elements.h>
#include <assets_icons.h>

#define TILE_COLUMNS   5
#define TILE_ROWS      2
#define SLIDER_COLUMNS 2
#define COLUMNS        (TILE_COLUMNS + SLIDER_COLUMNS)

// A tile is drawn 18x18 inside a 20x20 cell; the focus ring is the cell's edge
#define CELL      20
#define CELL_STEP 20
#define TILES_Y   11

// Sliders as tall as the two rows of tiles: a rounded well that fills from the bottom, its icon
// inside it, dark on the empty part and light on the filled part
#define SLIDER_X0 103
#define SLIDER_X1 116
#define SLIDER_W  11
#define SLIDER_H  (2 * CELL)

#define SLIDER_STEP 0.05f

struct DesktopControlCentreView {
    View* view;
    DesktopControlCentreCallback callback;
    void* context;
};

typedef struct {
    ControlCentreState state;
    uint8_t column; // 0..4 tiles, 5 brightness, 6 volume
    uint8_t row;
    ControlCentreTile chosen;
} ControlCentreModel;

static const Icon* const tile_icons[ControlCentreTileCount] = {
    [ControlCentreTileLock] = &I_cc_lock_12x12,
    [ControlCentreTilePinLock] = &I_cc_pin_12x12,
    [ControlCentreTileBluetooth] = &I_cc_bluetooth_12x12,
    [ControlCentreTileStealth] = &I_cc_mute_12x12,
    [ControlCentreTileDark] = &I_cc_dark_12x12,
    [ControlCentreTileLeftHanded] = &I_cc_flip_12x12,
    [ControlCentreTileHadal] = &I_cc_hadal_12x12,
    [ControlCentreTileApp1] = NULL,
    [ControlCentreTileApp2] = NULL,
    [ControlCentreTileDummy] = &I_cc_dummy_12x12,
};

static const char* const switch_names[ControlCentreTileCount] = {
    [ControlCentreTileBluetooth] = "Bluetooth",
    [ControlCentreTileStealth] = "Silent",
    [ControlCentreTileDark] = "Dark mode",
    [ControlCentreTileLeftHanded] = "Left-handed",
    [ControlCentreTileDummy] = "Dummy mode",
};

static ControlCentreTile control_centre_tile_at(uint8_t column, uint8_t row) {
    return (ControlCentreTile)(row * TILE_COLUMNS + column);
}

static uint8_t control_centre_percent(float value) {
    return (uint8_t)(value * 100.0f + 0.5f);
}

/** The line along the bottom: what the focused tile or slider is and does */
static void control_centre_label(const ControlCentreModel* model, char* out, size_t size) {
    const ControlCentreState* state = &model->state;
    if(model->column == TILE_COLUMNS) {
        snprintf(out, size, "Brightness %u%%", control_centre_percent(state->brightness));
        return;
    }
    if(model->column == TILE_COLUMNS + 1) {
        snprintf(
            out,
            size,
            "Volume %u%%, vibration %s",
            control_centre_percent(state->volume),
            state->vibro ? "on" : "off");
        return;
    }

    const ControlCentreTile tile = control_centre_tile_at(model->column, model->row);
    switch(tile) {
    case ControlCentreTileLock:
        snprintf(out, size, state->pin_set ? "Lock, no PIN to unlock" : "Lock");
        break;
    case ControlCentreTilePinLock:
        snprintf(out, size, state->pin_set ? "Lock with PIN" : "Set a PIN: Settings > Desktop");
        break;
    case ControlCentreTileHadal:
        snprintf(out, size, "Hadal Settings");
        break;
    case ControlCentreTileApp1:
    case ControlCentreTileApp2: {
        const char* label = state->app_label[tile - ControlCentreTileApp1];
        if(label[0]) {
            snprintf(out, size, "%s", label);
        } else {
            snprintf(out, size, "Pick an app: Hadal Settings");
        }
    } break;
    default:
        snprintf(out, size, "%s: %s", switch_names[tile], state->on[tile] ? "on" : "off");
        break;
    }
}

static void control_centre_draw_header(Canvas* canvas, const ControlCentreState* state) {
    canvas_set_font(canvas, FontSecondary);
    char text[12];

    uint8_t hour = state->hour;
    if(state->format_12) {
        hour = hour % 12;
        if(hour == 0) hour = 12;
    }
    snprintf(text, sizeof(text), "%u:%02u", hour, state->minute);
    canvas_draw_str(canvas, 1, 8, text);

    // battery: outline with a nub, filled to the charge
    const uint8_t bx = 114, by = 1;
    canvas_draw_rframe(canvas, bx, by, 12, 7, 1);
    canvas_draw_box(canvas, bx + 12, by + 2, 1, 3);
    const uint8_t fill = (uint8_t)((state->battery * 8U + 50U) / 100U);
    if(fill) canvas_draw_box(canvas, bx + 2, by + 2, fill, 3);

    snprintf(text, sizeof(text), "%u%%", state->battery);
    const uint16_t width = canvas_string_width(canvas, text);
    canvas_draw_str(canvas, bx - 2 - width, 8, text);
    if(state->charging) canvas_draw_icon(canvas, bx - 9 - width, 1, &I_cc_bolt_5x7);
}

static void control_centre_draw_tile(
    Canvas* canvas,
    const ControlCentreModel* model,
    uint8_t column,
    uint8_t row) {
    const ControlCentreTile tile = control_centre_tile_at(column, row);
    const ControlCentreState* state = &model->state;
    const uint8_t x = column * CELL_STEP;
    const uint8_t y = TILES_Y + row * CELL_STEP;
    const bool focused = model->column == column && model->row == row;
    const bool on = state->on[tile];

    if(focused) canvas_draw_rframe(canvas, x, y, CELL, CELL, 4);
    if(on) {
        canvas_draw_rbox(canvas, x + 1, y + 1, CELL - 2, CELL - 2, 3);
        canvas_set_color(canvas, ColorWhite);
    } else {
        canvas_draw_rframe(canvas, x + 1, y + 1, CELL - 2, CELL - 2, 3);
    }

    const Icon* icon = tile_icons[tile];
    if(icon) {
        canvas_draw_icon(canvas, x + 4, y + 4, icon);
    } else {
        // an app shortcut: its initial, or + when none is chosen
        const char* label = state->app_label[tile - ControlCentreTileApp1];
        if(label[0]) {
            char initial[2] = {label[0], '\0'};
            canvas_set_font(canvas, FontPrimary);
            canvas_draw_str_aligned(
                canvas, x + CELL / 2, y + CELL / 2 + 1, AlignCenter, AlignCenter, initial);
        } else {
            canvas_draw_icon(canvas, x + 4, y + 4, &I_cc_add_12x12);
        }
    }
    canvas_set_color(canvas, ColorBlack);
}

static void control_centre_draw_slider(
    Canvas* canvas,
    uint8_t x,
    float value,
    bool focused,
    const Icon* icon) {
    const uint8_t y = TILES_Y;
    if(focused) canvas_draw_rframe(canvas, x - 1, y - 1, SLIDER_W + 2, SLIDER_H + 2, 5);
    canvas_draw_rframe(canvas, x, y, SLIDER_W, SLIDER_H, 4);

    // the fill: the whole inside width, rounded like the well, from the bottom up
    const uint8_t inside = SLIDER_H - 2;
    const uint8_t fill = (uint8_t)(value * inside + 0.5f);
    if(fill >= 7) {
        canvas_draw_rbox(canvas, x + 1, y + 1 + inside - fill, SLIDER_W - 2, fill, 3);
    } else if(fill) {
        canvas_draw_box(canvas, x + 2, y + 1 + inside - fill, SLIDER_W - 4, fill);
    }

    // the icon sits in the well's foot and shows through whatever covers it
    canvas_set_bitmap_mode(canvas, true); // only the icon's own pixels
    canvas_set_color(canvas, ColorXOR);
    canvas_draw_icon(canvas, x + 2, y + SLIDER_H - 10, icon);
    canvas_set_color(canvas, ColorBlack);
    canvas_set_bitmap_mode(canvas, false);
}

static void control_centre_draw_callback(Canvas* canvas, void* model_) {
    const ControlCentreModel* model = model_;
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);

    control_centre_draw_header(canvas, &model->state);

    for(uint8_t row = 0; row < TILE_ROWS; row++) {
        for(uint8_t column = 0; column < TILE_COLUMNS; column++) {
            control_centre_draw_tile(canvas, model, column, row);
        }
    }
    control_centre_draw_slider(
        canvas,
        SLIDER_X0,
        model->state.brightness,
        model->column == TILE_COLUMNS,
        &I_cc_sun_7x7);
    control_centre_draw_slider(
        canvas,
        SLIDER_X1,
        model->state.volume,
        model->column == TILE_COLUMNS + 1,
        &I_cc_speaker_7x7);

    char label[40];
    control_centre_label(model, label, sizeof(label));
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 64, 63, AlignCenter, AlignBottom, label);
}

static float control_centre_step(float value, bool up) {
    value += up ? SLIDER_STEP : -SLIDER_STEP;
    if(value < 0.0f) value = 0.0f;
    if(value > 1.0f) value = 1.0f;
    // whole steps, so repeated presses land on round numbers
    return (float)(int)(value / SLIDER_STEP + 0.5f) * SLIDER_STEP;
}

static bool control_centre_input_callback(InputEvent* event, void* context) {
    DesktopControlCentreView* control_centre = context;
    if(event->key == InputKeyBack) return false;
    if(event->type != InputTypeShort && event->type != InputTypeRepeat) return true;

    DesktopEvent send = DesktopControlCentreEventNone;
    with_view_model(
        control_centre->view,
        ControlCentreModel * model,
        {
            const bool on_slider = model->column >= TILE_COLUMNS;
            switch(event->key) {
            case InputKeyLeft:
                model->column = (model->column + COLUMNS - 1) % COLUMNS;
                break;
            case InputKeyRight:
                model->column = (model->column + 1) % COLUMNS;
                break;
            case InputKeyUp:
            case InputKeyDown: {
                const bool up = event->key == InputKeyUp;
                if(!on_slider) {
                    if(event->type == InputTypeShort) model->row = up ? 0 : 1;
                } else if(model->column == TILE_COLUMNS) {
                    model->state.brightness = control_centre_step(model->state.brightness, up);
                    send = DesktopControlCentreEventBrightness;
                } else {
                    model->state.volume = control_centre_step(model->state.volume, up);
                    send = DesktopControlCentreEventVolume;
                }
            } break;
            case InputKeyOk:
                if(event->type != InputTypeShort) break;
                if(!on_slider) {
                    model->chosen = control_centre_tile_at(model->column, model->row);
                    send = DesktopControlCentreEventTile;
                } else if(model->column == TILE_COLUMNS + 1) {
                    model->state.vibro = !model->state.vibro;
                    send = DesktopControlCentreEventVibro;
                }
                break;
            default:
                break;
            }
        },
        true);

    if(send != DesktopControlCentreEventNone && control_centre->callback) {
        control_centre->callback(send, control_centre->context);
    }
    return true;
}

DesktopControlCentreView* desktop_control_centre_alloc(void) {
    DesktopControlCentreView* control_centre = malloc(sizeof(DesktopControlCentreView));
    control_centre->callback = NULL;
    control_centre->context = NULL;
    control_centre->view = view_alloc();
    view_allocate_model(control_centre->view, ViewModelTypeLocking, sizeof(ControlCentreModel));
    view_set_context(control_centre->view, control_centre);
    view_set_draw_callback(control_centre->view, control_centre_draw_callback);
    view_set_input_callback(control_centre->view, control_centre_input_callback);
    return control_centre;
}

void desktop_control_centre_free(DesktopControlCentreView* control_centre) {
    furi_check(control_centre);
    view_free(control_centre->view);
    free(control_centre);
}

View* desktop_control_centre_get_view(DesktopControlCentreView* control_centre) {
    furi_check(control_centre);
    return control_centre->view;
}

void desktop_control_centre_set_callback(
    DesktopControlCentreView* control_centre,
    DesktopControlCentreCallback callback,
    void* context) {
    furi_check(control_centre);
    control_centre->callback = callback;
    control_centre->context = context;
}

void desktop_control_centre_set_state(
    DesktopControlCentreView* control_centre,
    const ControlCentreState* state) {
    furi_check(control_centre);
    with_view_model(
        control_centre->view, ControlCentreModel * model, { model->state = *state; }, true);
}

void desktop_control_centre_get_state(
    DesktopControlCentreView* control_centre,
    ControlCentreState* state) {
    furi_check(control_centre);
    with_view_model(
        control_centre->view, ControlCentreModel * model, { *state = model->state; }, false);
}

ControlCentreTile desktop_control_centre_get_tile(DesktopControlCentreView* control_centre) {
    furi_check(control_centre);
    ControlCentreTile tile;
    with_view_model(
        control_centre->view, ControlCentreModel * model, { tile = model->chosen; }, false);
    return tile;
}

void desktop_control_centre_reset_focus(DesktopControlCentreView* control_centre) {
    furi_check(control_centre);
    with_view_model(
        control_centre->view,
        ControlCentreModel * model,
        {
            model->column = 0;
            model->row = 0;
        },
        true);
}
