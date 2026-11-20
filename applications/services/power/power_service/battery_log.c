#include "battery_log.h"
#include "power_i.h"

#include <furi.h>
#include <furi_hal_rtc.h>
#include <storage/storage.h>

#define TAG "BatteryLog"

// The SD card is mounted a few seconds after start
#define BATTERY_LOG_START_DELAY_MS (30U * 1000U)
// When a record could not be written (no card), try again
#define BATTERY_LOG_RETRY_S (60U)

static bool battery_log_append(const BatteryLogRecord* record) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    bool success = false;
    if(storage_sd_status(storage) == FSE_OK) {
        storage_simply_mkdir(storage, BATTERY_LOG_DIR);
        File* file = storage_file_alloc(storage);
        uint64_t size = 0;
        if(storage_file_open(file, BATTERY_LOG_PATH, FSAM_WRITE, FSOM_OPEN_APPEND)) {
            success = storage_file_write(file, record, sizeof(*record)) == sizeof(*record);
            size = storage_file_size(file);
        }
        storage_file_free(file);
        // full: it becomes the old one, and the one before goes
        if(success && size >= BATTERY_LOG_ROTATE_SIZE) {
            storage_common_remove(storage, BATTERY_LOG_OLD_PATH);
            storage_common_rename(storage, BATTERY_LOG_PATH, BATTERY_LOG_OLD_PATH);
        }
    }
    furi_record_close(RECORD_STORAGE);
    return success;
}

void battery_log_tick(BatteryLog* log, const PowerInfo* info) {
    if(furi_get_tick() < BATTERY_LOG_START_DELAY_MS) return;
    const uint32_t now = furi_hal_rtc_get_timestamp();
    const bool charging = info->is_charging;
    // due: at start, every interval, on plugging in or out, and if the clock was set back
    const bool due = !log->started || now >= log->next || charging != log->charging ||
                     log->next > now + BATTERY_LOG_INTERVAL_S;
    if(!due) return;

    const float voltage = info->voltage_gauge * 1000.0f;
    const BatteryLogRecord record = {
        .timestamp = now,
        .charge = info->charge,
        .flags = (charging ? BatteryLogFlagCharging : 0) | (log->started ? 0 : BatteryLogFlagStart),
        .voltage_mv = voltage > 0 && voltage < 65535.0f ? (uint16_t)voltage : 0,
    };
    log->charging = charging;
    if(battery_log_append(&record)) {
        log->started = true;
        log->next = now + BATTERY_LOG_INTERVAL_S;
    } else {
        log->next = now + BATTERY_LOG_RETRY_S;
        FURI_LOG_D(TAG, "Not written");
    }
}
