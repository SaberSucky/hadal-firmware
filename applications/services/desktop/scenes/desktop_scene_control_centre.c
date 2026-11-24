#include <furi.h>
#include <furi_hal.h>
#include <gui/scene_manager.h>
#include <gui/gui_i.h>
#include <loader/loader.h>
#include <bt/bt_service/bt_i.h>
#include <bt/bt_service/bt_settings_api_i.h>
#include <locale/locale.h>

#include "../desktop_i.h"
#include "../helpers/pin_code.h"
#include "desktop_scene.h"

#define TAG "DesktopControlCentre"

#define HADAL_SETTINGS_APP  "Hadal"
#define DESKTOP_SETTINGS_APP "Desktop"

// Scene state: the notification settings changed and are not saved yet
#define CONTROL_CENTRE_CLEAN 0
#define CONTROL_CENTRE_DIRTY 1

// The same short beep LCD & Notifications plays, so the new volume can be judged by ear
static const NotificationSequence sequence_volume_probe = {
    &message_note_c5,
    &message_delay_100,
    &message_sound_off,
    NULL,
};

static void desktop_scene_control_centre_callback(DesktopEvent event, void* context) {
    Desktop* desktop = context;
    view_dispatcher_send_custom_event(desktop->view_dispatcher, event);
}

// Through the Bluetooth service's own calls: its settings are its thread's to read and change
static bool desktop_scene_control_centre_bt_enabled(void) {
    Bt* bt = furi_record_open(RECORD_BT);
    BtSettings settings;
    bt_get_settings(bt, &settings);
    furi_record_close(RECORD_BT);
    return settings.enabled;
}

static void desktop_scene_control_centre_toggle_bt(void) {
    Bt* bt = furi_record_open(RECORD_BT);
    BtSettings settings;
    bt_get_settings(bt, &settings);
    settings.enabled = !settings.enabled;
    bt_set_settings(bt, &settings);
    furi_record_close(RECORD_BT);
}

static DesktopKeybind desktop_scene_control_centre_bind(uint8_t slot) {
    return slot ? DesktopKeybindControl2 : DesktopKeybindControl1;
}

/** An app tile's label and icon: the app it is bound to, by name, with the app's own icon */
static void
    desktop_scene_control_centre_app(Desktop* desktop, uint8_t slot, ControlCentreState* state) {
    state->app_label[slot][0] = '\0';
    state->app_icon[slot] = NULL;
    FuriString* action = furi_string_alloc();
    if(desktop_keybind_read(desktop, desktop_scene_control_centre_bind(slot), action) &&
       !furi_string_empty(action)) {
        FuriString* label = furi_string_alloc();
        state->app_icon[slot] = desktop_app_describe(
            furi_string_get_cstr(action), label, &desktop->control_app_icons[slot]);
        snprintf(
            state->app_label[slot], sizeof(state->app_label[slot]), "%s", furi_string_get_cstr(label));
        if(!state->app_label[slot][0]) state->app_label[slot][0] = '?';
        furi_string_free(label);
    }
    furi_string_free(action);
}

static void desktop_scene_control_centre_time(Desktop* desktop, ControlCentreState* state) {
    DateTime now;
    furi_hal_rtc_get_datetime(&now);
    state->hour = now.hour;
    state->minute = now.minute;
    state->format_12 = locale_get_time_format() == LocaleTimeFormat12h;
    state->battery = furi_hal_power_get_pct();
    state->charging = furi_hal_power_is_charging();
    UNUSED(desktop);
}

static void desktop_scene_control_centre_refresh(Desktop* desktop, bool everything) {
    ControlCentreState state;
    desktop_control_centre_get_state(desktop->control_centre, &state);
    if(everything) {
        // the app tiles stay as they were read: see desktop_scene_control_centre_load_apps()
        ControlCentreState apps = state;
        memset(&state, 0, sizeof(state));
        memcpy(state.app_label, apps.app_label, sizeof(state.app_label));
        memcpy(state.app_icon, apps.app_icon, sizeof(state.app_icon));
        state.on[ControlCentreTileBluetooth] = desktop_scene_control_centre_bt_enabled();
        state.on[ControlCentreTileStealth] = furi_hal_rtc_is_flag_set(FuriHalRtcFlagStealthMode);
        state.on[ControlCentreTileDark] = desktop_dark_mode(desktop);
        state.on[ControlCentreTileLeftHanded] = furi_hal_rtc_is_flag_set(FuriHalRtcFlagHandOrient);
        state.on[ControlCentreTileDummy] = desktop->settings.dummy_mode;
        state.pin_set = desktop_pin_code_is_set();
        state.brightness = desktop->notification->settings.display_brightness;
        state.volume = desktop->notification->settings.speaker_volume;
        state.vibro = desktop->notification->settings.vibro_on;
    }
    desktop_scene_control_centre_time(desktop, &state);
    desktop_control_centre_set_state(desktop->control_centre, &state);
}

