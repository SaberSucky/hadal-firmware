#include "ble_tools.h"

#include <gui/elements.h>
#include <storage/storage.h>
#include <stdio.h>

#define REPORT_QUEUE_LEN 48U
// Leave the radio a little time for the Flipper's own advertising
#define SCAN_INTERVAL_MS 100U
#define SCAN_WINDOW_MS   90U
// Signal strengths drawn as empty and full
#define RSSI_FLOOR (-100)
#define RSSI_CEIL  (-35)

static void scan_callback(const GapScannerReport* report, void* context) {
    // BLE event thread: copy and hand over, nothing else
    BleToolsScan* scan = context;
    if(furi_message_queue_put(scan->reports, report, 0) != FuriStatusOk) scan->dropped++;
}

bool ble_tools_scan_start(BleToolsScan* scan, bool active) {
    if(!scan->reports) scan->reports = furi_message_queue_alloc(REPORT_QUEUE_LEN, sizeof(GapScannerReport));
    ble_tools_scan_stop(scan);
    if(!furi_hal_bt_scanner_is_supported()) return false;
    const GapScannerConfig config = {
        .interval_ms = SCAN_INTERVAL_MS,
        .window_ms = SCAN_WINDOW_MS,
        .active = active,
    };
    scan->scanning = furi_hal_bt_scanner_start(&config, scan_callback, scan);
    return scan->scanning;
}

void ble_tools_scan_stop(BleToolsScan* scan) {
    if(scan->scanning) furi_hal_bt_scanner_stop();
    scan->scanning = false;
}

void ble_tools_scan_free(BleToolsScan* scan) {
    ble_tools_scan_stop(scan);
    if(scan->reports) furi_message_queue_free(scan->reports);
    scan->reports = NULL;
}

bool ble_tools_scan_next(BleToolsScan* scan, GapScannerReport* report) {
    return scan->reports && furi_message_queue_get(scan->reports, report, 0) == FuriStatusOk;
}

void ble_tools_draw_unsupported(Canvas* canvas) {
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 12, AlignCenter, AlignTop, "Cannot scan");
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 64, 30, AlignCenter, AlignTop, "Needs Hadal's BLE Full");
    canvas_draw_str_aligned(canvas, 64, 41, AlignCenter, AlignTop, "radio stack");
}

void ble_tools_draw_fit(Canvas* canvas, int32_t x, int32_t y, const char* text, size_t width) {
    FuriString* fitted = furi_string_alloc_set_str(text);
    elements_string_fit_width(canvas, fitted, width);
    canvas_draw_str(canvas, x, y, furi_string_get_cstr(fitted));
    furi_string_free(fitted);
}

void ble_tools_draw_sound_hint(Canvas* canvas, bool sound) {
    // in the corner the graph leaves free, clear of the screen's edges
    elements_hints(canvas, 2, 61, AlignLeft, sound ? "ok=Mute" : "ok=Sound");
}

void ble_tools_check_paired(BleSeen* device) {
    if(device->traits & BLE_TRAIT_CHECKED) return;
    device->traits |= BLE_TRAIT_CHECKED;
    uint8_t identity[BLE_SEEN_ADDR_SIZE];
    if(furi_hal_bt_resolve_address(device->addr, identity)) device->traits |= BLE_TRAIT_PAIRED;
}

void ble_tools_recognise(const BleMine* mine, BleSeen* device) {
    ble_tools_check_paired(device);
    if(!device->mine) ble_mine_recognise(mine, device);
}

void ble_tools_label(const BleSeen* device, char* out, size_t size) {
    const char* model = device->model ? ble_model_name(device->model) : NULL;
    if(device->traits & BLE_TRAIT_PAIRED) {
        // the only devices that pair with a Flipper are phones (and computers) with its app
        snprintf(out, size, "%s", device->name[0] ? device->name : "My phone");
    } else if(model) {
        snprintf(out, size, "%s", model);
    } else if(device->name[0]) {
        snprintf(out, size, "%s", device->name);
    } else if(device->kind != BleKindUnknown) {
        snprintf(out, size, "%s", ble_kind_name(device->kind));
    } else {
        snprintf(
            out, size, "%02X:%02X:%02X", device->addr[2], device->addr[1], device->addr[0]);
    }
}

/* ---- meter ---- */

const NotificationSequence ble_tools_click = {
    &message_note_c7,
    &message_delay_10,
    &message_sound_off,
    NULL,
};

void ble_meter_reset(BleMeter* meter) {
    memset(meter, 0, sizeof(BleMeter));
    meter->best = RSSI_FLOOR;
}

void ble_meter_add(BleMeter* meter, int8_t rssi, uint32_t now) {
    if(rssi == BLE_SEEN_RSSI_INVALID) return;
    if(meter->count < BLE_METER_HISTORY) {
        meter->history[meter->count++] = rssi;
    } else {
        memmove(meter->history, meter->history + 1, BLE_METER_HISTORY - 1);
        meter->history[BLE_METER_HISTORY - 1] = rssi;
    }
    meter->avg_x8 = meter->count == 1 ? rssi * 8 : meter->avg_x8 + (rssi * 8 - meter->avg_x8) / 4;
    if(rssi > meter->best) meter->best = rssi;
    meter->last_heard = now;
}

