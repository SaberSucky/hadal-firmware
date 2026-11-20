/**
 * Tracker Detector: warns when a tracker that is away from its owner (an AirTag or other Find My
 * accessory, a Google Find My Device tag, a SmartTag or a Tile) keeps travelling with you, and
 * helps find it. Your own trackers, marked with OK held (here or in Find My Stuff), never warn.
 * Needs the BLE Full radio stack.
 */
#include "ble_tools.h"

#include <gui/elements.h>
#include <input/input.h>
#include <stdio.h>

#define CAPACITY     48U
#define ROWS         3U /* over the hint bar */
#define ROW_H        12
#define LIST_Y       14
#define FOLLOW_MS    (10U * 60U * 1000U) /* with you this long: a warning */
#define FORGET_MS    (3U * 60U * 1000U) /* unheard this long: gone (its clock starts again) */
#define SORT_MS   1000U /* resorting less often keeps the list still enough to read */
#define TICK_MS      250U

typedef enum {
    ModeList,
    ModeLocate,
} Mode;

typedef struct {
    Gui* gui;
    ViewPort* view_port;
    FuriMessageQueue* input;
    FuriMutex* mutex;
    NotificationApp* notifications;

    BleToolsScan scan;
    BleSeen devices[CAPACITY];
    BleSeenTable table;
    size_t order[CAPACITY];

    bool supported;
    uint32_t started;
    Mode mode;
    size_t selected;
    size_t top;
    bool warning; /**< the warning is on screen */
    uint8_t warned_addr[BLE_SEEN_ADDR_SIZE];
    uint8_t locate_addr[BLE_SEEN_ADDR_SIZE];
    char locate_label[BLE_SEEN_NAME_SIZE];
    BleMeter meter;
    bool sound;
    uint32_t last_click;
    BleMine mine;
} TrackerDetector;

static const BleSeen* tracker_at(const TrackerDetector* app, size_t row) {
    return row < app->table.count ? &app->devices[app->order[row]] : NULL;
}

/** Whether a goes after b: yours last, then the longest with you first */
static bool tracker_after(const BleSeen* a, const BleSeen* b) {
    if(a->mine != b->mine) return a->mine;
    return a->first_seen > b->first_seen;
}

static void tracker_sort(TrackerDetector* app) {
    for(size_t i = 0; i < app->table.count; i++) {
        app->order[i] = i;
    }
    for(size_t i = 1; i < app->table.count; i++) {
        const size_t value = app->order[i];
        size_t j = i;
        while(j > 0 && tracker_after(&app->devices[app->order[j - 1]], &app->devices[value])) {
            app->order[j] = app->order[j - 1];
            j--;
        }
        app->order[j] = value;
    }
}

static void scroll_to_selection(TrackerDetector* app) {
    if(app->selected >= app->table.count) app->selected = app->table.count ? app->table.count - 1 : 0;
    if(app->selected < app->top) app->top = app->selected;
    if(app->selected >= app->top + ROWS) app->top = app->selected - ROWS + 1;
    if(app->top + ROWS > app->table.count) {
        app->top = app->table.count > ROWS ? app->table.count - ROWS : 0;
    }
}

