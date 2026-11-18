/**
 * BLE Scanner - lists nearby Bluetooth LE devices using the Flipper's own radio.
 * Requires the BLE "Full" radio stack (furi_hal_bt_scanner_is_supported()).
 */
#include <furi.h>
#include <furi_hal_bt.h>
#include <gui/gui.h>
#include <input/input.h>
#include <notification/notification_messages.h>
#include <stdlib.h>

#define TAG "BleScanner"

#define MAX_DEVICES       96
#define REPORT_QUEUE_LEN  48
#define DEVICE_TIMEOUT_MS 60000
#define ROWS_ON_SCREEN    4

typedef enum {
    DeviceKindUnknown,
    DeviceKindAppleFindMy, // AirTag or other Find My network accessory
    DeviceKindAppleAirPods,
    DeviceKindApple,
    DeviceKindSmartTag,
    DeviceKindTile,
    DeviceKindGoogleFind,
    DeviceKindMicrosoft,
    DeviceKindNamed,
} DeviceKind;

static const char* const device_kind_names[] = {
    [DeviceKindUnknown] = "Unknown",
    [DeviceKindAppleFindMy] = "Find My tag",
    [DeviceKindAppleAirPods] = "AirPods",
    [DeviceKindApple] = "Apple device",
    [DeviceKindSmartTag] = "SmartTag",
    [DeviceKindTile] = "Tile",
    [DeviceKindGoogleFind] = "Google Find tag",
    [DeviceKindMicrosoft] = "Windows device",
    [DeviceKindNamed] = "",
};

typedef struct {
    uint8_t addr[GAP_SCANNER_ADDR_SIZE];
    int8_t rssi;
    int16_t rssi_avg_x8; // exponential moving average, x8 fixed point
    uint32_t first_seen;
    uint32_t last_seen;
    uint32_t count;
    DeviceKind kind;
    char name[20];
} Device;

typedef struct {
    Gui* gui;
    ViewPort* view_port;
    FuriMessageQueue* input_queue;
    FuriMessageQueue* report_queue;
    FuriMutex* mutex;
    NotificationApp* notifications;

    Device devices[MAX_DEVICES];
    size_t device_count;
    size_t order[MAX_DEVICES]; // indices sorted by signal strength
    size_t scroll;
    uint32_t dropped_reports;
    bool supported;
    bool scanning;
    bool active_scan;
} BleScannerApp;

/* ---------- Advertisement parsing ---------- */

static void parse_advertisement(const GapScannerReport* report, Device* device) {
    const uint8_t* d = report->data;
    uint8_t len = report->data_len;
    uint8_t pos = 0;

    while(pos + 1 < len) {
        uint8_t field_len = d[pos];
        if(field_len == 0 || pos + 1 + field_len > len) break;
        uint8_t type = d[pos + 1];
        const uint8_t* val = &d[pos + 2];
        uint8_t val_len = field_len - 1;

        if((type == 0x08 || type == 0x09) && val_len > 0) {
            // Shortened / complete local name
            size_t n = MIN((size_t)val_len, sizeof(device->name) - 1);
            memcpy(device->name, val, n);
            device->name[n] = '\0';
            if(device->kind == DeviceKindUnknown) device->kind = DeviceKindNamed;
        } else if(type == 0xFF && val_len >= 2) {
            // Manufacturer specific data
            uint16_t company = val[0] | (val[1] << 8);
            if(company == 0x004C && val_len >= 3) {
                uint8_t apple_type = val[2];
                if(apple_type == 0x12) {
                    device->kind = DeviceKindAppleFindMy;
                } else if(apple_type == 0x07) {
                    device->kind = DeviceKindAppleAirPods;
                } else if(device->kind == DeviceKindUnknown) {
                    device->kind = DeviceKindApple;
                }
            } else if(company == 0x0006 && device->kind == DeviceKindUnknown) {
                device->kind = DeviceKindMicrosoft;
            }
        } else if((type == 0x16) && val_len >= 2) {
            // Service data, 16-bit UUID
            uint16_t uuid = val[0] | (val[1] << 8);
            if(uuid == 0xFD5A) {
                device->kind = DeviceKindSmartTag;
            } else if(uuid == 0xFEED || uuid == 0xFEEC) {
                device->kind = DeviceKindTile;
            } else if(uuid == 0xFEAA && val_len >= 3 && (val[2] == 0x40 || val[2] == 0x41)) {
                device->kind = DeviceKindGoogleFind;
            }
        } else if((type == 0x02 || type == 0x03) && val_len >= 2) {
            // 16-bit service UUID list
            for(uint8_t i = 0; i + 1 < val_len; i += 2) {
                uint16_t uuid = val[i] | (val[i + 1] << 8);
                if(uuid == 0xFEED || uuid == 0xFEEC) device->kind = DeviceKindTile;
            }
        }
        pos += 1 + field_len;
    }
}

