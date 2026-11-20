/**
 * @file ble_mine.h
 * @brief The user's own devices, marked as theirs in Tracker Detector or Find My Stuff: never a
 * warning, listed as theirs. Shared by both apps through one file on the SD card, a device a line:
 *
 *     AA:BB:CC:DD:EE:FF <kind> <label>
 *
 * Trackers, phones and earbuds change their address now and then so that they cannot be followed,
 * and a mark is kept by address. So while an app runs, a mark follows its device through such a
 * change (ble_mine_follow()): when a marked device falls silent and, within moments, exactly one
 * device of the same kind turns up where it was (as strong as it was), that is the same device
 * under its new address, and the mark moves to it. Between runs the mark stays with the last
 * address it knew; a tracker away from its owner keeps one address a day.
 *
 * No dependencies, for the host tests.
 */
#pragma once

#include "ble_seen.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BLE_MINE_MAX (32U)
/** A mark follows a device that fell silent this long ago at least... */
#define BLE_MINE_SILENT_MS (8U * 1000U)
/** ...to one first heard at most this long after it was last */
#define BLE_MINE_GAP_MS (20U * 1000U)
/** ...whose signal is within this many dB of its */
#define BLE_MINE_RSSI_DB (10)

typedef struct {
    uint8_t addr[BLE_SEEN_ADDR_SIZE];
    uint8_t kind; /**< BleKind, when it was marked */
    char label[BLE_SEEN_NAME_SIZE];
} BleMineEntry;

typedef struct {
    BleMineEntry entries[BLE_MINE_MAX];
    size_t count;
    bool changed; /**< since it was read: to be written back */
} BleMine;

/** Read the file's text into an empty list; lines that are not a device are skipped */
void ble_mine_parse(BleMine* mine, const char* text, size_t length);

/** The list as the file's text; returns its length, at most size - 1 (always terminated) */
size_t ble_mine_format(const BleMine* mine, char* out, size_t size);

/** The entry for an address, or NULL */
const BleMineEntry* ble_mine_find(const BleMine* mine, const uint8_t* addr);

/** Mark a device as mine (label: what to call it) or not; the device's flag is set to match. The
 * oldest mark makes room when the list is full */
void ble_mine_set(BleMine* mine, BleSeen* device, bool is_mine, const char* label);

/** Whether a device just heard is one of mine: sets its flag from the list */
void ble_mine_recognise(const BleMine* mine, BleSeen* device);

/** Move marks along with devices that changed their address (see above); call every second or
 * so. Returns how many moved */
size_t ble_mine_follow(BleMine* mine, BleSeenTable* table, uint32_t now);

#ifdef __cplusplus
}
#endif
