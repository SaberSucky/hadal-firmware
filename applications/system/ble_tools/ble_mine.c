#include "ble_mine.h"

#include <stdio.h>
#include <string.h>

static int hex_value(char c) {
    if(c >= '0' && c <= '9') return c - '0';
    if(c >= 'a' && c <= 'f') return c - 'a' + 10;
    if(c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/** "AA:BB:CC:DD:EE:FF" (17 characters) as the address, in the order the radio reports it */
static bool parse_addr(const char* text, size_t length, uint8_t* addr) {
    if(length < 17) return false;
    for(size_t i = 0; i < BLE_SEEN_ADDR_SIZE; i++) {
        const int hi = hex_value(text[i * 3]), lo = hex_value(text[i * 3 + 1]);
        if(hi < 0 || lo < 0 || (i < 5 && text[i * 3 + 2] != ':')) return false;
        // written most significant byte first, as people read addresses
        addr[BLE_SEEN_ADDR_SIZE - 1 - i] = (uint8_t)(hi << 4 | lo);
    }
    return true;
}

static void parse_line(BleMine* mine, const char* line, size_t length) {
    if(mine->count >= BLE_MINE_MAX) return;
    BleMineEntry entry;
    memset(&entry, 0, sizeof(entry));
    if(!parse_addr(line, length, entry.addr)) return;
    size_t pos = 17;
    if(pos >= length || line[pos] != ' ') return;
    pos++;
    unsigned kind = 0;
    while(pos < length && line[pos] >= '0' && line[pos] <= '9') {
        kind = kind * 10 + (unsigned)(line[pos] - '0');
        if(kind >= BleKindCount) return;
        pos++;
    }
    entry.kind = (uint8_t)kind;
    if(pos < length && line[pos] == ' ') pos++;
    // an AirPods pair's model and colour: "@" and six hex digits
    if(pos + 7 <= length && line[pos] == '@') {
        uint32_t value = 0;
        bool hex = true;
        for(size_t i = 1; i <= 6 && hex; i++) {
            const int digit = hex_value(line[pos + i]);
            hex = digit >= 0;
            value = value << 4 | (uint32_t)(digit < 0 ? 0 : digit);
        }
        if(hex && (pos + 7 == length || line[pos + 7] == ' ')) {
            entry.model = (uint16_t)(value >> 8);
            entry.colour = (uint8_t)value;
            pos += 7;
            if(pos < length) pos++;
        }
    }
    size_t n = 0;
    while(pos < length && n < BLE_SEEN_NAME_SIZE - 1) {
        const char c = line[pos++];
        entry.label[n++] = (c >= ' ' && c < 127) ? c : '?';
    }
    entry.label[n] = '\0';
    if(ble_mine_find(mine, entry.addr)) return; // the first line for an address counts
    mine->entries[mine->count++] = entry;
}

void ble_mine_parse(BleMine* mine, const char* text, size_t length) {
    memset(mine, 0, sizeof(BleMine));
    for(size_t start = 0; text && start < length;) {
        const char* line = text + start;
        const char* end = memchr(line, '\n', length - start);
        size_t line_length = end ? (size_t)(end - line) : length - start;
        start += line_length + 1;
        if(line_length && line[line_length - 1] == '\r') line_length--;
        if(line_length && line[0] != '#') parse_line(mine, line, line_length);
    }
}

size_t ble_mine_format(const BleMine* mine, char* out, size_t size) {
    if(!size) return 0;
    size_t used = 0;
    out[0] = '\0';
    for(size_t i = 0; i < mine->count; i++) {
        const BleMineEntry* e = &mine->entries[i];
        char signature[10] = "";
        if(e->model) snprintf(signature, sizeof(signature), "@%04X%02X ", e->model, e->colour);
        const int n = snprintf(
            out + used,
            size - used,
            "%02X:%02X:%02X:%02X:%02X:%02X %u %s%s\n",
            e->addr[5],
            e->addr[4],
            e->addr[3],
            e->addr[2],
            e->addr[1],
            e->addr[0],
            (unsigned)e->kind,
            signature,
            e->label);
        if(n < 0 || (size_t)n >= size - used) {
            out[used] = '\0'; // only whole lines
            break;
        }
        used += (size_t)n;
    }
    return used;
}

const BleMineEntry* ble_mine_find(const BleMine* mine, const uint8_t* addr) {
    for(size_t i = 0; i < mine->count; i++) {
        if(memcmp(mine->entries[i].addr, addr, BLE_SEEN_ADDR_SIZE) == 0) return &mine->entries[i];
    }
    return NULL;
}

static void remove_at(BleMine* mine, size_t index) {
    memmove(
        &mine->entries[index],
        &mine->entries[index + 1],
        (mine->count - index - 1) * sizeof(BleMineEntry));
    mine->count--;
}

void ble_mine_set(BleMine* mine, BleSeen* device, bool is_mine, const char* label) {
    device->mine = is_mine;
    const BleMineEntry* found = ble_mine_find(mine, device->addr);
    if(found) remove_at(mine, (size_t)(found - mine->entries));
    if(is_mine) {
        if(mine->count >= BLE_MINE_MAX) remove_at(mine, 0); // the oldest makes room
        BleMineEntry* entry = &mine->entries[mine->count++];
        memset(entry, 0, sizeof(BleMineEntry));
        memcpy(entry->addr, device->addr, BLE_SEEN_ADDR_SIZE);
        entry->kind = device->kind;
        entry->model = device->model;
        entry->colour = device->colour;
        snprintf(entry->label, sizeof(entry->label), "%s", label ? label : "");
    }
    mine->changed = true;
}

void ble_mine_recognise(const BleMine* mine, BleSeen* device) {
    if((device->traits & BLE_TRAIT_PAIRED) || ble_mine_find(mine, device->addr)) {
        device->mine = true;
        return;
    }
    // AirPods: a pair marked as mine, by model and colour, as near as they are on you
    if(!device->model || device->rssi_avg_x8 / 8 < BLE_MINE_NEAR_DBM) return;
    for(size_t i = 0; i < mine->count; i++) {
        const BleMineEntry* entry = &mine->entries[i];
        if(entry->model == device->model && entry->colour == device->colour) {
            device->mine = true;
            return;
        }
    }
}

/** Devices that keep one address are not followed: a stranger's device could take a mark */
static bool kind_changes_address(uint8_t kind) {
    return kind != BleKindUnknown && kind != BleKindNamed && kind != BleKindTile;
}

static int rssi_of(const BleSeen* device) {
    return device->rssi_avg_x8 / 8;
}

size_t ble_mine_follow(BleMine* mine, BleSeenTable* table, uint32_t now) {
    size_t moved = 0;
    for(size_t i = 0; i < table->count; i++) {
        BleSeen* gone = &table->devices[i];
        if(!gone->mine || gone->handed_over || !kind_changes_address(gone->kind)) continue;
        if(now - gone->last_seen < BLE_MINE_SILENT_MS) continue;

        // exactly one device of its kind that turned up just after it fell silent, as strong
        BleSeen* successor = NULL;
        size_t candidates = 0;
        for(size_t j = 0; j < table->count; j++) {
            BleSeen* device = &table->devices[j];
            if(j == i || device->mine || device->kind != gone->kind || device->count < 2) continue;
            if(device->first_seen < gone->last_seen) continue; // heard together: another one
            if(device->first_seen - gone->last_seen > BLE_MINE_GAP_MS) continue;
            const int difference = rssi_of(device) - rssi_of(gone);
            if(difference > BLE_MINE_RSSI_DB || difference < -BLE_MINE_RSSI_DB) continue;
            successor = device;
            candidates++;
        }
        if(candidates != 1) {
            // none yet; or more than one, and no telling which: it stays where it is
            if(now - gone->last_seen > BLE_MINE_SILENT_MS + BLE_MINE_GAP_MS) gone->handed_over = true;
            continue;
        }

        successor->mine = true;
        gone->handed_over = true;
        for(size_t k = 0; k < mine->count; k++) {
            if(memcmp(mine->entries[k].addr, gone->addr, BLE_SEEN_ADDR_SIZE) == 0) {
                memcpy(mine->entries[k].addr, successor->addr, BLE_SEEN_ADDR_SIZE);
                mine->changed = true;
            }
        }
        // still with you since the first of its addresses
        successor->first_seen = gone->first_seen;
        moved++;
    }
    return moved;
}