static int meter_average(const BleMeter* meter, size_t from_end, size_t span) {
    int sum = 0, n = 0;
    for(size_t i = 0; i < span && from_end + i < meter->count; i++) {
        sum += meter->history[meter->count - 1 - from_end - i];
        n++;
    }
    return n ? sum / n : RSSI_FLOOR;
}

static int meter_level(int rssi, int width) {
    int level = (rssi - RSSI_FLOOR) * width / (RSSI_CEIL - RSSI_FLOOR);
    return level < 0 ? 0 : (level > width ? width : level);
}

void ble_meter_draw(Canvas* canvas, const BleMeter* meter, const char* title, uint32_t now) {
    char text[32];
    canvas_set_font(canvas, FontPrimary);
    FuriString* fitted = furi_string_alloc_set_str(title ? title : "");
    elements_string_fit_width(canvas, fitted, 122);
    // a clear row over the capitals and under the descenders, then the rule
    canvas_draw_str_aligned(canvas, 64, 9, AlignCenter, AlignBottom, furi_string_get_cstr(fitted));
    furi_string_free(fitted);
    canvas_draw_line(canvas, 0, 12, 127, 12);

    const uint32_t silent_ms = now - meter->last_heard;
    if(!meter->count) {
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(canvas, 64, 30, AlignCenter, AlignTop, "Listening...");
        return;
    }

    // the signal as a percentage: easier to follow than dBm while walking around
    const int rssi = meter->avg_x8 / 8;
    snprintf(text, sizeof(text), "%d", meter_level(rssi, 100));
    canvas_set_font(canvas, FontBigNumbers);
    canvas_draw_str(canvas, 2, 33, text);
    const int number_width = canvas_string_width(canvas, text);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 4 + number_width, 33, "%");

    // how near, and which way it is going
    const char* distance = rssi > -50 ? "Very close" : rssi > -65 ? "Close" : rssi > -80 ? "Nearby" :
                                                                                         "Far";
    const int recent = meter_average(meter, 0, 5), before = meter_average(meter, 5, 5);
    const char* trend = meter->count < 10 ? "" : recent - before >= 3 ? "Warmer" :
                                                 before - recent >= 3 ? "Colder" :
                                                                        "Steady";
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 125, 14, AlignRight, AlignTop, distance);
    canvas_set_font(canvas, FontSecondary);
    if(silent_ms > 5000) {
        snprintf(text, sizeof(text), "unheard %lus", (unsigned long)(silent_ms / 1000));
        canvas_draw_str_aligned(canvas, 125, 25, AlignRight, AlignTop, text);
    } else {
        canvas_draw_str_aligned(canvas, 125, 25, AlignRight, AlignTop, trend);
    }

    // strength bar, with a tick at the best so far
    // (two clear columns from the screen's edges)
    canvas_draw_frame(canvas, 2, 37, 124, 7);
    canvas_draw_box(canvas, 3, 38, meter_level(rssi, 122), 5);
    const int best = 3 + meter_level(meter->best, 122);
    canvas_draw_line(canvas, best, 35, best, 45);

    // the last readings, oldest on the left; the corner left of them is the app's
    const size_t shown = meter->count < BLE_METER_GRAPH_WIDTH ? meter->count : BLE_METER_GRAPH_WIDTH;
    for(size_t i = 0; i < shown; i++) {
        const int h = meter_level(meter->history[meter->count - shown + i], 16);
        const int x = 126 - (int)shown + (int)i;
        if(h) canvas_draw_line(canvas, x, 62, x, 62 - h);
    }
}

uint32_t ble_meter_click_interval(const BleMeter* meter, uint32_t now) {
    if(!meter->count || now - meter->last_heard > 5000) return 0;
    // -100 dBm: a click a second; -35 dBm and stronger: ten a second
    const int level = meter_level(meter->avg_x8 / 8, 90);
    return 1000 - (uint32_t)level * 10;
}

/* ---- the user's own devices ---- */

#define MINE_DIR      EXT_PATH("apps_data/ble_tools")
#define MINE_FILE_MAX 2048U // 32 lines of at most 41 characters

void ble_tools_mine_load(BleMine* mine) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    char* text = malloc(MINE_FILE_MAX);
    size_t size = 0;
    if(storage_file_open(file, BLE_TOOLS_MINE_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        size = storage_file_read(file, text, MINE_FILE_MAX);
    }
    ble_mine_parse(mine, text, size);
    free(text);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
}

void ble_tools_mine_save(BleMine* mine) {
    if(!mine->changed) return;
    char* text = malloc(MINE_FILE_MAX);
    const size_t size = ble_mine_format(mine, text, MINE_FILE_MAX);
    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_simply_mkdir(storage, EXT_PATH("apps_data"));
    storage_simply_mkdir(storage, MINE_DIR);
    File* file = storage_file_alloc(storage);
    if(storage_file_open(file, BLE_TOOLS_MINE_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS) &&
       storage_file_write(file, text, size) == size) {
        mine->changed = false;
    }
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
    free(text);
}

void ble_tools_draw_mine_mark(Canvas* canvas, int32_t x, int32_t y) {
    // a tick, 7 by 5, in the middle of the capitals' height
    static const uint8_t tick[] = {0x40, 0x60, 0x31, 0x1B, 0x0E};
    canvas_set_bitmap_mode(canvas, true);
    canvas_draw_xbm(canvas, x, y - 6, 7, 5, tick);
    canvas_set_bitmap_mode(canvas, false);
}
