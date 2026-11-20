/* Advertisements for the BLE app previews */
#pragma once

#include "preview.h"
#include "../../applications/system/ble_tools/ble_seen.h"

static inline size_t adv_hex(const char* hex, uint8_t* out) {
    size_t n = 0;
    while(hex[0] && hex[1] && n < 31) {
        unsigned value;
        sscanf(hex, "%2x", &value);
        out[n++] = (uint8_t)value;
        hex += 2;
    }
    return n;
}

static inline size_t adv_name(const char* name, uint8_t* out) {
    const size_t length = strlen(name);
    out[0] = (uint8_t)(length + 1);
    out[1] = 0x09;
    memcpy(out + 2, name, length);
    return length + 2;
}

/* one device heard every second from `from` to `to` (ms), at about rssi */
static inline void hear(
    BleSeenTable* table,
    uint8_t id,
    const uint8_t* data,
    size_t length,
    int rssi,
    uint32_t from,
    uint32_t to) {
    const uint8_t addr[6] = {id, 0x42, 0x13, 0x37, 0xC0, 0xDE};
    for(uint32_t t = from; t <= to; t += 1000) {
        ble_seen_update(table, addr, (int8_t)(rssi + (int)((t / 1000 + id) % 5) - 2), data, (uint8_t)length, t);
    }
}

#define AIRTAG_AWAY "1EFF4C00121910ABABABABABABABABABABABABABABABABABABABABABAB0100"
#define AIRTAG_HOME "07FF4C0012020000"
#define TILE        "0303EDFE"
#define SMARTTAG    "03035AFD"
#define AIRPODS     "0BFF4C000700000000000000"
#define APPLE       "0AFF4C001005031C0E6F2B"
#define SAMSUNG     "05FF75000102"
#define WINDOWS     "1EFF0600010920026F7B5AD1D3E5C1C1F01D7E0A0C8B2B7BC3E2C9C7AD1D17"
