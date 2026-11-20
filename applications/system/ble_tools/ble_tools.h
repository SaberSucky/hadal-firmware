/**
 * @file ble_tools.h
 * @brief What Tracker Detector, Find My Stuff and Crowd Meter share: scanning, and a signal meter.
 */
#pragma once

#include <furi.h>
#include <furi_hal_bt.h>
#include <gui/gui.h>
#include <notification/notification_messages.h>

#include "ble_seen.h"
#include "ble_mine.h"

/* ---- scanning ---- */

typedef struct {
    FuriMessageQueue* reports; /**< GapScannerReport, filled on the BLE thread */
    uint32_t dropped;
    bool scanning;
} BleToolsScan;

/** false when the radio stack cannot scan (the Light stack) or scanning would not start */
bool ble_tools_scan_start(BleToolsScan* scan, bool active);
void ble_tools_scan_stop(BleToolsScan* scan);
void ble_tools_scan_free(BleToolsScan* scan);
/** Take one queued report, if any */
bool ble_tools_scan_next(BleToolsScan* scan, GapScannerReport* report);

/** The screen for a Flipper without the BLE Full stack */
void ble_tools_draw_unsupported(Canvas* canvas);

/* ---- signal meter: hotter / colder, for finding one device ---- */

#define BLE_METER_HISTORY     100U
#define BLE_METER_GRAPH_WIDTH 84U

typedef struct {
    int8_t history[BLE_METER_HISTORY]; /**< newest last */
    uint8_t count;
    int16_t avg_x8;
    int8_t best;
    uint32_t last_heard;
} BleMeter;

void ble_meter_reset(BleMeter* meter);
void ble_meter_add(BleMeter* meter, int8_t rssi, uint32_t now);
/**
 * The meter, full screen: title on top (may be NULL), the reading under it. The bottom left corner
 * (x < 128 - BLE_METER_GRAPH_WIDTH, y > 46) is left for the app, for a hint
 */
void ble_meter_draw(Canvas* canvas, const BleMeter* meter, const char* title, uint32_t now);
/** Time between clicks: shorter the stronger the signal; 0 when nothing heard lately */
uint32_t ble_meter_click_interval(const BleMeter* meter, uint32_t now);

/** A short click for the meter */
extern const NotificationSequence ble_tools_click;

/** Text cut to width pixels (with "...") in the current font, baseline at y */
void ble_tools_draw_fit(Canvas* canvas, int32_t x, int32_t y, const char* text, size_t width);

/** The meter's corner hint: what OK does to the clicks */
void ble_tools_draw_sound_hint(Canvas* canvas, bool sound);

/** "Find My tag", a device's name, or its address */
void ble_tools_label(const BleSeen* device, char* out, size_t size);

/* ---- the user's own devices (ble_mine.h), on the SD card ---- */

#define BLE_TOOLS_MINE_PATH EXT_PATH("apps_data/ble_tools/mine.txt")

/** Read the user's devices; an empty list when there is no file */
void ble_tools_mine_load(BleMine* mine);
/** Write them back, if they changed since */
void ble_tools_mine_save(BleMine* mine);
/** Width of the mark drawn before one of the user's devices, its gap included */
#define BLE_TOOLS_MINE_MARK_W 10
/** A tick before one of the user's devices: x its left, y the text's baseline */
void ble_tools_draw_mine_mark(Canvas* canvas, int32_t x, int32_t y);
