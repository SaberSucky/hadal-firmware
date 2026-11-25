/**
 * @file battery_stats.h
 * @brief What the battery history says: the graph's columns, and how long the charge will last.
 * No dependencies, for the host tests.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../../../services/power/power_service/battery_log.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BATTERY_STATS_NO_DATA (-1)

/**
 * The graph: one column per width-th of the span ending at now. charge[x] is the last charge
 * heard in the column (BATTERY_STATS_NO_DATA if none, the Flipper was off), charging[x] whether it
 * was charging at any time in it
 */
void battery_stats_columns(
    const BatteryLogRecord* records,
    size_t count,
    uint32_t now,
    uint32_t span_s,
    int8_t* charge,
    bool* charging,
    size_t width);

typedef struct {
    uint32_t seconds_left; /**< at the rate below, from the last record's charge */
    uint32_t percent_per_day_x10; /**< how fast it goes down */
} BatteryEstimate;

/**
 * How long the charge lasts, from the latest stretch on battery (since unplugging, with the
 * Flipper on throughout). Needs two hours and a 2% drop.
 *
 * @return false when there is not enough to tell, or it is charging
 */
bool battery_stats_estimate(const BatteryLogRecord* records, size_t count, BatteryEstimate* estimate);

/** "2 days 4 h", "5 h 20 min", "35 min" */
void battery_stats_format_duration(uint32_t seconds, char* out, size_t size);

#ifdef __cplusplus
}
#endif
