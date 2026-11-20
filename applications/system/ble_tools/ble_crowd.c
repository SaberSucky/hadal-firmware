#include "ble_crowd.h"

#include <string.h>
#include <strings.h>

/* Apple Continuity message types */
#define APPLE_AIRDROP        0x05U
#define APPLE_HOMEKIT        0x06U
#define APPLE_AIRPLAY_TARGET 0x09U
#define APPLE_AIRPLAY_SOURCE 0x0AU
#define APPLE_HANDOFF        0x0CU
#define APPLE_TETHER_TARGET  0x0DU
#define APPLE_TETHER_SOURCE  0x0EU
#define APPLE_NEARBY_ACTION  0x0FU
#define APPLE_NEARBY_INFO    0x10U
#define APPLE_FIND_MY        0x12U

static const char* const role_names[BleRoleCount] = {
    [BleRoleUnknown] = "Unknown",
    [BleRolePhone] = "Phones",
    [BleRoleWearable] = "Worn",
    [BleRoleThing] = "Things",
};

const char* ble_role_name(BleRole role) {
    return role < BleRoleCount ? role_names[role] : role_names[BleRoleUnknown];
}

static bool name_has(const char* name, const char* const* words) {
    for(; *words; words++) {
        const size_t length = strlen(*words);
        for(const char* p = name; *p; p++) {
            if(strncasecmp(p, *words, length) == 0) return true;
        }
    }
    return false;
}

static const char* const thing_words[] = {
    "[TV]", "BRAVIA", "Roku", "Fire TV", "Chromecast", "Printer", "DeskJet", "OfficeJet",
    "LaserJet", "Echo", "Sonos", "Nest", "Hue ", "Smart TV", NULL};
static const char* const worn_words[] = {
    "Buds", "AirPods", "Watch", "Band", "Headphone", "WH-", "WF-", "Jabra", "Bose", "JBL",
    "Beats", "Earbud", "Garmin", "Amazfit", "Fitbit", "Oura", "Soundcore", "Galaxy Fit", NULL};
static const char* const phone_words[] = {
    "iPhone", "Pixel", "Galaxy S", "Galaxy A", "Galaxy Z", "OnePlus", "Redmi", "Xiaomi", "Phone",
    NULL};

BleRole ble_role(const BleSeen* device) {
    const char* name = device->name;
    if(ble_kind_is_tracker(device->kind) || (device->traits & BLE_TRAIT_BEACON) ||
       device->kind == BleKindMicrosoft) {
        return BleRoleThing;
    }
    if(device->kind == BleKindApple) {
        switch(device->apple_type) {
        case APPLE_HOMEKIT:
        case APPLE_AIRPLAY_TARGET:
        case APPLE_FIND_MY: // an iPhone or Mac away from its owner: nobody is with it
            return BleRoleThing;
        default:
            break;
        }
    }
    if(name[0] && name_has(name, thing_words)) return BleRoleThing;
    // earbuds' names say "Pro" and "Phone" too: what is worn is told before what is a phone
    if(device->kind == BleKindAirPods || (device->traits & BLE_TRAIT_FAST_PAIR) ||
       (name[0] && name_has(name, worn_words))) {
        return BleRoleWearable;
    }
    if(device->kind == BleKindApple) {
        switch(device->apple_type) {
        case APPLE_AIRDROP:
        case APPLE_AIRPLAY_SOURCE:
        case APPLE_HANDOFF:
        case APPLE_TETHER_TARGET:
        case APPLE_TETHER_SOURCE:
        case APPLE_NEARBY_ACTION:
        case APPLE_NEARBY_INFO:
            return BleRolePhone;
        default:
            break;
        }
    }
    if(device->kind == BleKindSamsung || (device->traits & BLE_TRAIT_NEARBY) ||
       (name[0] && name_has(name, phone_words))) {
        return BleRolePhone;
    }
    return BleRoleUnknown;
}

static int rssi_of(const BleSeen* device) {
    return device->rssi_avg_x8 / 8;
}

/** Whether later is earlier under its next address */
static bool same_device(const BleSeen* earlier, const BleSeen* later, BleRole role, uint32_t now) {
    if(later->kind != earlier->kind || ble_role(later) != role) return false;
    if(now - earlier->last_seen < BLE_CROWD_SILENT_MS) return false; // still there: another one
    if(later->first_seen <= earlier->last_seen) return false; // heard together: two devices
    if(later->first_seen - earlier->last_seen > BLE_CROWD_HANDOVER_MS) return false;
    const int difference = rssi_of(later) - rssi_of(earlier);
    return difference <= BLE_CROWD_SAME_DB && difference >= -BLE_CROWD_SAME_DB;
}

#define CROWD_MAX 256U // devices looked at, at most: two bits each on the stack

#define BIT_SET(bits, i) ((bits)[(i) / 8] |= (uint8_t)(1U << ((i) % 8)))
#define BIT_GET(bits, i) (((bits)[(i) / 8] >> ((i) % 8)) & 1U)

static uint16_t estimate(uint16_t phones, uint16_t worn) {
    return phones + (worn > phones ? worn - phones : 0);
}

void ble_crowd_count(
    const BleSeenTable* table,
    uint32_t now,
    uint32_t window_ms,
    int near_dbm,
    BleCrowd* crowd) {
    memset(crowd, 0, sizeof(BleCrowd));
    uint16_t near[BleRoleCount] = {0};
    // which devices are an earlier address of one counted (each pairs with one at most)
    uint8_t merged[CROWD_MAX / 8] = {0};
    uint8_t absorbed[CROWD_MAX / 8] = {0};
    const size_t count = table->count < CROWD_MAX ? table->count : CROWD_MAX;

    for(size_t i = 0; i < count; i++) {
        const BleSeen* device = &table->devices[i];
        if(now - device->last_seen > window_ms || device->mine) continue;
        const BleRole role = ble_role(device);
        if(role == BleRoleUnknown) continue;
        // the closest later address of the same device, if it changed address in the window
        size_t best = count;
        uint32_t best_gap = UINT32_MAX;
        for(size_t j = 0; j < count; j++) {
            const BleSeen* later = &table->devices[j];
            if(j == i || BIT_GET(absorbed, j) || later->mine || now - later->last_seen > window_ms) {
                continue;
            }
            if(!same_device(device, later, role, now)) continue;
            const uint32_t gap = later->first_seen - device->last_seen;
            if(gap < best_gap) {
                best_gap = gap;
                best = j;
            }
        }
        if(best < count) {
            BIT_SET(merged, i);
            BIT_SET(absorbed, best);
        }
    }

    for(size_t i = 0; i < count; i++) {
        const BleSeen* device = &table->devices[i];
        if(now - device->last_seen > window_ms) continue;
        if(device->mine) {
            crowd->mine++;
            continue;
        }
        if(BIT_GET(merged, i)) continue;
        const BleRole role = ble_role(device);
        crowd->by_role[role]++;
        if(rssi_of(device) >= near_dbm) near[role]++;
    }
    crowd->people = estimate(crowd->by_role[BleRolePhone], crowd->by_role[BleRoleWearable]);
    crowd->people_near = estimate(near[BleRolePhone], near[BleRoleWearable]);
}
