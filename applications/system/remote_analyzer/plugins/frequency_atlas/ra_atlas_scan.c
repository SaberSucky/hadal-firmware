#include "ra_atlas_scan.h"

#include <string.h>

#define RA_ATLAS_RSSI_FLOOR (-127.0f)

static uint32_t ra_atlas_scan_ticks(uint32_t ms, uint32_t tps) {
    return ms * tps / 1000;
}

bool ra_atlas_scan_init(
    RaAtlasScan* scan,
    const bool* tunable,
    size_t count,
    float trigger,
    uint32_t ticks_per_second,
    uint32_t now) {
    memset(scan, 0, sizeof(*scan));
    if(count > RA_ATLAS_SCAN_BANDS_MAX) count = RA_ATLAS_SCAN_BANDS_MAX;
    scan->count = count;
    scan->trigger = trigger;

    if(!ticks_per_second) ticks_per_second = 1000;
    scan->ticks_per_second = ticks_per_second;
    scan->dwell = ra_atlas_scan_ticks(RA_ATLAS_DWELL_MS, ticks_per_second);
    scan->signal_hold = ra_atlas_scan_ticks(RA_ATLAS_SIGNAL_HOLD_MS, ticks_per_second);
    scan->park_max = ra_atlas_scan_ticks(RA_ATLAS_PARK_MAX_MS, ticks_per_second);
    scan->active = ra_atlas_scan_ticks(RA_ATLAS_ACTIVE_MS, ticks_per_second);

    bool any = false;
    for(size_t i = 0; i < count; i++) {
        scan->live[i].tunable = tunable && tunable[i];
        scan->live[i].rssi_last = RA_ATLAS_RSSI_FLOOR;
        scan->live[i].rssi_peak = RA_ATLAS_RSSI_FLOOR;
        if(scan->live[i].tunable && !any) {
            scan->current = i;
            any = true;
        }
    }
    scan->t_park = now;
    return any;
}

/** The next tunable band after the current one, wrapping. The current one if it is the only. */
static size_t ra_atlas_scan_next(const RaAtlasScan* scan) {
    for(size_t step = 1; step <= scan->count; step++) {
        const size_t i = (scan->current + step) % scan->count;
        if(scan->live[i].tunable) return i;
    }
    return scan->current;
}

bool ra_atlas_scan_is_signal(const RaAtlasScan* scan, uint32_t now) {
    return scan->have_carrier && ((now - scan->t_carrier) < scan->signal_hold);
}

RaAtlasAction ra_atlas_scan_tick(RaAtlasScan* scan, float rssi, uint32_t now) {
    if(!scan->count || !scan->live[scan->current].tunable) return RaAtlasActionNone;

    RaAtlasLive* live = &scan->live[scan->current];
    live->rssi_last = rssi;
    if(rssi > live->rssi_peak) live->rssi_peak = rssi;
    if(rssi >= scan->trigger) {
        live->seen = true;
        live->t_seen = now;
        scan->t_carrier = now;
        scan->have_carrier = true;
    }

    if(scan->hold) return RaAtlasActionNone;

    const uint32_t parked = now - scan->t_park;
    if(parked < scan->dwell) return RaAtlasActionNone;
    // Something is on the air here: stay, so the decoders hear the rest of it
    if(ra_atlas_scan_is_signal(scan, now) && (parked < scan->park_max)) {
        return RaAtlasActionNone;
    }

    const size_t next = ra_atlas_scan_next(scan);
    if(next == scan->current) {
        // The only band there is: start its park again rather than retuning onto itself
        scan->t_park = now;
        return RaAtlasActionNone;
    }
    scan->current = next;
    scan->t_park = now;
    scan->have_carrier = false;
    return RaAtlasActionRetune;
}

void ra_atlas_scan_decoded(RaAtlasScan* scan, const char* protocol, const char* note, uint32_t now) {
    if(!scan->count) return;
    RaAtlasLive* live = &scan->live[scan->current];

    size_t n = 0;
    if(protocol) {
        while(protocol[n] && (n + 1 < RA_ATLAS_PROTOCOL_SIZE)) {
            live->protocol[n] = protocol[n];
            n++;
        }
    }
    live->protocol[n] = '\0';
    live->note = note;
    live->decoded = true;
    live->t_decoded = now;

    // A frame is the surest sign there is: count it as a carrier, and stay for the next one
    live->seen = true;
    live->t_seen = now;
    scan->t_carrier = now;
    scan->have_carrier = true;
}

bool ra_atlas_scan_hold(RaAtlasScan* scan, size_t index, uint32_t now, bool* retune) {
    if(retune) *retune = false;
    if((index >= scan->count) || !scan->live[index].tunable) return false;

    if(index != scan->current) {
        scan->current = index;
        scan->have_carrier = false;
        if(retune) *retune = true;
    }
    scan->hold = true;
    scan->t_park = now;
    return true;
}

void ra_atlas_scan_release(RaAtlasScan* scan, uint32_t now) {
    scan->hold = false;
    scan->t_park = now;
}

size_t ra_atlas_scan_current(const RaAtlasScan* scan) {
    return scan->current;
}

bool ra_atlas_scan_is_held(const RaAtlasScan* scan) {
    return scan->hold;
}

RaAtlasStatus ra_atlas_scan_status(const RaAtlasScan* scan, size_t index, uint32_t now) {
    if((index >= scan->count) || !scan->live[index].tunable) return RaAtlasStatusOutOfRange;
    const RaAtlasLive* live = &scan->live[index];
    if(!live->seen) return RaAtlasStatusQuiet;
    return ((now - live->t_seen) < scan->active) ? RaAtlasStatusActive : RaAtlasStatusSeen;
}

uint32_t ra_atlas_scan_seen_ago(const RaAtlasScan* scan, size_t index, uint32_t now) {
    if((index >= scan->count) || !scan->live[index].seen) return 0;
    return (now - scan->live[index].t_seen) / scan->ticks_per_second;
}

const RaAtlasLive* ra_atlas_scan_live(const RaAtlasScan* scan, size_t index) {
    return (index < scan->count) ? &scan->live[index] : NULL;
}
