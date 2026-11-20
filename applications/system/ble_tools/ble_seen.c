#include "ble_seen.h"

#include <string.h>

#define APPLE_COMPANY        0x004CU
#define MICROSOFT_COMPANY    0x0006U
#define SAMSUNG_COMPANY      0x0075U
#define APPLE_FIND_MY        0x12U
#define APPLE_FIND_MY_AWAY   0x19U /* payload length of the "away from owner" form */
#define APPLE_FIND_MY_NEAR   0x02U /* payload length of the "owner nearby" form */
/* The Find My status byte says what sent it, in bits 4-5: every Apple device on the network
 * sends the same advertisement, AirPods and iPhones too, not only tags */
#define FIND_MY_TYPE(status) (((status) >> 4) & 0x03U)
#define FIND_MY_APPLE        0U /* an iPhone, iPad, Mac or Watch */
#define FIND_MY_AIRTAG       1U
#define FIND_MY_ACCESSORY    2U /* a Find My network accessory from another maker */
#define FIND_MY_AIRPODS      3U
#define APPLE_AIRPODS        0x07U
#define UUID_SMARTTAG        0xFD5AU
#define UUID_TILE            0xFEEDU
#define UUID_TILE_OLD        0xFEECU
#define UUID_EDDYSTONE       0xFEAAU
#define GOOGLE_FMDN          0x40U
#define GOOGLE_FMDN_UNWANTED 0x41U

static const char* const kind_names[BleKindCount] = {
    [BleKindUnknown] = "Unknown",
    [BleKindFindMy] = "Find My tag",
    [BleKindAirPods] = "AirPods",
    [BleKindApple] = "Apple device",
    [BleKindSmartTag] = "SmartTag",
    [BleKindTile] = "Tile",
    [BleKindGoogleFind] = "Google tag",
    [BleKindSamsung] = "Samsung device",
    [BleKindMicrosoft] = "Windows device",
    [BleKindNamed] = "Named device",
    [BleKindAirTag] = "AirTag",
};

const char* ble_kind_name(BleKind kind) {
    return kind < BleKindCount ? kind_names[kind] : kind_names[BleKindUnknown];
}

bool ble_kind_is_tracker(BleKind kind) {
    return kind == BleKindAirTag || kind == BleKindFindMy || kind == BleKindSmartTag ||
           kind == BleKindTile || kind == BleKindGoogleFind;
}

// A weaker hint never overrides a stronger one (a tracker stays a tracker)
static void set_kind(BleSeen* device, BleKind kind) {
    const BleKind current = device->kind;
    if(ble_kind_is_tracker(current) && !ble_kind_is_tracker(kind)) return;
    if(kind == BleKindApple && current == BleKindAirPods) return;
    if(kind == BleKindNamed && current != BleKindUnknown) return;
    device->kind = kind;
}

static void parse_service(BleSeen* device, uint16_t uuid, const uint8_t* value, uint8_t length) {
    if(uuid == UUID_SMARTTAG) {
        set_kind(device, BleKindSmartTag);
        device->separated = true; // it does not say
    } else if(uuid == UUID_TILE || uuid == UUID_TILE_OLD) {
        set_kind(device, BleKindTile);
        device->separated = true; // it does not say
    } else if(uuid == UUID_EDDYSTONE && length >= 1 &&
              (value[0] == GOOGLE_FMDN || value[0] == GOOGLE_FMDN_UNWANTED)) {
        set_kind(device, BleKindGoogleFind);
        device->separated = value[0] == GOOGLE_FMDN_UNWANTED;
    }
}

/* Apple's Find My advertisement after its type byte: its length, the status byte, then the key.
 * Only the two forms Apple sends, each whole, are taken: anything else is just an Apple device */
static void parse_find_my(BleSeen* device, const uint8_t* payload, uint8_t length) {
    const uint8_t form = payload[0];
    if((form != APPLE_FIND_MY_AWAY && form != APPLE_FIND_MY_NEAR) || length < 1 + form) {
        set_kind(device, BleKindApple);
        return;
    }
    const bool away = form == APPLE_FIND_MY_AWAY;
    switch(FIND_MY_TYPE(payload[1])) {
    case FIND_MY_AIRTAG:
        set_kind(device, BleKindAirTag);
        device->separated = away;
        break;
    case FIND_MY_ACCESSORY:
        set_kind(device, BleKindFindMy);
        device->separated = away;
        break;
    case FIND_MY_AIRPODS:
        // headphones on the network: findable, but not a tag
        set_kind(device, BleKindAirPods);
        break;
    default:
        set_kind(device, BleKindApple);
        break;
    }
}

