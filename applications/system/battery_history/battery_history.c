/**
 * Battery History: the charge over the last day to two weeks, from the records the power service
 * keeps (see battery_log.h), and how long the battery will last at the rate it has been going down.
 */
#include "battery_stats.h"

#include <furi.h>
#include <furi_hal_power.h>
#include <furi_hal_rtc.h>
#include <gui/gui.h>
#include <input/input.h>
#include <storage/storage.h>
#include <stdio.h>

#define MAX_RECORDS  (2U * BATTERY_LOG_ROTATE_SIZE / sizeof(BatteryLogRecord))
#define GRAPH_TOP    23
#define GRAPH_BOTTOM 58
#define GRAPH_WIDTH  128U

typedef struct {
    uint32_t seconds;
    const char* name;
} Span;

static const Span spans[] = {
    {24U * 3600U, "1 day"},
    {3U * 24U * 3600U, "3 days"},
    {7U * 24U * 3600U, "1 week"},
    {14U * 24U * 3600U, "2 weeks"},
};

typedef struct {
    Gui* gui;
    ViewPort* view_port;
    FuriMessageQueue* input;
    FuriMutex* mutex;

    BatteryLogRecord* records; /**< oldest first, the reading now last */
    size_t count;
    size_t span;
    int8_t charge[GRAPH_WIDTH];
    bool charging[GRAPH_WIDTH];
    bool has_estimate;
    BatteryEstimate estimate;
} BatteryHistory;

