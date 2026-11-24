/*
 * Host test of applications/services/desktop/desktop_settings.c, driven by
 * scripts/asset_packer_test.py: the real load/migrate code and the real saved_struct, on host
 * storage. Files of every older version are written from the layouts those versions had.
 *
 *   migrate ROOT VERSION [CASE]   write /int/.desktop.settings as VERSION (14, 17-22), load
 *                                 it with the current code and print what came out
 *
 * CASE: "" (normal), "unterminated" (string fields full of 0xFF), "badsum" (checksum wrong),
 *       "badaction" (v20: a wrong-PIN action the firmware does not know)
 */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <furi.h>
#include <saved_struct.h>
#include <desktop/desktop_settings.h>

#define PATH  "/int/.desktop.settings"
#define MAGIC 0x17

// The layouts the older versions wrote. They are history: never edit them.
typedef struct {
    uint32_t auto_lock_delay_ms;
    uint8_t displayBatteryPercentage;
    uint8_t dummy_mode;
    uint8_t display_clock;
    FavoriteApp favorite_apps[FavoriteAppNumber];
    FavoriteApp dummy_apps[DummyAppNumber];
} SettingsV14;

typedef struct {
    uint32_t auto_lock_delay_ms;
    uint8_t usb_inhibit_auto_lock;
    uint8_t displayBatteryPercentage;
    uint8_t dummy_mode;
    uint8_t display_clock;
    FavoriteApp favorite_apps[FavoriteAppNumber];
    FavoriteApp dummy_apps[DummyAppNumber];
} SettingsV17;

typedef struct {
    SettingsV17 v17;
    char menu_style[32];
} SettingsV18;

extern const char* host_storage_root;

static void fill_v17(SettingsV17* s) {
    memset(s, 0, sizeof(*s));
    s->auto_lock_delay_ms = 90000;
    s->usb_inhibit_auto_lock = 1;
    s->displayBatteryPercentage = 3;
    s->dummy_mode = 1;
    s->display_clock = 1;
    strcpy(s->favorite_apps[0].name_or_path, "/ext/apps/Tools/example.fap");
    strcpy(s->favorite_apps[FavoriteAppNumber - 1].name_or_path, "NFC");
    strcpy(s->dummy_apps[DummyAppNumber - 1].name_or_path, "Snake Game");
}

int main(int argc, char** argv) {
    if(argc < 4 || strcmp(argv[1], "migrate") != 0) return 2;
    host_storage_root = argv[2];
    const int version = atoi(argv[3]);
    const char* variant = argc > 4 ? argv[4] : "";

    SettingsV17 v17;
    fill_v17(&v17);
    bool ok = false;
    if(version == 14) {
        SettingsV14 v14 = {
            .auto_lock_delay_ms = v17.auto_lock_delay_ms,
            .displayBatteryPercentage = v17.displayBatteryPercentage,
            .dummy_mode = v17.dummy_mode,
            .display_clock = v17.display_clock};
        memcpy(v14.favorite_apps, v17.favorite_apps, sizeof(v14.favorite_apps));
        memcpy(v14.dummy_apps, v17.dummy_apps, sizeof(v14.dummy_apps));
        ok = saved_struct_save(PATH, &v14, sizeof(v14), MAGIC, 14);
    } else if(version == 17) {
        ok = saved_struct_save(PATH, &v17, sizeof(v17), MAGIC, 17);
    } else if(version == 18) {
        SettingsV18 v18 = {.v17 = v17};
        strcpy(v18.menu_style, "Test.fal");
        ok = saved_struct_save(PATH, &v18, sizeof(v18), MAGIC, 18);
    } else if(version >= 19 && version <= 22) {
        DesktopSettings current;
        memset(&current, 0, sizeof(current));
        memcpy(&current, &v17, sizeof(v17)); // the same leading layout
        strcpy(current.menu_style, "Test.fal");
        strcpy(current.active_asset_pack, "Hadal");
        if(!strcmp(variant, "unterminated")) {
            memset(current.menu_style, 0xFF, sizeof(current.menu_style));
            memset(current.active_asset_pack, 0xFF, sizeof(current.active_asset_pack));
        }
        if(version == 19) {
            // v19 ended where the lock options begin
            ok = saved_struct_save(
                PATH, &current, offsetof(DesktopSettings, boot_unlocked), MAGIC, 19);
        } else {
            current.boot_unlocked = 1;
            current.lock_clock = 1;
            current.pin_fail_action = !strcmp(variant, "badaction") ? 7 : 2;
            current.pin_fail_limit = 5;
            if(version == 20) {
                // v20 ended where dark mode begins
                ok = saved_struct_save(
                    PATH, &current, offsetof(DesktopSettings, dark_mode), MAGIC, 20);
            } else if(version == 21) {
                // v21 ended where the animation option begins
                current.dark_mode = !strcmp(variant, "baddark") ? 9 : 1;
                ok = saved_struct_save(
                    PATH, &current, offsetof(DesktopSettings, animate_screen_off), MAGIC, 21);
            } else {
                current.dark_mode = !strcmp(variant, "baddark") ? 9 : 1;
                current.animate_screen_off = !strcmp(variant, "badanim") ? 9 : 1;
                ok = saved_struct_save(PATH, &current, sizeof(current), MAGIC, 22);
            }
        }
    }
    if(!ok) return 3;

    if(!strcmp(variant, "badsum")) {
        char path[512];
        snprintf(path, sizeof(path), "%s%s", host_storage_root, PATH);
        FILE* f = fopen(path, "r+b");
        fseek(f, 2, SEEK_SET); // the checksum byte of the header
        int sum = fgetc(f);
        fseek(f, 2, SEEK_SET);
        fputc(sum ^ 0x55, f);
        fclose(f);
    }

    DesktopSettings loaded;
    memset(&loaded, 0xA5, sizeof(loaded)); // so anything the loader forgets to set shows up
    desktop_settings_load(&loaded);

    printf("auto_lock %u\n", (unsigned)loaded.auto_lock_delay_ms);
    printf("battery %u\n", loaded.displayBatteryPercentage);
    printf("clock %u\n", loaded.display_clock);
    printf("usb %u\n", loaded.usb_inhibit_auto_lock);
    printf("favorite0 %s\n", loaded.favorite_apps[0].name_or_path);
    printf("dummy_last %s\n", loaded.dummy_apps[DummyAppNumber - 1].name_or_path);
    printf("menu_style_len %zu\n", strnlen(loaded.menu_style, sizeof(loaded.menu_style)));
    printf("menu_style %s\n", loaded.menu_style);
    printf("pack_len %zu\n", strnlen(loaded.active_asset_pack, sizeof(loaded.active_asset_pack)));
    printf("pack %s\n", loaded.active_asset_pack);
    printf(
        "lock %u %u %u %u\n",
        loaded.boot_unlocked,
        loaded.lock_clock,
        loaded.pin_fail_action,
        loaded.pin_fail_limit);
    printf("dark %u\n", loaded.dark_mode);
    printf("animate %u\n", loaded.animate_screen_off);
    return 0;
}
