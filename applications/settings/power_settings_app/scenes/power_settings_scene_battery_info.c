#include "../power_settings_app.h"

#include <furi_hal_power.h>
#include <furi_hal_rtc.h>
#include <storage/storage.h>

/* The history's records: both log files, up to what they can hold (32 KB, read once as the page
 * opens and let go as soon as the graph is worked out), and the reading now at the end, so the
 * line reaches the right edge */
#define BATTERY_RECORDS_MAX (2U * BATTERY_LOG_ROTATE_SIZE / sizeof(BatteryLogRecord))

static size_t battery_records_in(Storage* storage, const char* path, size_t max) {
    File* file = storage_file_alloc(storage);
    uint64_t count = 0;
    if(storage_file_open(file, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        count = storage_file_size(file) / sizeof(BatteryLogRecord);
    }
    storage_file_free(file);
    return count < max ? (size_t)count : max;
}

static size_t battery_records_read(
    Storage* storage,
    const char* path,
    BatteryLogRecord* records,
    size_t room) {
    File* file = storage_file_alloc(storage);
    size_t count = 0;
    if(room && storage_file_open(file, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        // the newest records, if the file holds more than there is room for
        const uint64_t whole = storage_file_size(file) / sizeof(BatteryLogRecord);
        if(whole > room) {
            storage_file_seek(file, (uint32_t)((whole - room) * sizeof(BatteryLogRecord)), true);
        }
        const size_t want = (size_t)(whole < room ? whole : room);
        count = storage_file_read(file, records, want * sizeof(BatteryLogRecord)) /
                sizeof(BatteryLogRecord);
    }
    storage_file_free(file);
    return count;
}

static void power_settings_scene_battery_info_load_history(PowerSettingsApp* app) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    // the newer file is never cut for the older one; the older goes first, read into its place
    const size_t newer = battery_records_in(storage, BATTERY_LOG_PATH, BATTERY_RECORDS_MAX);
    const size_t older =
        battery_records_in(storage, BATTERY_LOG_OLD_PATH, BATTERY_RECORDS_MAX - newer);
    BatteryLogRecord* records = malloc(sizeof(BatteryLogRecord) * (older + newer + 1));
    size_t count = battery_records_read(storage, BATTERY_LOG_OLD_PATH, records, older);
    count += battery_records_read(storage, BATTERY_LOG_PATH, records + count, newer);
    furi_record_close(RECORD_STORAGE);

    records[count++] = (BatteryLogRecord){
        .timestamp = furi_hal_rtc_get_timestamp(),
        .charge = furi_hal_power_get_pct(),
        .flags = furi_hal_power_is_charging() ? BatteryLogFlagCharging : 0,
    };
    battery_info_set_history(app->battery_info, records, count);
    free(records);
}

static void power_settings_scene_battery_info_update_model(PowerSettingsApp* app) {
    power_get_info(app->power, &app->info);
    BatteryInfoModel battery_info_data = {
        .vbus_voltage = app->info.voltage_vbus,
        .gauge_voltage = app->info.voltage_gauge,
        .gauge_current = app->info.current_gauge,
        .gauge_temperature = app->info.temperature_gauge,
        .charge_voltage_limit = app->info.voltage_battery_charge_limit,
        .charge = app->info.charge,
        .health = app->info.health,
    };
    battery_info_set_data(app->battery_info, &battery_info_data);
}

void power_settings_scene_battery_info_on_enter(void* context) {
    PowerSettingsApp* app = context;
    power_settings_scene_battery_info_update_model(app);
    power_settings_scene_battery_info_load_history(app);
    view_dispatcher_switch_to_view(app->view_dispatcher, PowerSettingsAppViewBatteryInfo);
}

bool power_settings_scene_battery_info_on_event(void* context, SceneManagerEvent event) {
    PowerSettingsApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeTick) {
        power_settings_scene_battery_info_update_model(app);
        consumed = true;
    }
    return consumed;
}

void power_settings_scene_battery_info_on_exit(void* context) {
    UNUSED(context);
}