/* ---------- Device table ---------- */

static Device* device_find_or_add(BleScannerApp* app, const uint8_t* addr, uint32_t now) {
    for(size_t i = 0; i < app->device_count; i++) {
        if(memcmp(app->devices[i].addr, addr, GAP_SCANNER_ADDR_SIZE) == 0) {
            return &app->devices[i];
        }
    }

    size_t slot = app->device_count;
    if(slot >= MAX_DEVICES) {
        // Table full: reuse the device that was seen longest ago
        slot = 0;
        for(size_t i = 1; i < MAX_DEVICES; i++) {
            if(app->devices[i].last_seen < app->devices[slot].last_seen) slot = i;
        }
    } else {
        app->device_count++;
    }

    Device* device = &app->devices[slot];
    memset(device, 0, sizeof(Device));
    memcpy(device->addr, addr, GAP_SCANNER_ADDR_SIZE);
    device->first_seen = now;
    return device;
}

static void device_table_update(BleScannerApp* app, const GapScannerReport* report) {
    uint32_t now = furi_get_tick();
    Device* device = device_find_or_add(app, report->address, now);

    if(report->rssi != GAP_SCANNER_RSSI_INVALID) {
        device->rssi = report->rssi;
        if(device->count == 0) {
            device->rssi_avg_x8 = report->rssi * 8;
        } else {
            device->rssi_avg_x8 += (report->rssi * 8 - device->rssi_avg_x8) / 4;
        }
    }
    device->last_seen = now;
    device->count++;
    parse_advertisement(report, device);
}

static void device_table_expire(BleScannerApp* app) {
    uint32_t now = furi_get_tick();
    size_t w = 0;
    for(size_t r = 0; r < app->device_count; r++) {
        if(now - app->devices[r].last_seen <= DEVICE_TIMEOUT_MS) {
            if(w != r) app->devices[w] = app->devices[r];
            w++;
        }
    }
    app->device_count = w;
}

static void device_table_sort(BleScannerApp* app) {
    for(size_t i = 0; i < app->device_count; i++)
        app->order[i] = i;
    // Insertion sort by smoothed RSSI, strongest first (table is small)
    for(size_t i = 1; i < app->device_count; i++) {
        size_t v = app->order[i];
        size_t j = i;
        while(j > 0 &&
              app->devices[app->order[j - 1]].rssi_avg_x8 < app->devices[v].rssi_avg_x8) {
            app->order[j] = app->order[j - 1];
            j--;
        }
        app->order[j] = v;
    }
    if(app->scroll + ROWS_ON_SCREEN > app->device_count) {
        app->scroll = app->device_count > ROWS_ON_SCREEN ? app->device_count - ROWS_ON_SCREEN : 0;
    }
}

/* ---------- Callbacks ---------- */

static void scanner_callback(const GapScannerReport* report, void* context) {
    // BLE event thread: only copy and enqueue
    BleScannerApp* app = context;
    if(furi_message_queue_put(app->report_queue, report, 0) != FuriStatusOk) {
        app->dropped_reports++;
    }
}

static void input_callback(InputEvent* event, void* context) {
    BleScannerApp* app = context;
    furi_message_queue_put(app->input_queue, event, FuriWaitForever);
}

static void draw_callback(Canvas* canvas, void* context) {
    BleScannerApp* app = context;
    furi_mutex_acquire(app->mutex, FuriWaitForever);

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);

    if(!app->supported) {
        canvas_draw_str_aligned(canvas, 64, 14, AlignCenter, AlignTop, "Scanning unsupported");
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(canvas, 64, 32, AlignCenter, AlignTop, "Needs the BLE Full");
        canvas_draw_str_aligned(canvas, 64, 43, AlignCenter, AlignTop, "radio stack firmware");
        furi_mutex_release(app->mutex);
        return;
    }

    char line[40];
    snprintf(
        line,
        sizeof(line),
        "%u devices %s",
        (unsigned)app->device_count,
        app->scanning ? (app->active_scan ? "(active)" : "(passive)") : "(stopped)");
    canvas_draw_str(canvas, 0, 10, line);
    canvas_draw_line(canvas, 0, 12, 127, 12);

    canvas_set_font(canvas, FontSecondary);
    uint32_t now = furi_get_tick();
    for(size_t row = 0; row < ROWS_ON_SCREEN; row++) {
        size_t idx = app->scroll + row;
        if(idx >= app->device_count) break;
        const Device* d = &app->devices[app->order[idx]];
        int y = 23 + row * 11;

        const char* label = d->kind == DeviceKindNamed || (d->name[0] && d->kind == DeviceKindUnknown) ?
                                d->name :
                                device_kind_names[d->kind];
        if(d->kind == DeviceKindUnknown && !d->name[0]) {
            snprintf(line, sizeof(line), "%02X:%02X:%02X", d->addr[2], d->addr[1], d->addr[0]);
            label = line;
        }
        canvas_draw_str(canvas, 0, y, label);

        // Signal bar: -100 dBm = empty, -40 dBm = full
        int rssi = d->rssi_avg_x8 / 8;
        int bar = (rssi + 100) * 24 / 60;
        if(bar < 1) bar = 1;
        if(bar > 24) bar = 24;
        canvas_draw_frame(canvas, 74, y - 7, 26, 7);
        canvas_draw_box(canvas, 75, y - 6, bar, 5);

        char rssi_str[8];
        snprintf(rssi_str, sizeof(rssi_str), "%d", rssi);
        canvas_draw_str_aligned(canvas, 127, y, AlignRight, AlignBottom, rssi_str);

        // Dim marker for devices not heard in the last 10 s
        if(now - d->last_seen > 10000) canvas_draw_str(canvas, 66, y, "~");
    }

    furi_mutex_release(app->mutex);
}

