#include "storage_settings_view.h"

#include <furi.h>
#include <furi_hal_flash.h>
#include <gui/elements.h>
#include <storage/storage_sd_api.h>

/* Two rows, each a name, what is free and a gauge of what is used; the hint bar under them */
#define ROW_SD_Y   24 /* the SD card's name baseline */
#define ROW_INT_Y  41
#define GAUGE_GAP  2 /* from a name's baseline to its gauge */
#define GAUGE_H    7
#define PILL_Y     52 /* a drive page's buttons, where the hint bar is */
#define PILL_H     12
#define ACTIONS_MAX 3

typedef enum {
    DriveSd,
    DriveInternal,
} Drive;

typedef struct {
    bool sd_ok;
    uint64_t sd_total_kb;
    uint64_t sd_free_kb;
    char sd_type[12];
    char sd_label[24];
    char sd_maker[28];
    bool int_ok;
    uint64_t int_total_kb;
    uint64_t int_free_kb;

    Drive drive; /**< the row chosen, or the page open */
    bool page; /**< a drive's page is open */
    uint8_t action; /**< its button chosen */
} StorageViewModel;

struct StorageSettingsView {
    View* view;
    StorageSettingsViewCallback callback;
    void* context;
};

/** "512 KB", "300 MB", "29.7 GB" */
static void storage_view_size(char* out, size_t size, uint64_t kb) {
    if(kb < 1024) {
        snprintf(out, size, "%lu KB", (unsigned long)kb);
    } else if(kb < 1024 * 1024) {
        snprintf(out, size, "%lu MB", (unsigned long)(kb / 1024));
    } else {
        const unsigned long tenths = (unsigned long)(kb * 10 / (1024 * 1024));
        snprintf(out, size, "%lu.%lu GB", tenths / 10, tenths % 10);
    }
}

/** A drive's buttons: what each does and what it says */
static uint8_t storage_view_actions(
    const StorageViewModel* model,
    StorageSettingsAction* actions,
    const char** labels) {
    uint8_t count = 0;
    if(model->drive == DriveSd) {
        actions[count] = StorageSettingsActionMount;
        labels[count++] = model->sd_ok ? "Unmount" : "Mount";
        actions[count] = StorageSettingsActionFormat;
        labels[count++] = "Format";
        if(model->sd_ok) {
            actions[count] = StorageSettingsActionBenchmark;
            labels[count++] = "Speed";
        }
    } else {
        actions[count] = StorageSettingsActionFactoryReset;
        labels[count++] = "Factory reset";
    }
    return count;
}

static void storage_view_draw_gauge(Canvas* canvas, int32_t y, bool ok, uint64_t total, uint64_t free) {
    canvas_draw_rframe(canvas, 2, y, 124, GAUGE_H, 3);
    if(!ok || !total) return; // nothing to measure: left empty
    const uint64_t used = total > free ? total - free : 0;
    const int32_t width = (int32_t)(used * 120 / total);
    if(width > 0) canvas_draw_rbox(canvas, 4, y + 2, MAX(width, 2), GAUGE_H - 4, 1);
}

static void storage_view_draw_row(
    Canvas* canvas,
    int32_t y,
    const char* name,
    bool chosen,
    bool ok,
    uint64_t total,
    uint64_t free,
    const char* missing) {
    canvas_set_font(canvas, FontPrimary);
    const int32_t name_width = canvas_string_width(canvas, name);
    if(chosen) {
        // the chosen row's name in a pill, as every list marks its row
        canvas_draw_rbox(canvas, 0, y - 10, name_width + 6, 12, 3);
        canvas_set_color(canvas, ColorWhite);
    }
    canvas_draw_str(canvas, 3, y, name);
    canvas_set_color(canvas, ColorBlack);

    char text[24];
    if(ok && total) {
        storage_view_size(text, sizeof(text), free);
        strlcat(text, " free", sizeof(text));
    } else {
        strlcpy(text, missing, sizeof(text));
    }
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 125, y, AlignRight, AlignBottom, text);
    storage_view_draw_gauge(canvas, y + GAUGE_GAP, ok, total, free);
}

