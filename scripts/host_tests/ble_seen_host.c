/* Host driver for applications/system/ble_tools/ble_seen.c.
 *
 * stdin lines:
 *   H <addr 12 hex> <rssi> <ms> <advertisement hex>   one advertisement heard
 *   E <ms> <timeout> <tracker timeout>                 expire
 *   ?                                                  print every device:
 *        <addr> <kind name> separated=<0|1> name=<name> rssi=<avg> with_you=<ms at last H>
 *   F <ms> <threshold>                                 print the addresses that are following
 *   C <ms> <window>                                    print the count heard in the window
 * The user's own devices (ble_mine.c):
 *   L <text>                                           a line of the mine file (read with P)
 *   P                                                  read the lines given so far
 *   M <addr 12 hex> <0|1> <label>                      mark a device in the table, or unmark it
 *   T <ms>                                             follow address changes: print how many moved
 *   W                                                  print the mine file, then "."
 *   Q                                                  print every device: <addr> mine=<0|1>, then "."
 *   (a device heard for the first time is recognised from the list)
 * argv: [capacity]   or   fuzz N SEED   or   fuzzmine N SEED
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ble_seen.h"
#include "ble_mine.h"

static int hex_bytes(const char* hex, uint8_t* out, size_t max) {
    size_t n = 0;
    while(hex[0] && hex[1] && n < max) {
        unsigned v;
        if(sscanf(hex, "%2x", &v) != 1) break;
        out[n++] = (uint8_t)v;
        hex += 2;
    }
    return (int)n;
}

static unsigned rng_state;
static unsigned rng(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

int main(int argc, char** argv) {
    if(argc == 4 && strcmp(argv[1], "fuzzmine") == 0) {
        // random text read as a mine file, written back and read again: the same list
        rng_state = (unsigned)atoi(argv[3]) | 1;
        static const char alphabet[] = "0123456789ABCDEFabcdef: \n#xyz\r\t";
        char text[1024], out[2048], again[2048];
        static BleMine mine, second;
        for(int n = 0; n < atoi(argv[2]); n++) {
            const size_t len = rng() % sizeof(text);
            for(size_t i = 0; i < len; i++) {
                text[i] = (rng() % 4) ? alphabet[rng() % (sizeof(alphabet) - 1)] : (char)rng();
            }
            ble_mine_parse(&mine, text, len);
            if(mine.count > BLE_MINE_MAX) abort();
            const size_t written = ble_mine_format(&mine, out, sizeof(out));
            if(written >= sizeof(out) || strlen(out) != written) abort();
            ble_mine_parse(&second, out, written);
            ble_mine_format(&second, again, sizeof(again));
            if(strcmp(out, again) != 0) abort();
        }
        printf("ok\n");
        return 0;
    }
    if(argc == 4 && strcmp(argv[1], "fuzz") == 0) {
        rng_state = (unsigned)atoi(argv[3]) | 1;
        BleSeen devices[16];
        BleSeenTable table = {devices, 16, 0};
        uint8_t data[31], addr[6];
        for(int n = 0; n < atoi(argv[2]); n++) {
            for(int i = 0; i < 6; i++) addr[i] = (uint8_t)(rng() % 24);
            uint8_t len = (uint8_t)(rng() % 32);
            for(int i = 0; i < len; i++) data[i] = (uint8_t)rng();
            if(rng() % 3 == 0 && len > 4) data[0] = (uint8_t)(rng() % len);
            BleSeen* d = ble_seen_update(&table, addr, (int8_t)(rng() % 120 - 110), data, len, (uint32_t)n * 37);
            if(!d || d->kind >= BleKindCount || strlen(d->name) >= BLE_SEEN_NAME_SIZE) abort();
            if(table.count > table.capacity) abort();
            if(n % 50 == 0) ble_seen_expire(&table, (uint32_t)n * 37, 500, 2000);
            size_t order[16];
            ble_seen_sort_by_signal(&table, order);
            uint16_t by_kind[BleKindCount];
            if(ble_seen_count_recent(&table, (uint32_t)n * 37, 1000, by_kind) > table.count) abort();
        }
        printf("ok\n");
        return 0;
    }

    static BleSeen devices[256];
    BleSeenTable table = {devices, argc > 1 ? (size_t)atoi(argv[1]) : 64, 0};
    static BleMine mine;
    static char mine_text[4096];
    size_t mine_length = 0;
    char line[512], hex[256];
    uint8_t addr[6], data[64];
    while(fgets(line, sizeof(line), stdin)) {
        int rssi;
        unsigned long ms, a, b;
        char adv[256] = "";
        if(line[0] == 'H' && sscanf(line + 2, "%15s %d %lu %255s", hex, &rssi, &ms, adv) >= 3) {
            hex_bytes(hex, addr, 6);
            const int len = hex_bytes(adv, data, sizeof(data));
            BleSeen* d = ble_seen_update(&table, addr, (int8_t)rssi, data, (uint8_t)len, (uint32_t)ms);
            if(d && d->count == 1) ble_mine_recognise(&mine, d);
        } else if(line[0] == 'L' && line[1] == ' ') {
            const size_t n = strlen(line + 2);
            if(mine_length + n < sizeof(mine_text)) {
                memcpy(mine_text + mine_length, line + 2, n);
                mine_length += n;
            }
        } else if(line[0] == 'P') {
            ble_mine_parse(&mine, mine_text, mine_length);
        } else if(line[0] == 'M' && sscanf(line + 2, "%15s %d", hex, &rssi) == 2) {
            hex_bytes(hex, addr, 6);
            const char* label = strchr(strchr(line + 2, ' ') + 1, ' ');
            char name[64] = "";
            if(label) sscanf(label + 1, "%63[^\n]", name);
            for(size_t i = 0; i < table.count; i++) {
                if(memcmp(table.devices[i].addr, addr, 6) == 0) ble_mine_set(&mine, &table.devices[i], rssi != 0, name);
            }
        } else if(line[0] == 'T' && sscanf(line + 2, "%lu", &ms) == 1) {
            printf("%u\n", (unsigned)ble_mine_follow(&mine, &table, (uint32_t)ms));
        } else if(line[0] == 'W') {
            static char out[4096];
            ble_mine_format(&mine, out, sizeof(out));
            printf("%s.\n", out);
        } else if(line[0] == 'Q') {
            for(size_t i = 0; i < table.count; i++) {
                const BleSeen* d = &table.devices[i];
                printf(
                    "%02x%02x%02x%02x%02x%02x mine=%d\n",
                    d->addr[0], d->addr[1], d->addr[2], d->addr[3], d->addr[4], d->addr[5], d->mine);
            }
            printf(".\n");
        } else if(line[0] == 'E' && sscanf(line + 2, "%lu %lu %lu", &ms, &a, &b) == 3) {
            ble_seen_expire(&table, (uint32_t)ms, (uint32_t)a, (uint32_t)b);
        } else if(line[0] == '?') {
            for(size_t i = 0; i < table.count; i++) {
                const BleSeen* d = &table.devices[i];
                printf(
                    "%02x%02x%02x%02x%02x%02x %s separated=%d name=%s rssi=%d\n",
                    d->addr[0], d->addr[1], d->addr[2], d->addr[3], d->addr[4], d->addr[5],
                    ble_kind_name(d->kind), d->separated, d->name, d->rssi_avg_x8 / 8);
            }
            printf(".\n");
        } else if(line[0] == 'F' && sscanf(line + 2, "%lu %lu", &ms, &a) == 2) {
            for(size_t i = 0; i < table.count; i++) {
                if(ble_seen_is_following(&table.devices[i], (uint32_t)ms, (uint32_t)a)) {
                    printf("%02x%02x%02x%02x%02x%02x\n", table.devices[i].addr[0], table.devices[i].addr[1],
                           table.devices[i].addr[2], table.devices[i].addr[3], table.devices[i].addr[4],
                           table.devices[i].addr[5]);
                }
            }
            printf(".\n");
        } else if(line[0] == 'C' && sscanf(line + 2, "%lu %lu", &ms, &a) == 2) {
            printf("%u\n", (unsigned)ble_seen_count_recent(&table, (uint32_t)ms, (uint32_t)a, NULL));
        }
    }
    return 0;
}