/* ---------- Scanning control ---------- */

static void ble_scanner_set_scanning(BleScannerApp* app, bool enable, bool active) {
    if(app->scanning) {
        furi_hal_bt_scanner_stop();
        app->scanning = false;
    }
    if(enable) {
        GapScannerConfig config = {
            .interval_ms = 100,
            .window_ms = 90, // leave the radio a little time for our own advertising
            .active = active,
        };
        app->scanning = furi_hal_bt_scanner_start(&config, scanner_callback, app);
        app->active_scan = active;
        if(!app->scanning) FURI_LOG_E(TAG, "Failed to start scanning");
    }
}

/* ---------- App ---------- */

int32_t ble_scanner_app(void* p) {
    UNUSED(p);
    BleScannerApp* app = malloc(sizeof(BleScannerApp));
    memset(app, 0, sizeof(BleScannerApp));

    app->input_queue = furi_message_queue_alloc(8, sizeof(InputEvent));
    app->report_queue = furi_message_queue_alloc(REPORT_QUEUE_LEN, sizeof(GapScannerReport));
    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);

    app->view_port = view_port_alloc();
    view_port_draw_callback_set(app->view_port, draw_callback, app);
    view_port_input_callback_set(app->view_port, input_callback, app);
    app->gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);

    app->supported = furi_hal_bt_scanner_is_supported();
    if(app->supported) ble_scanner_set_scanning(app, true, false);

    uint32_t last_redraw = 0;
    bool running = true;
    while(running) {
        InputEvent event;
        if(furi_message_queue_get(app->input_queue, &event, 50) == FuriStatusOk) {
            if(event.type == InputTypeShort || event.type == InputTypeRepeat) {
                furi_mutex_acquire(app->mutex, FuriWaitForever);
                if(event.key == InputKeyBack) {
                    running = false;
                } else if(event.key == InputKeyUp && app->scroll > 0) {
                    app->scroll--;
                } else if(event.key == InputKeyDown && app->scroll + ROWS_ON_SCREEN < app->device_count) {
                    app->scroll++;
                } else if(event.key == InputKeyOk && app->supported) {
                    // Toggle passive (silent) / active (asks devices for their names)
                    ble_scanner_set_scanning(app, true, !app->active_scan);
                }
                furi_mutex_release(app->mutex);
                view_port_update(app->view_port);
            }
        }

        GapScannerReport report;
        bool got_reports = false;
        furi_mutex_acquire(app->mutex, FuriWaitForever);
        while(furi_message_queue_get(app->report_queue, &report, 0) == FuriStatusOk) {
            device_table_update(app, &report);
            got_reports = true;
        }
        furi_mutex_release(app->mutex);
        if(got_reports) notification_message(app->notifications, &sequence_blink_blue_10);

        uint32_t now = furi_get_tick();
        if(now - last_redraw > 250) {
            furi_mutex_acquire(app->mutex, FuriWaitForever);
            device_table_expire(app);
            device_table_sort(app);
            furi_mutex_release(app->mutex);
            view_port_update(app->view_port);
            last_redraw = now;
        }
    }

    ble_scanner_set_scanning(app, false, false);

    gui_remove_view_port(app->gui, app->view_port);
    view_port_free(app->view_port);
    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_NOTIFICATION);
    furi_message_queue_free(app->input_queue);
    furi_message_queue_free(app->report_queue);
    furi_mutex_free(app->mutex);
    free(app);
    return 0;
}
