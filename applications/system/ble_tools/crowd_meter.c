/**
 * Crowd Meter: how many people are around, from the Bluetooth LE devices heard: phones, and
 * earbuds or watches beyond the phones, not trackers, beacons, televisions or computers, and not
 * your own (ble_crowd.h). Up and Down choose how far: near you, this room, or everything heard;
 * each keeps its own graph of the last 20 minutes. The second page says what the devices are. An
 * estimate: a phone that keeps quiet is not heard. Needs the BLE Full radio stack.
 */
#include "ble_tools.h"
#include "ble_crowd.h"

#include <gui/elements.h>
#include <input/input.h>
#include <stdio.h>

#define CAPACITY     160U
#define WINDOW_MS    (60U * 1000U) /* counted: heard in the last minute */
#define FORGET_MS    (2U * 60U * 1000U)
#define SAMPLE_MS    (10U * 1000U)
#define HISTORY      124U /* one sample a column: 20 minutes */
#define TICK_MS      500U

/* How far is counted, by how strong a device is heard: a rough distance, as walls, pockets and
 * bodies weaken the signal too */
typedef enum {
    RangeNear,
    RangeRoom,
    RangeAll,
    RangeCount,
} Range;

static const struct {
    int min_dbm;
    const char* name;
} ranges[RangeCount] = {
    [RangeNear] = {-65, "within ~3 m"},
    [RangeRoom] = {-78, "within ~10 m"},
    [RangeAll] = {-127, "all heard"},
};

typedef enum {
    PageNow,
    PageKinds,
    PageCount,
} Page;


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
    Range range;
    BleCrowd crowd[RangeCount];
    size_t peak[RangeCount];
    uint16_t history[RangeCount][HISTORY]; /**< people in each range, newest last */
    size_t history_count;
    BleMine mine;
} CrowdMeter;

static const char* crowd_word(size_t people) {
    return people < 3 ? "Quiet" : people < 8 ? "A few people" : people < 20 ? "Busy" : "Crowded";
}

static void count_now(CrowdMeter* app, uint32_t now) {
    // every range, so that each graph goes on while another is shown
    for(size_t range = 0; range < RangeCount; range++) {
        ble_crowd_count(&app->table, now, WINDOW_MS, ranges[range].min_dbm, &app->crowd[range]);
        if(app->crowd[range].people > app->peak[range]) app->peak[range] = app->crowd[range].people;
    }
}

static void add_sample(CrowdMeter* app) {
    const bool full = app->history_count == HISTORY;
    for(size_t range = 0; range < RangeCount; range++) {
        uint16_t* history = app->history[range];
        if(full) memmove(history, history + 1, (HISTORY - 1) * sizeof(uint16_t));
        history[full ? HISTORY - 1 : app->history_count] = app->crowd[range].people;
    }
    if(!full) app->history_count++;
}

static void draw_now(Canvas* canvas, CrowdMeter* app, uint32_t now) {
    char text[24];
    // the estimate, and what it means
    const BleCrowd* crowd = &app->crowd[app->range];
    const uint16_t* history = app->history[app->range];
    const size_t people = crowd->people;
    snprintf(text, sizeof(text), "%u%s", (unsigned)people, app->table.count >= CAPACITY ? "+" : "");
    canvas_set_font(canvas, FontBigNumbers);
    canvas_draw_str(canvas, 2, 30, text);
    const int x = 6 + canvas_string_width(canvas, text);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, x, 22, crowd_word(people));
    canvas_set_font(canvas, FontSecondary);
    // the range, with the buttons that change it
    snprintf(text, sizeof(text), "ud=%s", ranges[app->range].name);
    elements_hints(canvas, x, 31, AlignLeft, text);

    // the last 20 minutes, scaled to the busiest moment (at least 10), under the peak
    canvas_draw_line(canvas, 2, 62, 125, 62); // two clear columns and a row from the edges
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
        if(history[i] > top) top = history[i];
    }
    for(size_t i = 0; i < app->history_count; i++) {
        const int h = history[i] * 16 / top;
        const int column = 125 - (int)(app->history_count - 1 - i);
        if(h) canvas_draw_line(canvas, column, 61, column, 61 - h);
    }
    snprintf(text, sizeof(text), "peak %u", (unsigned)app->peak[app->range]);
    canvas_draw_str(canvas, 2, 43, text);
}

/** What was heard: the devices behind the estimate, and what was left out */
static void draw_kinds(Canvas* canvas, CrowdMeter* app) {
    static const struct {
        BleRole role;
        const char* name;
    } rows[] = {
        {BleRolePhone, "Phones"},
        {BleRoleWearable, "Worn"},
        {BleRoleThing, "Things"},
        {BleRoleUnknown, "Unknown"},
    };
    char text[8];
    const BleCrowd* crowd = &app->crowd[app->range];
    canvas_set_font(canvas, FontSecondary);
    uint16_t top = crowd->mine ? crowd->mine : 1;
    for(size_t i = 0; i < COUNT_OF(rows); i++) {
        if(crowd->by_role[rows[i].role] > top) top = crowd->by_role[rows[i].role];
    }
    for(size_t i = 0; i <= COUNT_OF(rows); i++) {
        const bool yours = i == COUNT_OF(rows);
        const uint16_t value = yours ? crowd->mine : crowd->by_role[rows[i].role];
        const int y = 22 + (int)i * 10;
        canvas_draw_str(canvas, 2, y, yours ? "Yours" : rows[i].name);
        snprintf(text, sizeof(text), "%u", (unsigned)value);
        canvas_draw_str_aligned(canvas, 64, y, AlignRight, AlignBottom, text);
        const int width = value * 58 / top; // ends two columns short of the edge
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
        canvas_draw_str(canvas, 2, 9, "Crowd Meter");
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(
            canvas, 125, 9, AlignRight, AlignBottom, app->page == PageNow ? "< kinds >" : "< now >");
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
    app->range = RangeAll; // as it always counted; Up for nearer
    ble_tools_mine_load(&app->mine);
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
            // nearer and further
            if(event.key == InputKeyUp && app->range > 0) app->range--;
            if(event.key == InputKeyDown && app->range + 1 < RangeCount) app->range++;
            furi_mutex_release(app->mutex);
            view_port_update(app->view_port);
        }
        const uint32_t now = furi_get_tick();
        furi_mutex_acquire(app->mutex, FuriWaitForever);
        GapScannerReport report;
        while(ble_tools_scan_next(&app->scan, &report)) {
            BleSeen* device =
                ble_seen_update(&app->table, report.address, report.rssi, report.data, report.data_len, now);
            // your own phone and earbuds are you, not the crowd
            if(device) ble_tools_recognise(&app->mine, device);
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
