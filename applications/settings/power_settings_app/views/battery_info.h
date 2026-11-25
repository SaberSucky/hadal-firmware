#pragma once

#include <gui/view.h>
#include "battery_stats.h"

/* Battery info: the battery now (its face, what it draws or takes, its stats), and under it, a
 * page down, its history: the charge over a day to two weeks and how long it will last */

typedef struct BatteryInfo BatteryInfo;

typedef struct {
    float vbus_voltage;
    float gauge_voltage;
    float gauge_current;
    float gauge_temperature;
    float charge_voltage_limit;
    uint8_t charge;
    uint8_t health;
} BatteryInfoModel;

BatteryInfo* battery_info_alloc(void);

void battery_info_free(BatteryInfo* battery_info);

View* battery_info_get_view(BatteryInfo* battery_info);

void battery_info_set_data(BatteryInfo* battery_info, BatteryInfoModel* data);

/** The history page's graph and estimate, from the power service's records (oldest first, the
 * reading now last): worked out once, the records can go after */
void battery_info_set_history(
    BatteryInfo* battery_info,
    const BatteryLogRecord* records,
    size_t count);
