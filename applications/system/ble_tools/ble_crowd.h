/**
 * @file ble_crowd.h
 * @brief How many people are around, from the Bluetooth LE devices heard (Crowd Meter).
 *
 * Devices are not people: one carries a phone, earbuds and a watch, and the room is full of
 * trackers, beacons, televisions and computers nobody carries. So each device heard gets a role:
 * - a phone (or a tablet or laptop someone has with them): Apple's own messages (Nearby, Handoff,
 *   AirDrop...), Samsung's, Google Nearby, a phone's name
 * - something worn with a phone: AirPods and Beats, Google Fast Pair devices, a name such as
 *   "Galaxy Buds" or "Watch"
 * - a thing nobody carries: trackers, beacons, Windows computers, Apple TVs and HomePods, a lost
 *   Apple device away from its owner, a name such as "[TV]" or "Printer"
 * - unknown: everything else (not counted)
 * and people are the phones, and the worn devices beyond the phones (someone whose phone is quiet
 * but whose earbuds are not). The user's own devices are not counted, and a device seen under two
 * addresses in the window (they change them now and then) is counted once.
 *
 * No dependencies, for the host tests.
 */
#pragma once

#include "ble_seen.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BleRoleUnknown,
    BleRolePhone,
    BleRoleWearable,
    BleRoleThing,
    BleRoleCount,
} BleRole;

/** One device under two addresses: the second first heard within this long of the first falling
 * silent, with the same role and kind, within BLE_CROWD_SAME_DB of its signal */
#define BLE_CROWD_HANDOVER_MS (20U * 1000U)
#define BLE_CROWD_SAME_DB     (10)
/** ...and the first silent for this long at least: devices advertise every second or two */
#define BLE_CROWD_SILENT_MS (5U * 1000U)

typedef struct {
    uint16_t by_role[BleRoleCount]; /**< devices in each role, each counted once */
    uint16_t mine; /**< the user's own, not counted */
    uint16_t people; /**< the estimate */
    uint16_t people_near; /**< of them, those at least as strong as near_dbm */
} BleCrowd;

BleRole ble_role(const BleSeen* device);
const char* ble_role_name(BleRole role);

/** Count the devices heard in the last window_ms, and the people they stand for */
void ble_crowd_count(
    const BleSeenTable* table,
    uint32_t now,
    uint32_t window_ms,
    int near_dbm,
    BleCrowd* crowd);

#ifdef __cplusplus
}
#endif
