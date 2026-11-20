#include "subghz_location.h"

#include <gps/gps.h>

// a fix older than this is not where the signal was
#define LOCATION_FRESH_MS (30U * 1000U)
// with nothing heard for this long, the request goes again (the phone may have come back)
#define LOCATION_RETRY_MS (10U * 1000U)
#define LOCATION_HZ       (1U)

struct SubGhzLocation {
    Gps* gps;
    FuriMutex* mutex;
    bool running;
    bool have;
    int32_t latitude;
    int32_t longitude;
    uint32_t fix_tick;
    uint32_t request_tick;
};

static void subghz_location_callback(GpsStatus status, const GpsLocation* fix, void* context) {
    SubGhzLocation* location = context;
    if(status != GpsStatusOk || !fix) return;
    furi_mutex_acquire(location->mutex, FuriWaitForever);
    location->have = true;
    location->latitude = fix->latitude;
    location->longitude = fix->longitude;
    location->fix_tick = furi_get_tick();
    furi_mutex_release(location->mutex);
}

SubGhzLocation* subghz_location_alloc(void) {
    SubGhzLocation* location = malloc(sizeof(SubGhzLocation));
    memset(location, 0, sizeof(SubGhzLocation));
    location->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    return location;
}

void subghz_location_free(SubGhzLocation* location) {
    if(!location) return;
    subghz_location_stop(location);
    furi_mutex_free(location->mutex);
    free(location);
}

static void subghz_location_request(SubGhzLocation* location) {
    location->request_tick = furi_get_tick();
    gps_request_stream(location->gps, LOCATION_HZ);
}

void subghz_location_start(SubGhzLocation* location) {
    if(!location || location->running) return;
    location->gps = furi_record_open(RECORD_GPS);
    gps_set_location_callback(location->gps, subghz_location_callback, location);
    location->running = true;
    subghz_location_request(location);
}

void subghz_location_poll(SubGhzLocation* location) {
    if(!location || !location->running) return;
    const uint32_t now = furi_get_tick();
    furi_mutex_acquire(location->mutex, FuriWaitForever);
    const bool quiet = !location->have || now - location->fix_tick > LOCATION_RETRY_MS;
    furi_mutex_release(location->mutex);
    if(quiet && now - location->request_tick > LOCATION_RETRY_MS) subghz_location_request(location);
}

void subghz_location_stop(SubGhzLocation* location) {
    if(!location || !location->running) return;
    gps_stop_stream(location->gps);
    gps_set_location_callback(location->gps, NULL, NULL);
    furi_record_close(RECORD_GPS);
    location->gps = NULL;
    location->running = false;
}

bool subghz_location_get(SubGhzLocation* location, int32_t* latitude, int32_t* longitude) {
    if(!location || !location->running) return false;
    furi_mutex_acquire(location->mutex, FuriWaitForever);
    const bool fresh = location->have && furi_get_tick() - location->fix_tick < LOCATION_FRESH_MS;
    if(fresh) {
        *latitude = location->latitude;
        *longitude = location->longitude;
    }
    furi_mutex_release(location->mutex);
    return fresh;
}
