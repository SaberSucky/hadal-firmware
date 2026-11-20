/**
 * Find My Stuff: lists the Bluetooth LE devices around, strongest first, and walks you to one with
 * a hotter / colder meter that clicks faster as you get closer. For your own earbuds, watch,
 * tracker or phone. Needs the BLE Full radio stack.
 */
#include "ble_tools.h"

#include <input/input.h>
#include <stdio.h>

#define CAPACITY  96U
#define ROWS      4U
#define FORGET_MS (60U * 1000U)
#define SORT_MS   1000U /* resorting less often keeps the list still enough to read */
#define TICK_MS   250U

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
    Mode mode;
    size_t selected;
    size_t top;
    /** the list moves as signals change: the selection follows its device, not its row */
    uint8_t selected_addr[BLE_SEEN_ADDR_SIZE];
    bool has_selection;
    uint8_t locate_addr[BLE_SEEN_ADDR_SIZE];
    char locate_label[BLE_SEEN_NAME_SIZE];
    BleMeter meter;
    bool sound;
    uint32_t last_click;
} FindMyStuff;

static const BleSeen* device_at(const FindMyStuff* app, size_t row) {
    return row < app->table.count ? &app->devices[app->order[row]] : NULL;
}

static void select_row(FindMyStuff* app, size_t row) {
    const BleSeen* device = device_at(app, row);
    app->selected = row;
    app->has_selection = device != NULL;
    if(device) memcpy(app->selected_addr, device->addr, BLE_SEEN_ADDR_SIZE);
}

static void scroll_to_selection(FindMyStuff* app) {
    if(app->selected < app->top) app->top = app->selected;
    if(app->selected >= app->top + ROWS) app->top = app->selected - ROWS + 1;
    if(app->top + ROWS > app->table.count) {
        app->top = app->table.count > ROWS ? app->table.count - ROWS : 0;
    }
}

static void resort(FindMyStuff* app) {
    ble_seen_sort_by_signal(&app->table, app->order);
    size_t row = app->selected;
    if(app->has_selection) {
        for(size_t i = 0; i < app->table.count; i++) {
            if(memcmp(device_at(app, i)->addr, app->selected_addr, BLE_SEEN_ADDR_SIZE) == 0) {
                row = i;
                break;
            }
        }
    }
    if(row >= app->table.count) row = app->table.count ? app->table.count - 1 : 0;
    select_row(app, row);
    scroll_to_selection(app);
}

static int bars(int8_t rssi) {
    // 0..4 bars, like a phone's
    return rssi >= -55 ? 4 : rssi >= -67 ? 3 : rssi >= -78 ? 2 : rssi >= -90 ? 1 : 0;
}

static void draw_list(Canvas* canvas, FindMyStuff* app) {
    char text[BLE_SEEN_NAME_SIZE];
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 9, "Find My Stuff"); // its descender a row clear of the rule
    canvas_set_font(canvas, FontSecondary);
    snprintf(text, sizeof(text), "%u near", (unsigned)app->table.count);
    canvas_draw_str_aligned(canvas, 125, 9, AlignRight, AlignBottom, text);
    canvas_draw_line(canvas, 0, 12, 127, 12);

    if(!app->table.count) {
        canvas_draw_str_aligned(canvas, 64, 24, AlignCenter, AlignTop, "Listening...");
        canvas_draw_str_aligned(canvas, 64, 38, AlignCenter, AlignTop, "Wake the device up to");
        canvas_draw_str_aligned(canvas, 64, 48, AlignCenter, AlignTop, "make it easier to hear");
        return;
    }

    for(size_t row = 0; row < ROWS; row++) {
        const BleSeen* device = device_at(app, app->top + row);
        if(!device) break;
        const int y = 14 + (int)row * 12;
        if(app->top + row == app->selected) {
            canvas_draw_rbox(canvas, 0, y, 124, 12, 2);
            canvas_set_color(canvas, ColorWhite);
        }
        ble_tools_label(device, text, sizeof(text));
        ble_tools_draw_fit(canvas, 3, y + 9, text, 99);
        // signal bars on the right
        const int level = bars((int8_t)(device->rssi_avg_x8 / 8));
        for(int bar = 0; bar < 4; bar++) {
            const int h = 2 + bar * 2;
            const int x = 106 + bar * 4;
            if(bar < level) {
                canvas_draw_box(canvas, x, y + 10 - h, 3, h);
            } else {
                canvas_draw_frame(canvas, x, y + 10 - h, 3, h);
            }
        }
        canvas_set_color(canvas, ColorBlack);
    }
    if(app->table.count > ROWS) {
        const int height = 48 * ROWS / (int)app->table.count;
        const int y = 14 + (48 - height) * (int)app->top / (int)(app->table.count - ROWS);
        canvas_draw_box(canvas, 126, y, 2, height);
    }
}