static void storage_view_draw_overview(Canvas* canvas, const StorageViewModel* model) {
    elements_header(canvas, "Storage");
    storage_view_draw_row(
        canvas,
        ROW_SD_Y,
        "SD card",
        model->drive == DriveSd,
        model->sd_ok,
        model->sd_total_kb,
        model->sd_free_kb,
        "not mounted");
    storage_view_draw_row(
        canvas,
        ROW_INT_Y,
        "Internal",
        model->drive == DriveInternal,
        model->int_ok,
        model->int_total_kb,
        model->int_free_kb,
        "unreadable");
    elements_hint_bar(canvas, "ud=Choose;ok=Open");
}

static void storage_view_draw_page(Canvas* canvas, const StorageViewModel* model) {
    char line[48];
    char used[16];
    char free[16];
    canvas_set_font(canvas, FontSecondary);
    if(model->drive == DriveSd) {
        elements_header(canvas, "SD card");
        if(model->sd_ok) {
            snprintf(line, sizeof(line), "%s  %s", model->sd_type, model->sd_label);
            canvas_draw_str(canvas, 3, 25, line);
            storage_view_size(used, sizeof(used), model->sd_total_kb - model->sd_free_kb);
            storage_view_size(free, sizeof(free), model->sd_free_kb);
            snprintf(line, sizeof(line), "%s used, %s free", used, free);
            canvas_draw_str(canvas, 3, 35, line);
            canvas_draw_str(canvas, 3, 45, model->sd_maker);
        } else {
            canvas_draw_str(canvas, 3, 25, "not mounted"); // its buttons say the rest
        }
    } else {
        elements_header(canvas, "Internal");
        if(model->int_ok) {
            storage_view_size(used, sizeof(used), model->int_total_kb - model->int_free_kb);
            storage_view_size(free, sizeof(free), model->int_free_kb);
            snprintf(line, sizeof(line), "%s used, %s free", used, free);
            canvas_draw_str(canvas, 3, 25, line);
        } else {
            canvas_draw_str(canvas, 3, 25, "unreadable");
        }
        canvas_draw_str(canvas, 3, 35, "Firmware and radio stack");
    }

    // the drive's buttons across the bottom, the chosen one filled
    StorageSettingsAction actions[ACTIONS_MAX];
    const char* labels[ACTIONS_MAX];
    const uint8_t count = storage_view_actions(model, actions, labels);
    int32_t widths[ACTIONS_MAX];
    int32_t total = 0;
    for(uint8_t i = 0; i < count; i++) {
        widths[i] = canvas_string_width(canvas, labels[i]) + 8;
        total += widths[i];
    }
    const int32_t gap = count > 1 ? (126 - total) / (count - 1) : 0;
    int32_t x = count > 1 ? 1 : (128 - total) / 2;
    for(uint8_t i = 0; i < count; i++) {
        if(i == model->action) {
            canvas_draw_rbox(canvas, x, PILL_Y, widths[i], PILL_H, 3);
            canvas_set_color(canvas, ColorWhite);
        } else {
            canvas_draw_rframe(canvas, x, PILL_Y, widths[i], PILL_H, 3);
        }
        canvas_draw_str_aligned(
            canvas, x + widths[i] / 2, PILL_Y + PILL_H - 2, AlignCenter, AlignBottom, labels[i]);
        canvas_set_color(canvas, ColorBlack);
        x += widths[i] + gap;
    }
}

static void storage_view_draw(Canvas* canvas, void* model_) {
    const StorageViewModel* model = model_;
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);
    if(model->page) {
        storage_view_draw_page(canvas, model);
    } else {
        storage_view_draw_overview(canvas, model);
    }
}

