#pragma once

#include <stdint.h>

#define DISPLAY_BATTERY_BAR              0
#define DISPLAY_BATTERY_PERCENT          1
#define DISPLAY_BATTERY_INVERTED_PERCENT 2
#define DISPLAY_BATTERY_RETRO_3          3
#define DISPLAY_BATTERY_RETRO_5          4
#define DISPLAY_BATTERY_BAR_PERCENT      5

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FavoriteAppLeftShort,
    FavoriteAppLeftLong,
    FavoriteAppRightShort,
    FavoriteAppRightLong,
    FavoriteAppOkLong,

    FavoriteAppNumber,
} FavoriteAppShortcut;

typedef enum {
    DummyAppLeftShort,
    DummyAppLeftLong,
    DummyAppRightShort,
    DummyAppRightLong,
    DummyAppUpLong,
    DummyAppDownShort,
    DummyAppDownLong,
    DummyAppOkShort,
    DummyAppOkLong,

    DummyAppNumber,
} DummyAppShortcut;

typedef struct {
    char name_or_path[128];
} FavoriteApp;

/** What happens after too many wrong PINs in a row */
typedef enum {
    DesktopPinFailActionNone,
    DesktopPinFailActionPowerOff,
    /** erase the internal storage (settings, PIN, pairings, keys); the SD card is kept */
    DesktopPinFailActionFactoryReset,
} DesktopPinFailAction;

typedef struct {
    uint32_t auto_lock_delay_ms;
    uint8_t usb_inhibit_auto_lock;
    uint8_t displayBatteryPercentage;
    uint8_t dummy_mode;
    uint8_t display_clock;
    FavoriteApp favorite_apps[FavoriteAppNumber];
    FavoriteApp dummy_apps[DummyAppNumber];
    char menu_style[32];
    /** Folder name under /ext/asset_packs, empty for the built-in assets. */
    char active_asset_pack[32];
    /** 1: do not lock at start even with a PIN set (0, the default, locks as before) */
    uint8_t boot_unlocked;
    /** show the time and date on the lock screen */
    uint8_t lock_clock;
    /** DesktopPinFailAction, after pin_fail_limit wrong PINs in a row */
    uint8_t pin_fail_action;
    uint8_t pin_fail_limit;
    /** light pixels on dark, the whole screen (v21) */
    uint8_t dark_mode;
    /** 1: the idle animation keeps playing while the backlight is off; 0 (the default) pauses
     * it then, the desktop's largest use of the battery while nobody looks (v22) */
    uint8_t animate_screen_off;
} DesktopSettings;

void desktop_settings_load(DesktopSettings* settings);
void desktop_settings_save(const DesktopSettings* settings);

#ifdef __cplusplus
}
#endif