static void draw_list(Canvas* canvas, TrackerDetector* app, uint32_t now) {
    char text[40];
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 9, "Tracker Detector"); // a clear row above the rule
    canvas_draw_line(canvas, 0, 12, 127, 12);
    canvas_set_font(canvas, FontSecondary);

    if(!app->table.count) {
        canvas_draw_str_aligned(canvas, 64, 18, AlignCenter, AlignTop, "No trackers around");
        canvas_draw_str_aligned(canvas, 64, 31, AlignCenter, AlignTop, "Keep this open: it warns");
        canvas_draw_str_aligned(canvas, 64, 41, AlignCenter, AlignTop, "you if one stays with you");
        snprintf(text, sizeof(text), "watching %lu min", (unsigned long)((now - app->started) / 60000));
        canvas_draw_str_aligned(canvas, 64, 54, AlignCenter, AlignTop, text);
        return;
    }

    for(size_t row = 0; row < ROWS; row++) {
        const BleSeen* device = tracker_at(app, app->top + row);
        if(!device) break;
        const int y = LIST_Y + (int)row * ROW_H;
        const bool selected = app->top + row == app->selected;
        if(selected) {
            canvas_draw_rbox(canvas, 0, y, 124, ROW_H, 2);
            canvas_set_color(canvas, ColorWhite);
        }
        // right: yours, or whether its owner is near and how long it has been with you
        if(device->mine) {
            snprintf(text, sizeof(text), "mine");
        } else {
            snprintf(
                text,
                sizeof(text),
                "%s %lum",
                device->separated ? "away" : "owner",
                (unsigned long)(ble_seen_with_you_ms(device, now) / 60000));
        }
        canvas_draw_str_aligned(canvas, 121, y + 9, AlignRight, AlignBottom, text);
        const size_t right = canvas_string_width(canvas, text);
        // left: what it is, ticked when yours, marked when it follows you
        char label[BLE_SEEN_NAME_SIZE];
        ble_tools_label(device, label, sizeof(label));
        int32_t x = 3;
        if(device->mine) {
            ble_tools_draw_mine_mark(canvas, x, y + 9);
            x += BLE_TOOLS_MINE_MARK_W;
        }
        const bool following = ble_seen_is_following(device, now, FOLLOW_MS);
        snprintf(text, sizeof(text), "%s%s", following ? "! " : "", label);
        ble_tools_draw_fit(canvas, x, y + 9, text, 116 - x - right);
        canvas_set_color(canvas, ColorBlack);
    }
    // scroll position
    const int list_h = ROWS * ROW_H;
    if(app->table.count > ROWS) {
        const int height = list_h * ROWS / (int)app->table.count;
        const int y = LIST_Y + (list_h - height) * (int)app->top / (int)(app->table.count - ROWS);
        canvas_draw_box(canvas, 126, y, 2, height);
    }
    const BleSeen* selected = tracker_at(app, app->selected);
    elements_hint_bar(
        canvas, selected && selected->mine ? "ok=Find;ok=Hold: not mine" : "ok=Find;ok=Hold: mine");
}

static void draw_warning(Canvas* canvas, TrackerDetector* app, uint32_t now) {
    // full screen: nothing else matters until it is read
    canvas_clear(canvas);
    canvas_draw_rframe(canvas, 0, 0, 128, 64, 3);
    canvas_draw_box(canvas, 0, 0, 128, 14);
    canvas_set_color(canvas, ColorWhite);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 3, AlignCenter, AlignTop, "A tracker is with you");
    canvas_set_color(canvas, ColorBlack);

    canvas_set_font(canvas, FontSecondary);
    char text[40];
    for(size_t i = 0; i < app->table.count; i++) {
        const BleSeen* device = &app->devices[i];
        if(memcmp(device->addr, app->warned_addr, BLE_SEEN_ADDR_SIZE) != 0) continue;
        char label[BLE_SEEN_NAME_SIZE];
        ble_tools_label(device, label, sizeof(label));
        snprintf(
            text,
            sizeof(text),
            "%s, %lu min",
            label,
            (unsigned long)(ble_seen_with_you_ms(device, now) / 60000));
        canvas_draw_str_aligned(canvas, 64, 18, AlignCenter, AlignTop, text);
    }
    canvas_draw_str_aligned(canvas, 64, 29, AlignCenter, AlignTop, "away from its owner and");
    canvas_draw_str_aligned(canvas, 64, 38, AlignCenter, AlignTop, "moving along with you");
    canvas_draw_line(canvas, 4, 50, 123, 50);
    // a clear row under the descenders, above the frame
    elements_hints(canvas, 64, 60, AlignCenter, "ok=Find;back=Close;ok=Hold: mine");
}