/** Read what the app tiles are bound to, with their apps' icons: this reads the SD card, so it is
 * done when the panel opens and when a tile gets a new app, not on every refresh */
static void desktop_scene_control_centre_load_apps(Desktop* desktop) {
    ControlCentreState state;
    desktop_control_centre_get_state(desktop->control_centre, &state);
    desktop_scene_control_centre_app(desktop, 0, &state);
    desktop_scene_control_centre_app(desktop, 1, &state);
    desktop_control_centre_set_state(desktop->control_centre, &state);
}

static void desktop_scene_control_centre_save(Desktop* desktop) {
    if(scene_manager_get_scene_state(desktop->scene_manager, DesktopSceneControlCentre) ==
       CONTROL_CENTRE_DIRTY) {
        scene_manager_set_scene_state(
            desktop->scene_manager, DesktopSceneControlCentre, CONTROL_CENTRE_CLEAN);
        notification_message_save_settings(desktop->notification);
    }
}

void desktop_scene_control_centre_on_enter(void* context) {
    Desktop* desktop = context;
    desktop->control_centre_open = true; // refreshed on the desktop's tick
    desktop_tick_update(desktop);

    scene_manager_set_scene_state(
        desktop->scene_manager, DesktopSceneControlCentre, CONTROL_CENTRE_CLEAN);
    desktop_control_centre_set_callback(
        desktop->control_centre, desktop_scene_control_centre_callback, desktop);
    desktop_control_centre_reset_focus(desktop->control_centre);
    desktop_scene_control_centre_refresh(desktop, true);
    desktop_scene_control_centre_load_apps(desktop);

    // the panel has its own clock and battery: the whole screen is its
    desktop->gui->status_bar_hidden = true;
    view_dispatcher_switch_to_view(desktop->view_dispatcher, DesktopViewIdControlCentre);
}

/** Close the panel and go back to the desktop, before doing something there */
static void desktop_scene_control_centre_close(Desktop* desktop) {
    scene_manager_search_and_switch_to_previous_scene(desktop->scene_manager, DesktopSceneMain);
}

static void desktop_scene_control_centre_open_app(Desktop* desktop, const char* app) {
    desktop_scene_control_centre_close(desktop);
    loader_start_detached_with_gui_error(desktop->loader, app, NULL);
}

/** Open the app picker for an app tile, on the app it has now */
static void desktop_scene_control_centre_open_picker(Desktop* desktop, ControlCentreTile tile) {
    const uint8_t slot = tile - ControlCentreTileApp1;
    desktop_app_list_clear(&desktop->control_apps);
    desktop_app_list_load(&desktop->control_apps);

    FuriString* action = furi_string_alloc();
    size_t index = 1; // the first app, for a tile with none
    if(desktop_keybind_read(desktop, desktop_scene_control_centre_bind(slot), action)) {
        index = desktop_app_list_find(&desktop->control_apps, furi_string_get_cstr(action));
    }
    furi_string_free(action);
    if(index >= desktop->control_apps.count) index = 0;

    desktop_control_centre_open_picker(
        desktop->control_centre,
        slot,
        desktop->control_apps.items,
        desktop->control_apps.count,
        index);
}

static void desktop_scene_control_centre_close_picker(Desktop* desktop) {
    desktop_control_centre_close_picker(desktop->control_centre);
    desktop_app_list_clear(&desktop->control_apps);
}

/** Bind the tile the picker was for to the app chosen in it */
static void desktop_scene_control_centre_pick(Desktop* desktop) {
    uint8_t slot = 0;
    const size_t index = desktop_control_centre_get_pick(desktop->control_centre, &slot);
    if(index < desktop->control_apps.count) {
        const DesktopApp* app = &desktop->control_apps.items[index];
        if(!desktop_keybinds_file_set(
               desktop->storage, desktop_scene_control_centre_bind(slot), app->action)) {
            FURI_LOG_E(TAG, "Cannot save the app tile");
        }
    }
    desktop_scene_control_centre_close_picker(desktop);
    desktop_scene_control_centre_load_apps(desktop);
}

