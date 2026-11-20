/**
 * @file ble_seen.h
 * @brief Bluetooth LE devices heard nearby: what they are, how strong, since when.
 *
 * Shared by Tracker Detector, Find My Stuff and Crowd Meter. No dependencies, for the host tests.
 *
 * Trackers say whether they are away from their owner, which is what tells a stranger's tag that
 * travels with you from one whose owner is walking beside you:
 * - Apple Find My (AirTag and other accessories): the long payload (25 bytes) is sent only when
 *   the accessory has been away from its owner for a while; near its owner it sends 2 bytes
 * - Google Find My Device network tags: frame 0x41 is the "unwanted tracking protection" mode
 * - Samsung SmartTag and Tile do not say: they count as away
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BLE_SEEN_ADDR_SIZE    (6U)
#define BLE_SEEN_NAME_SIZE    (20U)
#define BLE_SEEN_RSSI_INVALID (127)

typedef enum {
    BleKindUnknown,
    BleKindFindMy, /**< a Find My network accessory that is not an AirTag (Chipolo, ...) */
    BleKindAirPods,
    BleKindApple,
    BleKindSmartTag,
    BleKindTile,
    BleKindGoogleFind,
    BleKindSamsung,
    BleKindMicrosoft,
    BleKindNamed, /**< told its name, nothing else known */
    BleKindAirTag,
    BleKindCount,
} BleKind;

typedef struct {
    uint8_t addr[BLE_SEEN_ADDR_SIZE];
    int8_t rssi; /**< last, dBm */
    int16_t rssi_avg_x8; /**< moving average, dBm x 8 */
    uint32_t first_seen; /**< ms tick: since then heard with no gap longer than the expiry */
    uint32_t last_seen;
    uint32_t count;
    uint8_t kind; /**< BleKind */
    bool separated; /**< a tracker away from its owner (see above) */
    bool alerted; /**< for the app: the user was told about it */
    bool mine; /**< the user's own (ble_mine.h): never a warning */
    bool handed_over; /**< its mark moved on to its next address (ble_mine_follow()) */
    char name[BLE_SEEN_NAME_SIZE];
} BleSeen;

typedef struct {
    BleSeen* devices;
    size_t capacity;
    size_t count;
} BleSeenTable;

const char* ble_kind_name(BleKind kind);
bool ble_kind_is_tracker(BleKind kind);

/** Learn what a device is from one advertisement (raw AD structures) */
void ble_seen_parse(BleSeen* device, const uint8_t* data, uint8_t length);

/** One advertisement heard: the device's entry, added if new (the longest unheard one makes room) */
BleSeen* ble_seen_update(
    BleSeenTable* table,
    const uint8_t* addr,
    int8_t rssi,
    const uint8_t* data,
    uint8_t length,
    uint32_t now);

/** Forget devices not heard for timeout_ms (tracker_timeout_ms for trackers) */
void ble_seen_expire(BleSeenTable* table, uint32_t now, uint32_t timeout_ms, uint32_t tracker_timeout_ms);

/** Indices into the table, strongest signal first */
void ble_seen_sort_by_signal(const BleSeenTable* table, size_t* order);

/** Devices heard in the last window_ms; by_kind (BleKindCount entries) may be NULL */
size_t ble_seen_count_recent(
    const BleSeenTable* table,
    uint32_t now,
    uint32_t window_ms,
    uint16_t* by_kind);

/** How long a device has been heard without a long gap */
uint32_t ble_seen_with_you_ms(const BleSeen* device, uint32_t now);

/** A tracker away from its owner that has stayed with you for threshold_ms, and is not yours */
bool ble_seen_is_following(const BleSeen* device, uint32_t now, uint32_t threshold_ms);

#ifdef __cplusplus
}
#endif