static void draw_callback(Canvas* canvas, void* context) {
    TrackerDetector* app = context;
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    canvas_clear(canvas);
    const uint32_t now = furi_get_tick();
    if(!app->supported) {
        ble_tools_draw_unsupported(canvas);
    } else if(app->mode == ModeLocate) {
        ble_meter_draw(canvas, &app->meter, app->locate_label, now);
        ble_tools_draw_sound_hint(canvas, app->sound);
    } else {
        draw_list(canvas, app, now);
        if(app->warning) draw_warning(canvas, app, now);
    }
    furi_mutex_release(app->mutex);
}

static void input_callback(InputEvent* event, void* context) {
    TrackerDetector* app = context;
    furi_message_queue_put(app->input, event, FuriWaitForever);
}

static void start_locating(TrackerDetector* app, const BleSeen* device) {
    memcpy(app->locate_addr, device->addr, BLE_SEEN_ADDR_SIZE);
    ble_tools_label(device, app->locate_label, sizeof(app->locate_label));
    ble_meter_reset(&app->meter);
    ble_meter_add(&app->meter, device->rssi, device->last_seen);
    app->mode = ModeLocate;
    app->sound = true;
}

static BleSeen* tracker_by_addr(TrackerDetector* app, const uint8_t* addr) {
    for(size_t i = 0; i < app->table.count; i++) {
        if(memcmp(app->devices[i].addr, addr, BLE_SEEN_ADDR_SIZE) == 0) return &app->devices[i];
    }
    return NULL;
}

/** Mark a device as yours, or not any more, and write the list */
static void toggle_mine(TrackerDetector* app, BleSeen* device) {
    char label[BLE_SEEN_NAME_SIZE];
    ble_tools_label(device, label, sizeof(label));
    ble_mine_set(&app->mine, device, !device->mine, label);
    ble_tools_mine_save(&app->mine);
    notification_message(app->notifications, &sequence_single_vibro);
}

// Returns false to quit
static bool handle_input(TrackerDetector* app, const InputEvent* event) {
    if(event->type == InputTypeLong && event->key == InputKeyOk && app->mode == ModeList) {
        // held: yours, or not any more (from the warning: yours, and the warning goes)
        BleSeen* device = app->warning ? tracker_by_addr(app, app->warned_addr) :
                                         (BleSeen*)tracker_at(app, app->selected);
        if(device) toggle_mine(app, device);
        if(app->warning) {
            app->warning = false;
            notification_message(app->notifications, &sequence_reset_red);
        }
        tracker_sort(app);
        // the selection goes with the device to its new place in the list
        for(size_t row = 0; device && row < app->table.count; row++) {
            if(tracker_at(app, row) == device) app->selected = row;
        }
        scroll_to_selection(app);
        return true;
    }
    if(event->type != InputTypeShort && event->type != InputTypeRepeat) return true;
    if(app->mode == ModeLocate) {
        if(event->key == InputKeyBack) app->mode = ModeList;
        if(event->key == InputKeyOk) app->sound = !app->sound;
        return true;
    }
    if(app->warning) {
        if(event->key == InputKeyOk) {
            for(size_t i = 0; i < app->table.count; i++) {
                if(memcmp(app->devices[i].addr, app->warned_addr, BLE_SEEN_ADDR_SIZE) == 0) {
                    start_locating(app, &app->devices[i]);
                }
            }
        }
        if(event->key == InputKeyOk || event->key == InputKeyBack) {
            app->warning = false;
            notification_message(app->notifications, &sequence_reset_red);
        }
        return true;
    }
    switch(event->key) {
    case InputKeyBack:
        return false;
    case InputKeyUp:
        if(app->selected) app->selected--;
        break;
    case InputKeyDown:
        if(app->selected + 1 < app->table.count) app->selected++;
        break;
    case InputKeyOk:
        if(tracker_at(app, app->selected)) start_locating(app, tracker_at(app, app->selected));
        break;
    default:
        break;
    }
    scroll_to_selection(app);
    return true;
}

