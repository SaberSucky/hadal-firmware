#include "desktop_settings.h"
#include "desktop_settings_filename.h"

#include <saved_struct.h>
#include <gui/elements.h>
#include <storage/storage.h>

#define TAG "DesktopSettings"

#define DESKTOP_SETTINGS_VER_14 (14)
#define DESKTOP_SETTINGS_VER_17 (17)
#define DESKTOP_SETTINGS_VER_18 (18)
#define DESKTOP_SETTINGS_VER_19 (19)
#define DESKTOP_SETTINGS_VER_20 (20)
#define DESKTOP_SETTINGS_VER_21 (21)
#define DESKTOP_SETTINGS_VER_22 (22)
#define DESKTOP_SETTINGS_VER    (23)

#define DESKTOP_SETTINGS_PATH  INT_PATH(DESKTOP_SETTINGS_FILE_NAME)
#define DESKTOP_SETTINGS_MAGIC (0x17)

typedef struct {
    uint32_t auto_lock_delay_ms;
    uint8_t displayBatteryPercentage;
    uint8_t dummy_mode;
    uint8_t display_clock;
    FavoriteApp favorite_apps[FavoriteAppNumber];
    FavoriteApp dummy_apps[DummyAppNumber];
} DesktopSettingsV14;

// v18 files hold everything up to the asset pack name: 8 bytes of numbers, the favorite and
// dummy app paths and the menu style
_Static_assert(
    offsetof(DesktopSettings, active_asset_pack) ==
        8 + sizeof(FavoriteApp) * (FavoriteAppNumber + DummyAppNumber) + 32,
    "DesktopSettings before active_asset_pack must still be the v18 layout");

// v19 files hold everything up to the lock options, which v20 appended
_Static_assert(
    offsetof(DesktopSettings, boot_unlocked) == offsetof(DesktopSettings, active_asset_pack) + 32,
    "DesktopSettings before boot_unlocked must still be the v19 layout");

// v20 files hold everything up to dark mode, which v21 appended
_Static_assert(
    offsetof(DesktopSettings, dark_mode) == offsetof(DesktopSettings, boot_unlocked) + 4,
    "DesktopSettings before dark_mode must still be the v20 layout");

// v21 files hold everything up to the animation option, which v22 appended
_Static_assert(
    offsetof(DesktopSettings, animate_screen_off) == offsetof(DesktopSettings, dark_mode) + 1,
    "DesktopSettings before animate_screen_off must still be the v21 layout");

// v22 files hold everything up to the header style, which v23 appended
_Static_assert(
    offsetof(DesktopSettings, header_style) == offsetof(DesktopSettings, animate_screen_off) + 1,
    "DesktopSettings before header_style must still be the v22 layout");

// The field v23 added: zero, the fading line
static void desktop_settings_clear_v23_options(DesktopSettings* settings) {
    settings->header_style = 0;
}

// The field v22 added (and v23's): zero, the animation pausing in the dark, saves the battery
static void desktop_settings_clear_v22_options(DesktopSettings* settings) {
    settings->animate_screen_off = 0;
    desktop_settings_clear_v23_options(settings);
}

// The fields v21 added (and v22's): zero is the behaviour before them
static void desktop_settings_clear_v21_options(DesktopSettings* settings) {
    settings->dark_mode = 0;
    desktop_settings_clear_v22_options(settings);
}

// The fields v20 added (and after them, v21's): zero is the behaviour before them
static void desktop_settings_clear_lock_options(DesktopSettings* settings) {
    settings->boot_unlocked = 0;
    settings->lock_clock = 0;
    settings->pin_fail_action = DesktopPinFailActionNone;
    settings->pin_fail_limit = 0;
    desktop_settings_clear_v21_options(settings);
}

// Actual size of DesktopSettings v13
//static_assert(sizeof(DesktopSettingsV13) == 1234);

static void desktop_settings_terminate_apps(FavoriteApp* apps, size_t count) {
    for(size_t i = 0; i < count; i++) {
        apps[i].name_or_path[sizeof(apps[i].name_or_path) - 1] = '\0';
    }
}

/** saved_struct checks magic, version, size and an 8-bit sum, but never the payload, so the file
 * can hold a string with no terminator in it. Everything here is later used as a C string.
 */
static void desktop_settings_terminate_strings(DesktopSettings* settings) {
    desktop_settings_terminate_apps(settings->favorite_apps, FavoriteAppNumber);
    desktop_settings_terminate_apps(settings->dummy_apps, DummyAppNumber);
    settings->menu_style[sizeof(settings->menu_style) - 1] = '\0';
    settings->active_asset_pack[sizeof(settings->active_asset_pack) - 1] = '\0';
    // an action the firmware does not know is none
    if(settings->pin_fail_action > DesktopPinFailActionFactoryReset) {
        settings->pin_fail_action = DesktopPinFailActionNone;
    }
    settings->dark_mode = settings->dark_mode ? 1 : 0;
    settings->animate_screen_off = settings->animate_screen_off ? 1 : 0;
    if(settings->header_style >= ElementsHeaderStyleCount) settings->header_style = 0;
}