static void draw_callback(Canvas* canvas, void* context) {
    FindMyStuff* app = context;
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    canvas_clear(canvas);
    if(!app->supported) {
        ble_tools_draw_unsupported(canvas);
    } else if(app->mode == ModeLocate) {
        ble_meter_draw(canvas, &app->meter, app->locate_label, furi_get_tick());
        ble_tools_draw_sound_hint(canvas, app->sound);
    } else {
        draw_list(canvas, app);
    }
    furi_mutex_release(app->mutex);
}

static void input_callback(InputEvent* event, void* context) {
    FindMyStuff* app = context;
    furi_message_queue_put(app->input, event, FuriWaitForever);
}

// Returns false to quit
static bool handle_input(FindMyStuff* app, const InputEvent* event) {
    if(event->type != InputTypeShort && event->type != InputTypeRepeat) return true;
    if(app->mode == ModeLocate) {
        if(event->key == InputKeyBack) app->mode = ModeList;
        if(event->key == InputKeyOk && event->type == InputTypeShort) app->sound = !app->sound;
        return true;
    }
    switch(event->key) {
    case InputKeyBack:
        return false;
    case InputKeyUp:
        if(app->selected) select_row(app, app->selected - 1);
        break;
    case InputKeyDown:
        if(app->selected + 1 < app->table.count) select_row(app, app->selected + 1);
        break;
    case InputKeyOk: {
        const BleSeen* device = device_at(app, app->selected);
        if(device && event->type == InputTypeShort) {
            memcpy(app->locate_addr, device->addr, BLE_SEEN_ADDR_SIZE);
            ble_tools_label(device, app->locate_label, sizeof(app->locate_label));
            ble_meter_reset(&app->meter);
            ble_meter_add(&app->meter, device->rssi, device->last_seen);
            app->sound = true;
            app->mode = ModeLocate;
        }
        break;
    }
    default:
        break;
    }
    scroll_to_selection(app);
    return true;
}

static void take_reports(FindMyStuff* app, uint32_t now) {
    GapScannerReport report;
    const size_t sorted = app->table.count;
    while(ble_tools_scan_next(&app->scan, &report)) {
        ble_seen_update(&app->table, report.address, report.rssi, report.data, report.data_len, now);
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

int32_t find_my_stuff_app(void* p) {
    UNUSED(p);
    FindMyStuff* app = malloc(sizeof(FindMyStuff));
    memset(app, 0, sizeof(FindMyStuff));
    app->table = (BleSeenTable){app->devices, CAPACITY, 0};
    app->input = furi_message_queue_alloc(8, sizeof(InputEvent));
    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);
    app->view_port = view_port_alloc();
    view_port_draw_callback_set(app->view_port, draw_callback, app);
    view_port_input_callback_set(app->view_port, input_callback, app);
    app->gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);

    // active: devices are asked their names, which is how you tell your earbuds from others
    app->supported = ble_tools_scan_start(&app->scan, true);

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
            ble_seen_expire(&app->table, now, FORGET_MS, FORGET_MS);
            resort(app);
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
