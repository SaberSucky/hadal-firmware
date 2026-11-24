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
    bool active;
    bool fast;
} BleToolsScan;

/** How long a tool's loop waits for a key between looks at the reports: longer while the radio
 * listens a quarter of the time, as short as the locate clicks need while it listens nearly all */
#define BLE_TOOLS_WAIT_MS      100U
#define BLE_TOOLS_WAIT_FAST_MS 20U

/** false when the radio stack cannot scan (the Light stack) or scanning would not start.
 * fast: the radio listens nearly all the time (finding one device); otherwise a quarter of it,
 * which lists everything around within a second or two on a quarter of the battery */
bool ble_tools_scan_start(BleToolsScan* scan, bool active, bool fast);
/** Scanning on, fast or not: restarted only when that changes */
void ble_tools_scan_set_fast(BleToolsScan* scan, bool fast);
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

/** "My phone", "AirPods Pro 2", a device's name, "Find My tag", or its address */
void ble_tools_label(const BleSeen* device, char* out, size_t size);

/** Whether a device is paired with this Flipper (a phone with the Flipper app), whatever address
 * it uses now: asks the radio once a device, and sets BLE_TRAIT_PAIRED when it is */
void ble_tools_check_paired(BleSeen* device);

/** What every app does with a device it has just heard: whether it is paired, and whether it is
 * one of the user's own */
void ble_tools_recognise(const BleMine* mine, BleSeen* device);

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