void desktop_settings_load(DesktopSettings* settings) {
    furi_assert(settings);

    bool success = false;

    do {
        uint8_t version;
        if(!saved_struct_get_metadata(DESKTOP_SETTINGS_PATH, NULL, &version, NULL)) break;

        if(version == DESKTOP_SETTINGS_VER) {
            success = saved_struct_load(
                DESKTOP_SETTINGS_PATH,
                settings,
                sizeof(DesktopSettings),
                DESKTOP_SETTINGS_MAGIC,
                DESKTOP_SETTINGS_VER);

        } else if(version == DESKTOP_SETTINGS_VER_22) {
            // v23 appended the header style; everything before it is unchanged
            success = saved_struct_load(
                DESKTOP_SETTINGS_PATH,
                settings,
                offsetof(DesktopSettings, header_style),
                DESKTOP_SETTINGS_MAGIC,
                DESKTOP_SETTINGS_VER_22);

            if(success) desktop_settings_clear_v23_options(settings);

        } else if(version == DESKTOP_SETTINGS_VER_21) {
            // v22 appended the animation option; everything before it is unchanged
            success = saved_struct_load(
                DESKTOP_SETTINGS_PATH,
                settings,
                offsetof(DesktopSettings, animate_screen_off),
                DESKTOP_SETTINGS_MAGIC,
                DESKTOP_SETTINGS_VER_21);

            if(success) desktop_settings_clear_v22_options(settings);

        } else if(version == DESKTOP_SETTINGS_VER_20) {
            // v21 appended dark mode; everything before it is unchanged
            success = saved_struct_load(
                DESKTOP_SETTINGS_PATH,
                settings,
                offsetof(DesktopSettings, dark_mode),
                DESKTOP_SETTINGS_MAGIC,
                DESKTOP_SETTINGS_VER_20);

            if(success) desktop_settings_clear_v21_options(settings);

        } else if(version == DESKTOP_SETTINGS_VER_19) {
            // v20 appended the lock options; everything before them is unchanged
            success = saved_struct_load(
                DESKTOP_SETTINGS_PATH,
                settings,
                offsetof(DesktopSettings, boot_unlocked),
                DESKTOP_SETTINGS_MAGIC,
                DESKTOP_SETTINGS_VER_19);

            if(success) desktop_settings_clear_lock_options(settings);

        } else if(version == DESKTOP_SETTINGS_VER_18) {
            // v19 appended the asset pack name; everything before it is unchanged
            success = saved_struct_load(
                DESKTOP_SETTINGS_PATH,
                settings,
                offsetof(DesktopSettings, active_asset_pack),
                DESKTOP_SETTINGS_MAGIC,
                DESKTOP_SETTINGS_VER_18);

            if(success) {
                memset(settings->active_asset_pack, 0, sizeof(settings->active_asset_pack));
                desktop_settings_clear_lock_options(settings);
            }

        } else if(version == DESKTOP_SETTINGS_VER_17) {
            success = saved_struct_load(
                DESKTOP_SETTINGS_PATH,
                settings,
                offsetof(DesktopSettings, menu_style),
                DESKTOP_SETTINGS_MAGIC,
                DESKTOP_SETTINGS_VER_17);

            if(success) {
                memset(settings->menu_style, 0, sizeof(settings->menu_style));
                memset(settings->active_asset_pack, 0, sizeof(settings->active_asset_pack));
                desktop_settings_clear_lock_options(settings);
            }

        } else if(version == DESKTOP_SETTINGS_VER_14) {
            DesktopSettingsV14* settings_v14 = malloc(sizeof(DesktopSettingsV14));

            success = saved_struct_load(
                DESKTOP_SETTINGS_PATH,
                settings_v14,
                sizeof(DesktopSettingsV14),
                DESKTOP_SETTINGS_MAGIC,
                DESKTOP_SETTINGS_VER_14);

            if(success) {
                settings->auto_lock_delay_ms = settings_v14->auto_lock_delay_ms;
                settings->usb_inhibit_auto_lock = 0;
                settings->displayBatteryPercentage = settings_v14->displayBatteryPercentage;
                settings->dummy_mode = settings_v14->dummy_mode;
                settings->display_clock = settings_v14->display_clock;
                memcpy(
                    settings->favorite_apps,
                    settings_v14->favorite_apps,
                    sizeof(settings->favorite_apps));
                memcpy(
                    settings->dummy_apps, settings_v14->dummy_apps, sizeof(settings->dummy_apps));
                memset(settings->menu_style, 0, sizeof(settings->menu_style));
                memset(settings->active_asset_pack, 0, sizeof(settings->active_asset_pack));
                desktop_settings_clear_lock_options(settings);
            }

            free(settings_v14);
        }

    } while(false);

    if(!success) {
        FURI_LOG_W(TAG, "Failed to load file, using defaults");
        memset(settings, 0, sizeof(DesktopSettings));
        desktop_settings_save(settings);
    } else {
        desktop_settings_terminate_strings(settings);
    }
}

void desktop_settings_save(const DesktopSettings* settings) {
    furi_assert(settings);

    const bool success = saved_struct_save(
        DESKTOP_SETTINGS_PATH,
        settings,
        sizeof(DesktopSettings),
        DESKTOP_SETTINGS_MAGIC,
        DESKTOP_SETTINGS_VER);

    if(!success) {
        FURI_LOG_E(TAG, "Failed to save file");
    }
}