static size_t read_records(Storage* storage, const char* path, BatteryLogRecord* records, size_t room) {
    File* file = storage_file_alloc(storage);
    size_t count = 0;
    if(storage_file_open(file, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        // the newest records, if the file holds more than there is room for
        const uint64_t size = storage_file_size(file);
        const uint64_t whole = size / sizeof(BatteryLogRecord);
        if(whole > room) storage_file_seek(file, (uint32_t)((whole - room) * sizeof(BatteryLogRecord)), true);
        const size_t want = (size_t)(whole < room ? whole : room);
        count = storage_file_read(file, records, want * sizeof(BatteryLogRecord)) / sizeof(BatteryLogRecord);
    }
    storage_file_free(file);
    return count;
}

static void load(BatteryHistory* app) {
    app->records = malloc(sizeof(BatteryLogRecord) * (MAX_RECORDS + 1));
    Storage* storage = furi_record_open(RECORD_STORAGE);
    // the newer file first, so that it is never cut for the older one
    const size_t newer = read_records(storage, BATTERY_LOG_PATH, app->records, MAX_RECORDS);
    BatteryLogRecord* older = malloc(sizeof(BatteryLogRecord) * (MAX_RECORDS - newer + 1));
    const size_t older_count = read_records(storage, BATTERY_LOG_OLD_PATH, older, MAX_RECORDS - newer);
    furi_record_close(RECORD_STORAGE);
    memmove(app->records + older_count, app->records, newer * sizeof(BatteryLogRecord));
    memcpy(app->records, older, older_count * sizeof(BatteryLogRecord));
    free(older);
    app->count = older_count + newer;

    // and the reading now, so that the graph reaches the right edge
    const BatteryLogRecord now = {
        .timestamp = furi_hal_rtc_get_timestamp(),
        .charge = furi_hal_power_get_pct(),
        .flags = furi_hal_power_is_charging() ? BatteryLogFlagCharging : 0,
    };
    app->records[app->count++] = now;
    app->has_estimate = battery_stats_estimate(app->records, app->count, &app->estimate);
}

static void compute_columns(BatteryHistory* app) {
    const BatteryLogRecord* now = &app->records[app->count - 1];
    battery_stats_columns(
        app->records,
        app->count,
        now->timestamp,
        spans[app->span].seconds,
        app->charge,
        app->charging,
        GRAPH_WIDTH);
}

static int graph_y(int charge) {
    return GRAPH_BOTTOM - charge * (GRAPH_BOTTOM - GRAPH_TOP) / 100;
}

static void draw_callback(Canvas* canvas, void* context) {
    BatteryHistory* app = context;
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    canvas_clear(canvas);
    const BatteryLogRecord* now = &app->records[app->count - 1];
    char text[48];

    canvas_set_font(canvas, FontPrimary);
    snprintf(text, sizeof(text), "Battery %u%%", now->charge);
    canvas_draw_str(canvas, 0, 9, text);
    canvas_set_font(canvas, FontSecondary);
    snprintf(text, sizeof(text), "< %s >", spans[app->span].name);
    canvas_draw_str_aligned(canvas, 127, 9, AlignRight, AlignBottom, text);

    if(now->flags & BatteryLogFlagCharging) {
        snprintf(text, sizeof(text), "Charging");
    } else if(app->has_estimate) {
        char duration[24];
        battery_stats_format_duration(app->estimate.seconds_left, duration, sizeof(duration));
        snprintf(
            text,
            sizeof(text),
            "%s left, %lu%%/day",
            duration,
            (unsigned long)((app->estimate.percent_per_day_x10 + 5) / 10));
    } else if(app->count < 13) {
        snprintf(text, sizeof(text), "Recording: back in a few hours");
    } else {
        snprintf(text, sizeof(text), "On battery too briefly to tell");
    }
    canvas_draw_str(canvas, 0, 19, text);

    // grid: 100%, 50% and 0%
    for(int x = 0; x < 128; x += 4) {
        canvas_draw_dot(canvas, x, graph_y(100));
        canvas_draw_dot(canvas, x, graph_y(50));
        canvas_draw_dot(canvas, x, graph_y(0) + 1);
    }
    // the charge, broken where the Flipper was off
    int last_x = -1, last_y = 0;
    for(int x = 0; x < (int)GRAPH_WIDTH; x++) {
        if(app->charge[x] == BATTERY_STATS_NO_DATA) {
            last_x = -1;
            continue;
        }
        const int y = graph_y(app->charge[x]);
        if(last_x >= 0) {
            canvas_draw_line(canvas, last_x, last_y, x, y);
        } else {
            canvas_draw_dot(canvas, x, y);
        }
        last_x = x;
        last_y = y;
        // charging: a band under the graph
        if(app->charging[x]) canvas_draw_line(canvas, x, 61, x, 63);
    }
    furi_mutex_release(app->mutex);
}

static void input_callback(InputEvent* event, void* context) {
    BatteryHistory* app = context;
    furi_message_queue_put(app->input, event, FuriWaitForever);
}

int32_t battery_history_app(void* p) {
    UNUSED(p);
    BatteryHistory* app = malloc(sizeof(BatteryHistory));
    memset(app, 0, sizeof(BatteryHistory));
    app->input = furi_message_queue_alloc(8, sizeof(InputEvent));
    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    load(app);
    compute_columns(app);

    app->view_port = view_port_alloc();
    view_port_draw_callback_set(app->view_port, draw_callback, app);
    view_port_input_callback_set(app->view_port, input_callback, app);
    app->gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);

    InputEvent event;
    while(furi_message_queue_get(app->input, &event, FuriWaitForever) == FuriStatusOk) {
        if(event.type != InputTypeShort && event.type != InputTypeRepeat) continue;
        if(event.key == InputKeyBack) break;
        furi_mutex_acquire(app->mutex, FuriWaitForever);
        if(event.key == InputKeyRight && app->span + 1 < COUNT_OF(spans)) app->span++;
        if(event.key == InputKeyLeft && app->span > 0) app->span--;
        compute_columns(app);
        furi_mutex_release(app->mutex);
        view_port_update(app->view_port);
    }

    gui_remove_view_port(app->gui, app->view_port);
    view_port_free(app->view_port);
    furi_record_close(RECORD_GUI);
    furi_message_queue_free(app->input);
    furi_mutex_free(app->mutex);
    free(app->records);
    free(app);
    return 0;
}
