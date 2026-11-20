/**
 * Crowd Meter: how busy a place is, from the Bluetooth LE devices heard around (phones, watches,
 * earbuds, trackers), with a graph of the last 20 minutes and what kinds of device they are.
 * Counts devices, not people: one person often carries two or three. Needs the BLE Full radio
 * stack.
 */
#include "ble_tools.h"

#include <input/input.h>
#include <stdio.h>

#define CAPACITY     160U
#define WINDOW_MS    (60U * 1000U) /* counted: heard in the last minute */
#define FORGET_MS    (2U * 60U * 1000U)
#define SAMPLE_MS    (10U * 1000U)
#define HISTORY      124U /* one sample a column: 20 minutes */
#define TICK_MS      500U

typedef enum {
    PageNow,
    PageKinds,
    PageCount,
} Page;

typedef enum {
    GroupApple,
    GroupSamsung,
    GroupWindows,
    GroupTrackers,
    GroupOther,
    GroupCount,
} Group;

static const char* const group_names[GroupCount] = {
    [GroupApple] = "Apple",
    [GroupSamsung] = "Samsung",
    [GroupWindows] = "Windows",
    [GroupTrackers] = "Trackers",
    [GroupOther] = "Other",
};

typedef struct {
    Gui* gui;
    ViewPort* view_port;
    FuriMessageQueue* input;
    FuriMutex* mutex;

    BleToolsScan scan;
    BleSeen devices[CAPACITY];
    BleSeenTable table;

    bool supported;
    uint32_t started;
    Page page;
    size_t now_count;
    size_t peak;
    uint16_t groups[GroupCount];
    uint16_t history[HISTORY]; /**< newest last */
    size_t history_count;
} CrowdMeter;

static Group group_of(BleKind kind) {
    switch(kind) {
    case BleKindApple:
    case BleKindAirPods:
        return GroupApple;
    case BleKindSamsung:
        return GroupSamsung;
    case BleKindMicrosoft:
        return GroupWindows;
    case BleKindFindMy:
    case BleKindSmartTag:
    case BleKindTile:
    case BleKindGoogleFind:
        return GroupTrackers;
    default:
        return GroupOther;
    }
}

static const char* crowd_word(size_t count) {
    return count < 5 ? "Quiet" : count < 15 ? "A few people" : count < 40 ? "Busy" : "Crowded";
}

static void count_now(CrowdMeter* app, uint32_t now) {
    uint16_t by_kind[BleKindCount];
    app->now_count = ble_seen_count_recent(&app->table, now, WINDOW_MS, by_kind);
    if(app->now_count > app->peak) app->peak = app->now_count;
    memset(app->groups, 0, sizeof(app->groups));
    for(size_t kind = 0; kind < BleKindCount; kind++) {
        app->groups[group_of((BleKind)kind)] += by_kind[kind];
    }
}

static void add_sample(CrowdMeter* app) {
    const uint16_t value = (uint16_t)app->now_count;
    if(app->history_count < HISTORY) {
        app->history[app->history_count++] = value;
    } else {
        memmove(app->history, app->history + 1, (HISTORY - 1) * sizeof(uint16_t));
        app->history[HISTORY - 1] = value;
    }
}

static void draw_now(Canvas* canvas, CrowdMeter* app, uint32_t now) {
    char text[24];
    // the count, and what it means
    snprintf(text, sizeof(text), "%u%s", (unsigned)app->now_count, app->now_count >= CAPACITY ? "+" : "");
    canvas_set_font(canvas, FontBigNumbers);
    canvas_draw_str(canvas, 2, 30, text);
    const int x = 6 + canvas_string_width(canvas, text);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, x, 22, crowd_word(app->now_count));
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, x, 31, "devices, last min");

    // the last 20 minutes, scaled to the busiest moment (at least 10), under the peak
    canvas_draw_line(canvas, 0, 63, 127, 63);
    if(app->history_count < 2) {
        const uint32_t running = now - app->started;
        if(running < SAMPLE_MS * 2) {
            snprintf(text, sizeof(text), "graph in %lus", (unsigned long)((SAMPLE_MS * 2 - running) / 1000 + 1));
            canvas_draw_str_aligned(canvas, 64, 50, AlignCenter, AlignTop, text);
        }
        return;
    }
    uint16_t top = 10;
    for(size_t i = 0; i < app->history_count; i++) {
        if(app->history[i] > top) top = app->history[i];
    }
    for(size_t i = 0; i < app->history_count; i++) {
        const int h = app->history[i] * 16 / top;
        const int column = 127 - (int)(app->history_count - 1 - i);
        if(h) canvas_draw_line(canvas, column, 62, column, 62 - h);
    }
    snprintf(text, sizeof(text), "peak %u", (unsigned)app->peak);
    canvas_draw_str(canvas, 0, 43, text);
}

