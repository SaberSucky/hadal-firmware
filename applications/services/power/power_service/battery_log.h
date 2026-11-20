/**
 * @file battery_log.h
 * @brief Battery history: the power service writes the charge to the SD card every 10 minutes
 * and when the charger is plugged in or out, for the Battery History app.
 *
 * Two files of 8-byte records, oldest first: history.bin, and history.old.bin, the one before
 * it (16 KB, about two weeks, each). A gap between records is time the Flipper was off.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BATTERY_LOG_DIR         "/ext/apps_data/battery"
#define BATTERY_LOG_PATH        BATTERY_LOG_DIR "/history.bin"
#define BATTERY_LOG_OLD_PATH    BATTERY_LOG_DIR "/history.old.bin"
#define BATTERY_LOG_INTERVAL_S  (10U * 60U)
#define BATTERY_LOG_ROTATE_SIZE (16U * 1024U)

typedef enum {
    BatteryLogFlagCharging = (1 << 0),
    BatteryLogFlagStart = (1 << 1), /**< the first record after the Flipper started */
} BatteryLogFlag;

typedef struct {
    uint32_t timestamp; /**< RTC, seconds */
    uint8_t charge; /**< percent */
    uint8_t flags; /**< BatteryLogFlag */
    uint16_t voltage_mv;
} BatteryLogRecord;

_Static_assert(sizeof(BatteryLogRecord) == 8, "the file format is 8 bytes a record");

/** The logger's state, in the power service */
typedef struct {
    uint32_t next; /**< RTC seconds when the next record is due, 0: at start */
    bool started;
    bool charging;
} BatteryLog;

#ifdef __cplusplus
}
#endif