static void take_reports(TrackerDetector* app, uint32_t now) {
    GapScannerReport report;
    const size_t sorted = app->table.count;
    while(ble_tools_scan_next(&app->scan, &report)) {
        // only trackers are kept: a busy street must not push them out of the table
        BleSeen probe = {0};
        ble_seen_parse(&probe, report.data, report.data_len);
        bool known = false;
        for(size_t i = 0; i < app->table.count && !known; i++) {
            known = memcmp(app->devices[i].addr, report.address, BLE_SEEN_ADDR_SIZE) == 0;
        }
        if(!known && !ble_kind_is_tracker(probe.kind)) continue;
        BleSeen* device =
            ble_seen_update(&app->table, report.address, report.rssi, report.data, report.data_len, now);
        if(device && device->count == 1) ble_mine_recognise(&app->mine, device);
        if(app->mode == ModeLocate &&
           memcmp(report.address, app->locate_addr, BLE_SEEN_ADDR_SIZE) == 0) {
            ble_meter_add(&app->meter, report.rssi, now);
        }
    }
    // new devices go at the end until the next sort
    for(size_t i = sorted; i < app->table.count; i++) {
        app->order[i] = i;
    }
}

static void check_following(TrackerDetector* app, uint32_t now) {
    for(size_t i = 0; i < app->table.count; i++) {
        BleSeen* device = &app->devices[i];
        if(device->alerted || !ble_seen_is_following(device, now, FOLLOW_MS)) continue;
        device->alerted = true;
        memcpy(app->warned_addr, device->addr, BLE_SEEN_ADDR_SIZE);
        app->warning = true;
        notification_message(app->notifications, &sequence_display_backlight_on);
        notification_message(app->notifications, &sequence_double_vibro);
        notification_message(app->notifications, &sequence_set_red_255);
    }
}

int32_t tracker_detector_app(void* p) {
    UNUSED(p);
    TrackerDetector* app = malloc(sizeof(TrackerDetector));
    memset(app, 0, sizeof(TrackerDetector));
    app->table = (BleSeenTable){app->devices, CAPACITY, 0};
    app->input = furi_message_queue_alloc(8, sizeof(InputEvent));
    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);
    app->view_port = view_port_alloc();
    view_port_draw_callback_set(app->view_port, draw_callback, app);
    view_port_input_callback_set(app->view_port, input_callback, app);
    app->gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);

    app->started = furi_get_tick();
    ble_tools_mine_load(&app->mine);
    app->supported = ble_tools_scan_start(&app->scan, false);

    uint32_t last_tick = 0, last_sort = 0;
    bool running = true;
    while(running) {
        InputEvent event;
        if(furi_message_queue_get(app->input, &event, 20) == FuriStatusOk) {
            furi_mutex_acquire(app->mutex, FuriWaitForever);
            running = handle_input(app, &event);
            furi_mutex_release(app->mutex);
            view_port_update(app->view_port);
        }
        const uint32_t now = furi_get_tick();
        furi_mutex_acquire(app->mutex, FuriWaitForever);
        take_reports(app, now);
        if(now - last_sort >= SORT_MS) {
            last_sort = now;
            // your trackers keep their marks when they change address
            if(ble_mine_follow(&app->mine, &app->table, now)) ble_tools_mine_save(&app->mine);
            ble_seen_expire(&app->table, now, FORGET_MS, FORGET_MS);
            tracker_sort(app);
            scroll_to_selection(app);
            check_following(app, now);
        }
        if(now - last_tick >= TICK_MS) {
            last_tick = now;
            view_port_update(app->view_port);
        }
        const uint32_t interval = ble_meter_click_interval(&app->meter, now);
        const bool click = app->mode == ModeLocate && app->sound && interval &&
                           now - app->last_click >= interval;
        if(click) app->last_click = now;
        furi_mutex_release(app->mutex);
        if(click) notification_message(app->notifications, &ble_tools_click);
    }

    notification_message(app->notifications, &sequence_reset_red);
    ble_tools_scan_free(&app->scan);
    gui_remove_view_port(app->gui, app->view_port);
    view_port_free(app->view_port);
    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_NOTIFICATION);
    furi_message_queue_free(app->input);
    furi_mutex_free(app->mutex);
    free(app);
    return 0;
}
