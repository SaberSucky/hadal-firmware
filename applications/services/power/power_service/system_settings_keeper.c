#include "system_settings_keeper.h"

#include <furi.h>
#include <furi_hal.h>
#include <storage/storage.h>
#include <toolbox/saved_struct.h>

#define TAG "SettingsKeeper"

#define KEEPER_PATH    INT_PATH(".system.settings")
#define KEEPER_MAGIC   (0x4B) // 'K'
#define KEEPER_VERSION (1)

// The flags that are the user's settings (not the system's own: lock, update, format)
static const FuriHalRtcFlag kept_flags[] = {
    FuriHalRtcFlagDebug,
    FuriHalRtcFlagHandOrient,
    FuriHalRtcFlagLegacySleep,
    FuriHalRtcFlagStealthMode,
    FuriHalRtcFlagDetailedFilename,
};

typedef struct {
    uint8_t flags;
    uint8_t log_level;
    uint8_t log_device;
    uint8_t log_baud_rate;
    uint8_t units;
    uint8_t time_format;
    uint8_t date_format;
    uint8_t reserved;
} SystemSettingsKept;

static void keeper_read(SystemSettingsKept* kept) {
    memset(kept, 0, sizeof(SystemSettingsKept));
    for(size_t i = 0; i < COUNT_OF(kept_flags); i++) {
        if(furi_hal_rtc_is_flag_set(kept_flags[i])) kept->flags |= kept_flags[i];
    }
    kept->log_level = furi_hal_rtc_get_log_level();
    kept->log_device = furi_hal_rtc_get_log_device();
    kept->log_baud_rate = furi_hal_rtc_get_log_baud_rate();
    kept->units = furi_hal_rtc_get_locale_units();
    kept->time_format = furi_hal_rtc_get_locale_timeformat();
    kept->date_format = furi_hal_rtc_get_locale_dateformat();
}

static void keeper_apply(const SystemSettingsKept* kept) {
    for(size_t i = 0; i < COUNT_OF(kept_flags); i++) {
        if(kept->flags & kept_flags[i]) {
            furi_hal_rtc_set_flag(kept_flags[i]);
        } else {
            furi_hal_rtc_reset_flag(kept_flags[i]);
        }
    }
    if(kept->flags & FuriHalRtcFlagDebug) {
        furi_hal_debug_enable();
    } else {
        furi_hal_debug_disable();
    }
    furi_hal_rtc_set_log_level(kept->log_level);
    furi_log_set_level(kept->log_level);
    // the log port and speed take effect from the next start
    furi_hal_rtc_set_log_device(kept->log_device);
    furi_hal_rtc_set_log_baud_rate(kept->log_baud_rate);
    furi_hal_rtc_set_locale_units(kept->units);
    furi_hal_rtc_set_locale_timeformat(kept->time_format);
    furi_hal_rtc_set_locale_dateformat(kept->date_format);
}

static bool keeper_load(SystemSettingsKept* kept) {
    return saved_struct_load(
        KEEPER_PATH, kept, sizeof(SystemSettingsKept), KEEPER_MAGIC, KEEPER_VERSION);
}

// Written only when it differs from the file: most starts write nothing
static void keeper_store(const SystemSettingsKept* now, const SystemSettingsKept* on_file) {
    if(on_file && memcmp(now, on_file, sizeof(SystemSettingsKept)) == 0) return;
    if(!saved_struct_save(
           KEEPER_PATH, now, sizeof(SystemSettingsKept), KEEPER_MAGIC, KEEPER_VERSION)) {
        FURI_LOG_W(TAG, "Could not keep the system settings");
    }
}

void system_settings_keeper_start(void) {
    if(furi_hal_rtc_get_boot_mode() != FuriHalRtcBootModeNormal) return;

    SystemSettingsKept on_file;
    const bool have_file = keeper_load(&on_file);
    if(!furi_hal_rtc_get_register(FuriHalRtcRegisterSettingsKept) && have_file) {
        // the registers were wiped since the file was written: they get the settings back
        FURI_LOG_I(TAG, "Registers were reset, restoring the system settings");
        keeper_apply(&on_file);
    }
    furi_hal_rtc_set_register(FuriHalRtcRegisterSettingsKept, 1);

    SystemSettingsKept now;
    keeper_read(&now);
    keeper_store(&now, have_file ? &on_file : NULL);
}

void system_settings_keeper_save(void) {
    if(furi_hal_rtc_get_boot_mode() != FuriHalRtcBootModeNormal) return;
    SystemSettingsKept on_file;
    const bool have_file = keeper_load(&on_file);
    SystemSettingsKept now;
    keeper_read(&now);
    keeper_store(&now, have_file ? &on_file : NULL);
}
