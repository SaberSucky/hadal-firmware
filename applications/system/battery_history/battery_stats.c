#include "battery_stats.h"

#include <stdio.h>
#include <string.h>

// Records further apart than this were not one stretch: the Flipper was off between them
#define STATS_GAP_S (BATTERY_LOG_INTERVAL_S * 3U)
#define STATS_MIN_SPAN_S (2U * 3600U)
#define STATS_MIN_DROP   (2)
#define STATS_MAX_SPAN_S (3U * 24U * 3600U)

void battery_stats_columns(
    const BatteryLogRecord* records,
    size_t count,
    uint32_t now,
    uint32_t span_s,
    int8_t* charge,
    bool* charging,
    size_t width) {
    for(size_t x = 0; x < width; x++) {
        charge[x] = BATTERY_STATS_NO_DATA;
        charging[x] = false;
    }
    if(!width || !span_s) return;
    const uint32_t start = now > span_s ? now - span_s : 0;
    for(size_t i = 0; i < count; i++) {
        const BatteryLogRecord* record = &records[i];
        if(record->timestamp < start || record->timestamp > now) continue;
        size_t x = (size_t)((uint64_t)(record->timestamp - start) * width / span_s);
        if(x >= width) x = width - 1;
        charge[x] = (int8_t)(record->charge > 100 ? 100 : record->charge);
        if(record->flags & BatteryLogFlagCharging) charging[x] = true;
    }
    // a record stands until the next one, if the Flipper stayed on: fill the columns between
    // records closer than the gap
    int last_x = -1;
    uint32_t last_time = 0;
    for(size_t i = 0; i < count; i++) {
        const BatteryLogRecord* record = &records[i];
        if(record->timestamp < start || record->timestamp > now) continue;
        int x = (int)((uint64_t)(record->timestamp - start) * width / span_s);
        if(x >= (int)width) x = (int)width - 1;
        if(last_x >= 0 && x > last_x + 1 && record->timestamp - last_time <= STATS_GAP_S &&
           !(record->flags & BatteryLogFlagStart)) {
            for(int fill = last_x + 1; fill < x; fill++) {
                if(charge[fill] == BATTERY_STATS_NO_DATA) {
                    charge[fill] = charge[last_x];
                    charging[fill] = charging[last_x] && charging[x];
                }
            }
        }
        last_x = x;
        last_time = record->timestamp;
    }
}

bool battery_stats_estimate(const BatteryLogRecord* records, size_t count, BatteryEstimate* estimate) {
    if(!count) return false;
    const BatteryLogRecord* last = &records[count - 1];
    if(last->flags & BatteryLogFlagCharging) return false;

    // back from the newest record, while on battery and on
    size_t first = count - 1;
    while(first > 0) {
        const BatteryLogRecord* before = &records[first - 1];
        const BatteryLogRecord* current = &records[first];
        if(before->flags & BatteryLogFlagCharging) break;
        if(current->flags & BatteryLogFlagStart) break;
        if(current->timestamp < before->timestamp) break;
        if(current->timestamp - before->timestamp > STATS_GAP_S) break;
        if(last->timestamp - before->timestamp > STATS_MAX_SPAN_S) break;
        first--;
    }

    // a least-squares line through the stretch (the charge in steps of 1% reads noisy), about
    // the means, which keeps single precision exact enough
    const uint32_t t0 = records[first].timestamp;
    const uint32_t span = last->timestamp - t0;
    const int drop = (int)records[first].charge - (int)last->charge;
    if(span < STATS_MIN_SPAN_S || drop < STATS_MIN_DROP) return false;
    const float n = (float)(count - first);
    float mean_t = 0.0f, mean_c = 0.0f;
    for(size_t i = first; i < count; i++) {
        mean_t += (float)(records[i].timestamp - t0) / 3600.0f;
        mean_c += (float)records[i].charge;
    }
    mean_t /= n;
    mean_c /= n;
    float sum_tt = 0.0f, sum_tc = 0.0f;
    for(size_t i = first; i < count; i++) {
        const float t = (float)(records[i].timestamp - t0) / 3600.0f - mean_t;
        sum_tt += t * t;
        sum_tc += t * ((float)records[i].charge - mean_c);
    }
    if(sum_tt <= 0.0f) return false;
    const float slope = sum_tc / sum_tt; // % per hour
    if(slope >= -0.01f) return false;
    const float hours_left = (float)last->charge / -slope;
    estimate->seconds_left = hours_left > 365.0f * 24.0f ? 365U * 24U * 3600U :
                                                           (uint32_t)(hours_left * 3600.0f);
    estimate->percent_per_day_x10 = (uint32_t)(-slope * 240.0f + 0.5f);
    return true;
}

void battery_stats_format_duration(uint32_t seconds, char* out, size_t size) {
    const uint32_t minutes = seconds / 60;
    const uint32_t hours = minutes / 60;
    const uint32_t days = hours / 24;
    if(days >= 2) {
        snprintf(out, size, "%lu days %lu h", (unsigned long)days, (unsigned long)(hours % 24));
    } else if(days == 1) {
        snprintf(out, size, "1 day %lu h", (unsigned long)(hours % 24));
    } else if(hours) {
        snprintf(out, size, "%lu h %lu min", (unsigned long)hours, (unsigned long)(minutes % 60));
    } else {
        snprintf(out, size, "%lu min", (unsigned long)minutes);
    }
}