static bool storage_view_input(InputEvent* event, void* context) {
    StorageSettingsView* instance = context;
    if(event->type != InputTypeShort && event->type != InputTypeRepeat) {
        return event->key != InputKeyBack;
    }
    bool consumed = true;
    bool run = false;
    StorageSettingsAction action = StorageSettingsActionMount;
    with_view_model(
        instance->view,
        StorageViewModel * model,
        {
            if(!model->page) {
                if(event->key == InputKeyUp || event->key == InputKeyDown) {
                    model->drive = model->drive == DriveSd ? DriveInternal : DriveSd;
                } else if(event->key == InputKeyOk && event->type == InputTypeShort) {
                    model->page = true;
                    model->action = 0;
                } else if(event->key == InputKeyBack) {
                    consumed = false; // the app closes
                }
            } else {
                StorageSettingsAction actions[ACTIONS_MAX];
                const char* labels[ACTIONS_MAX];
                const uint8_t count = storage_view_actions(model, actions, labels);
                if(event->key == InputKeyLeft && model->action > 0) {
                    model->action--;
                } else if(event->key == InputKeyRight && model->action + 1 < count) {
                    model->action++;
                } else if(event->key == InputKeyOk && event->type == InputTypeShort) {
                    run = model->action < count;
                    if(run) action = actions[model->action];
                } else if(event->key == InputKeyBack) {
                    model->page = false;
                }
            }
        },
        true);
    if(run && instance->callback) instance->callback(instance->context, action);
    return consumed;
}

StorageSettingsView* storage_settings_view_alloc(void) {
    StorageSettingsView* instance = malloc(sizeof(StorageSettingsView));
    instance->callback = NULL;
    instance->context = NULL;
    instance->view = view_alloc();
    view_allocate_model(instance->view, ViewModelTypeLocking, sizeof(StorageViewModel));
    view_set_context(instance->view, instance);
    view_set_draw_callback(instance->view, storage_view_draw);
    view_set_input_callback(instance->view, storage_view_input);
    with_view_model(
        instance->view,
        StorageViewModel * model,
        { memset(model, 0, sizeof(StorageViewModel)); },
        false);
    return instance;
}

void storage_settings_view_free(StorageSettingsView* instance) {
    furi_check(instance);
    view_free(instance->view);
    free(instance);
}

View* storage_settings_view_get_view(StorageSettingsView* instance) {
    furi_check(instance);
    return instance->view;
}

void storage_settings_view_set_callback(
    StorageSettingsView* instance,
    StorageSettingsViewCallback callback,
    void* context) {
    furi_check(instance);
    instance->callback = callback;
    instance->context = context;
}

void storage_settings_view_refresh(StorageSettingsView* instance, Storage* storage) {
    furi_check(instance);
    // read before the model is held: the card's free space is counted, and the screen waits not
    SDInfo sd_info;
    const bool sd_ok = storage_sd_info(storage, &sd_info) == FSE_OK;
    // Internal is the Flipper's own flash: /int is a folder on the card here, and measured the
    // card over again. What the firmware and the radio stack leave free of its 1 MB
    const uint32_t flash_free = (uint32_t)furi_hal_flash_get_free_end_address() -
                                (uint32_t)furi_hal_flash_get_free_start_address();

    with_view_model(
        instance->view,
        StorageViewModel * model,
        {
            model->sd_ok = sd_ok;
            if(sd_ok) {
                model->sd_total_kb = sd_info.kb_total;
                model->sd_free_kb = sd_info.kb_free;
                strlcpy(
                    model->sd_type, sd_api_get_fs_type_text(sd_info.fs_type), sizeof(model->sd_type));
                strlcpy(model->sd_label, sd_info.label, sizeof(model->sd_label));
                snprintf(
                    model->sd_maker,
                    sizeof(model->sd_maker),
                    "%s %s",
                    sd_info.oem_id,
                    sd_info.product_name);
            }
            model->int_ok = true;
            model->int_total_kb = 1024;
            model->int_free_kb = flash_free / 1024;
            // a drive's page stays open (back from its action), on a button it still has
            StorageSettingsAction actions[ACTIONS_MAX];
            const char* labels[ACTIONS_MAX];
            const uint8_t count = storage_view_actions(model, actions, labels);
            if(model->action >= count) model->action = 0;
        },
        true);
}