static void desktop_scene_control_centre_tile(Desktop* desktop, ControlCentreTile tile) {
    switch(tile) {
    case ControlCentreTileLock:
        desktop_scene_control_centre_close(desktop);
        desktop->keypad_lock = true;
        desktop_lock(desktop);
        break;
    case ControlCentreTilePinLock:
        if(desktop_pin_code_is_set()) {
            desktop_scene_control_centre_close(desktop);
            desktop->keypad_lock = false;
            desktop_lock(desktop);
        } else {
            desktop_scene_control_centre_open_app(desktop, DESKTOP_SETTINGS_APP);
        }
        break;
    case ControlCentreTileBluetooth:
        desktop_scene_control_centre_toggle_bt();
        break;
    case ControlCentreTileStealth:
        desktop_set_stealth_mode_state(
            desktop, !furi_hal_rtc_is_flag_set(FuriHalRtcFlagStealthMode));
        break;
    case ControlCentreTileDark:
        desktop_set_dark_mode(desktop, !desktop_dark_mode(desktop));
        break;
    case ControlCentreTileLeftHanded:
        if(furi_hal_rtc_is_flag_set(FuriHalRtcFlagHandOrient)) {
            furi_hal_rtc_reset_flag(FuriHalRtcFlagHandOrient);
        } else {
            furi_hal_rtc_set_flag(FuriHalRtcFlagHandOrient);
        }
        gui_update(desktop->gui);
        break;
    case ControlCentreTileHadal:
        desktop_scene_control_centre_open_app(desktop, HADAL_SETTINGS_APP);
        break;
    case ControlCentreTileApp1:
    case ControlCentreTileApp2: {
        const DesktopKeybind bind =
            desktop_scene_control_centre_bind(tile - ControlCentreTileApp1);
        FuriString* action = furi_string_alloc();
        const bool bound = desktop_keybind_read(desktop, bind, action) &&
                           !furi_string_empty(action);
        furi_string_free(action);
        if(bound) {
            desktop_scene_control_centre_close(desktop);
            desktop_keybind_run(desktop, bind);
        } else {
            // a tile with no app: choose one, right here
            desktop_scene_control_centre_open_picker(desktop, tile);
        }
    } break;
    case ControlCentreTileDummy:
        desktop_set_dummy_mode_state(desktop, !desktop->settings.dummy_mode);
        break;
    default:
        break;
    }
}

bool desktop_scene_control_centre_on_event(void* context, SceneManagerEvent event) {
    Desktop* desktop = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeTick) {
        // the clock and the battery move on while the panel is open
        desktop_scene_control_centre_refresh(desktop, false);
    } else if(event.type == SceneManagerEventTypeCustom) {
        ControlCentreState state;
        desktop_control_centre_get_state(desktop->control_centre, &state);
        NotificationSettings* settings = &desktop->notification->settings;
        consumed = true;

        switch(event.event) {
        case DesktopControlCentreEventTile: {
            const ControlCentreTile tile = desktop_control_centre_get_tile(desktop->control_centre);
            desktop_scene_control_centre_tile(desktop, tile);
            // still open after a switch: show where it now stands
            if(scene_manager_get_current_scene(desktop->scene_manager) ==
               DesktopSceneControlCentre) {
                desktop_scene_control_centre_refresh(desktop, true);
            }
        } break;
        // Every change goes into the live notification settings at once and is shown or
        // played, as LCD & Notifications does; they are saved when the panel closes
        case DesktopControlCentreEventBrightness:
            settings->display_brightness = state.brightness;
            notification_message(desktop->notification, &sequence_display_backlight_force_on);
            scene_manager_set_scene_state(
                desktop->scene_manager, DesktopSceneControlCentre, CONTROL_CENTRE_DIRTY);
            break;
        case DesktopControlCentreEventVolume:
            // Silent leaves the sound as it is, as LCD & Notifications does
            if(furi_hal_rtc_is_flag_set(FuriHalRtcFlagStealthMode)) {
                desktop_scene_control_centre_refresh(desktop, true);
                break;
            }
            settings->speaker_volume = state.volume;
            notification_message(desktop->notification, &sequence_volume_probe);
            scene_manager_set_scene_state(
                desktop->scene_manager, DesktopSceneControlCentre, CONTROL_CENTRE_DIRTY);
            break;
        case DesktopControlCentreEventChangeApp:
            desktop_scene_control_centre_open_picker(
                desktop, desktop_control_centre_get_tile(desktop->control_centre));
            break;
        case DesktopControlCentreEventPick:
            desktop_scene_control_centre_pick(desktop);
            break;
        case DesktopControlCentreEventPickCancel:
            desktop_scene_control_centre_close_picker(desktop);
            break;
        case DesktopControlCentreEventVibro:
            if(furi_hal_rtc_is_flag_set(FuriHalRtcFlagStealthMode)) {
                desktop_scene_control_centre_refresh(desktop, true);
                break;
            }
            settings->vibro_on = state.vibro;
            if(state.vibro) notification_message(desktop->notification, &sequence_single_vibro);
            scene_manager_set_scene_state(
                desktop->scene_manager, DesktopSceneControlCentre, CONTROL_CENTRE_DIRTY);
            break;
        default:
            consumed = false;
            break;
        }
    }
    return consumed;
}

void desktop_scene_control_centre_on_exit(void* context) {
    Desktop* desktop = context;
    desktop->control_centre_open = false;
    desktop_tick_update(desktop);
    desktop_scene_control_centre_close_picker(desktop);
    desktop_scene_control_centre_save(desktop);
    desktop->gui->status_bar_hidden = false;
    gui_update(desktop->gui);
}