static void draw_kinds(Canvas* canvas, CrowdMeter* app) {
    char text[8];
    canvas_set_font(canvas, FontSecondary);
    uint16_t top = 1;
    for(size_t group = 0; group < GroupCount; group++) {
        if(app->groups[group] > top) top = app->groups[group];
    }
    for(size_t group = 0; group < GroupCount; group++) {
        const int y = 21 + (int)group * 10;
        canvas_draw_str(canvas, 0, y, group_names[group]);
        snprintf(text, sizeof(text), "%u", (unsigned)app->groups[group]);
        canvas_draw_str_aligned(canvas, 64, y, AlignRight, AlignBottom, text);
        const int width = app->groups[group] * 60 / top;
        if(width) canvas_draw_box(canvas, 67, y - 6, width, 5);
    }
}

static void draw_callback(Canvas* canvas, void* context) {
    CrowdMeter* app = context;
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    canvas_clear(canvas);
    if(!app->supported) {
        ble_tools_draw_unsupported(canvas);
    } else {
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str(canvas, 2, 10, "Crowd Meter");
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(
            canvas, 126, 10, AlignRight, AlignBottom, app->page == PageNow ? "< kinds >" : "< now >");
        canvas_draw_line(canvas, 0, 12, 127, 12);
        if(app->page == PageNow) {
            draw_now(canvas, app, furi_get_tick());
        } else {
            draw_kinds(canvas, app);
        }
    }
    furi_mutex_release(app->mutex);
}

static void input_callback(InputEvent* event, void* context) {
    CrowdMeter* app = context;
    furi_message_queue_put(app->input, event, FuriWaitForever);
}

int32_t crowd_meter_app(void* p) {
    UNUSED(p);
    CrowdMeter* app = malloc(sizeof(CrowdMeter));
    memset(app, 0, sizeof(CrowdMeter));
    app->table = (BleSeenTable){app->devices, CAPACITY, 0};
    app->input = furi_message_queue_alloc(8, sizeof(InputEvent));
    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->view_port = view_port_alloc();
    view_port_draw_callback_set(app->view_port, draw_callback, app);
    view_port_input_callback_set(app->view_port, input_callback, app);
    app->gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);

    // passive: only listens, and devices are not asked anything
    app->started = furi_get_tick();
    app->supported = ble_tools_scan_start(&app->scan, false);

    uint32_t last_tick = 0, last_sample = app->started;
    bool running = true;
    while(running) {
        InputEvent event;
        if(furi_message_queue_get(app->input, &event, 20) == FuriStatusOk &&
           event.type == InputTypeShort) {
            furi_mutex_acquire(app->mutex, FuriWaitForever);
            if(event.key == InputKeyBack) running = false;
            if(event.key == InputKeyLeft || event.key == InputKeyRight || event.key == InputKeyOk) {
                app->page = (app->page + 1) % PageCount;
            }
            furi_mutex_release(app->mutex);
            view_port_update(app->view_port);
        }
        const uint32_t now = furi_get_tick();
        furi_mutex_acquire(app->mutex, FuriWaitForever);
        GapScannerReport report;
        while(ble_tools_scan_next(&app->scan, &report)) {
            ble_seen_update(&app->table, report.address, report.rssi, report.data, report.data_len, now);
        }
        if(now - last_tick >= TICK_MS) {
            last_tick = now;
            ble_seen_expire(&app->table, now, FORGET_MS, FORGET_MS);
            count_now(app, now);
            if(now - last_sample >= SAMPLE_MS) {
                last_sample += SAMPLE_MS;
                add_sample(app);
            }
            view_port_update(app->view_port);
        }
        furi_mutex_release(app->mutex);
    }

    ble_tools_scan_free(&app->scan);
    gui_remove_view_port(app->gui, app->view_port);
    view_port_free(app->view_port);
    furi_record_close(RECORD_GUI);
    furi_message_queue_free(app->input);
    furi_mutex_free(app->mutex);
    free(app);
    return 0;
}