void ble_seen_parse(BleSeen* device, const uint8_t* data, uint8_t length) {
    uint8_t pos = 0;
    while(data && pos + 1 < length) {
        const uint8_t field = data[pos];
        if(field == 0 || pos + 1 + field > length) break;
        const uint8_t type = data[pos + 1];
        const uint8_t* value = &data[pos + 2];
        const uint8_t value_length = field - 1;

        if((type == 0x08 || type == 0x09) && value_length > 0) {
            // shortened or complete local name, printable characters only
            size_t n = value_length < BLE_SEEN_NAME_SIZE - 1 ? value_length : BLE_SEEN_NAME_SIZE - 1;
            for(size_t i = 0; i < n; i++) {
                device->name[i] = (value[i] >= ' ' && value[i] < 127) ? (char)value[i] : '?';
            }
            device->name[n] = '\0';
            set_kind(device, BleKindNamed);
        } else if(type == 0xFF && value_length >= 2) {
            const uint16_t company = value[0] | (value[1] << 8);
            if(company == APPLE_COMPANY && value_length >= 5 && value[2] == APPLE_FIND_MY) {
                parse_find_my(device, value + 3, value_length - 3);
            } else if(company == APPLE_COMPANY && value_length >= 3 && value[2] == APPLE_AIRPODS) {
                set_kind(device, BleKindAirPods);
            } else if(company == APPLE_COMPANY) {
                set_kind(device, BleKindApple);
            } else if(company == SAMSUNG_COMPANY) {
                set_kind(device, BleKindSamsung);
            } else if(company == MICROSOFT_COMPANY) {
                set_kind(device, BleKindMicrosoft);
            }
        } else if(type == 0x16 && value_length >= 2) {
            parse_service(device, value[0] | (value[1] << 8), value + 2, value_length - 2);
        } else if((type == 0x02 || type == 0x03) && value_length >= 2) {
            for(uint8_t i = 0; i + 1 < value_length; i += 2) {
                const uint16_t uuid = value[i] | (value[i + 1] << 8);
                if(uuid == UUID_TILE || uuid == UUID_TILE_OLD || uuid == UUID_SMARTTAG) {
                    parse_service(device, uuid, NULL, 0);
                }
            }
        }
        pos += 1 + field;
    }
}

BleSeen* ble_seen_update(
    BleSeenTable* table,
    const uint8_t* addr,
    int8_t rssi,
    const uint8_t* data,
    uint8_t length,
    uint32_t now) {
    BleSeen* device = NULL;
    for(size_t i = 0; i < table->count && !device; i++) {
        if(memcmp(table->devices[i].addr, addr, BLE_SEEN_ADDR_SIZE) == 0) device = &table->devices[i];
    }
    if(!device) {
        if(!table->capacity) return NULL;
        size_t slot = table->count;
        if(slot >= table->capacity) {
            // full: the one unheard the longest makes room
            slot = 0;
            for(size_t i = 1; i < table->count; i++) {
                if(now - table->devices[i].last_seen > now - table->devices[slot].last_seen) slot = i;
            }
        } else {
            table->count++;
        }
        device = &table->devices[slot];
        memset(device, 0, sizeof(BleSeen));
        memcpy(device->addr, addr, BLE_SEEN_ADDR_SIZE);
        device->first_seen = now;
    }

    if(rssi != BLE_SEEN_RSSI_INVALID) {
        device->rssi = rssi;
        if(device->count == 0) {
            device->rssi_avg_x8 = (int16_t)(rssi * 8);
        } else {
            device->rssi_avg_x8 = (int16_t)(device->rssi_avg_x8 + (rssi * 8 - device->rssi_avg_x8) / 4);
        }
    }
    device->last_seen = now;
    device->count++;
    ble_seen_parse(device, data, length);
    return device;
}

void ble_seen_expire(BleSeenTable* table, uint32_t now, uint32_t timeout_ms, uint32_t tracker_timeout_ms) {
    size_t kept = 0;
    for(size_t i = 0; i < table->count; i++) {
        const BleSeen* device = &table->devices[i];
        const uint32_t limit = ble_kind_is_tracker(device->kind) ? tracker_timeout_ms : timeout_ms;
        if(now - device->last_seen <= limit) {
            if(kept != i) table->devices[kept] = table->devices[i];
            kept++;
        }
    }
    table->count = kept;
}

void ble_seen_sort_by_signal(const BleSeenTable* table, size_t* order) {
    for(size_t i = 0; i < table->count; i++) {
        order[i] = i;
    }
    for(size_t i = 1; i < table->count; i++) {
        const size_t value = order[i];
        size_t j = i;
        while(j > 0 && table->devices[order[j - 1]].rssi_avg_x8 < table->devices[value].rssi_avg_x8) {
            order[j] = order[j - 1];
            j--;
        }
        order[j] = value;
    }
}

size_t ble_seen_count_recent(
    const BleSeenTable* table,
    uint32_t now,
    uint32_t window_ms,
    uint16_t* by_kind) {
    if(by_kind) memset(by_kind, 0, BleKindCount * sizeof(uint16_t));
    size_t count = 0;
    for(size_t i = 0; i < table->count; i++) {
        const BleSeen* device = &table->devices[i];
        if(now - device->last_seen > window_ms) continue;
        count++;
        if(by_kind && device->kind < BleKindCount) by_kind[device->kind]++;
    }
    return count;
}

uint32_t ble_seen_with_you_ms(const BleSeen* device, uint32_t now) {
    return now - device->first_seen;
}

bool ble_seen_is_following(const BleSeen* device, uint32_t now, uint32_t threshold_ms) {
    return ble_kind_is_tracker(device->kind) && device->separated &&
           ble_seen_with_you_ms(device, now) >= threshold_ms;
}
