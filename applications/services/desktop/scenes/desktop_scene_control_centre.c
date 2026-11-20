#include <furi.h>
#include <furi_hal.h>
#include <gui/scene_manager.h>
#include <gui/gui_i.h>
#include <loader/loader.h>
#include <bt/bt_service/bt_i.h>
#include <bt/bt_service/bt_settings_api_i.h>
#include <toolbox/path.h>
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

static bool desktop_scene_control_centre_bt_enabled(void) {
    Bt* bt = furi_record_open(RECORD_BT);
    const bool enabled = bt->bt_settings.enabled;
    furi_record_close(RECORD_BT);
    return enabled;
}

static void desktop_scene_control_centre_toggle_bt(void) {
    Bt* bt = furi_record_open(RECORD_BT);
    BtSettings settings = bt->bt_settings;
    settings.enabled = !settings.enabled;
    bt_set_settings(bt, &settings);
    furi_record_close(RECORD_BT);
}

/** An app tile's label: what it is bound to, without folders or ".fap" */
static void desktop_scene_control_centre_app_label(
    Desktop* desktop,
    DesktopKeybind bind,
    char* label,
    size_t size) {
    label[0] = '\0';
    FuriString* action = furi_string_alloc();
    if(desktop_keybind_read(desktop, bind, action)) {
        FuriString* name = furi_string_alloc();
        path_extract_filename(action, name, true);
        snprintf(label, size, "%s", furi_string_get_cstr(name));
        furi_string_free(name);
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
        memset(&state, 0, sizeof(state));
        state.on[ControlCentreTileBluetooth] = desktop_scene_control_centre_bt_enabled();
        state.on[ControlCentreTileStealth] = furi_hal_rtc_is_flag_set(FuriHalRtcFlagStealthMode);
        state.on[ControlCentreTileDark] = desktop->settings.dark_mode;
        state.on[ControlCentreTileLeftHanded] = furi_hal_rtc_is_flag_set(FuriHalRtcFlagHandOrient);
        state.on[ControlCentreTileDummy] = desktop->settings.dummy_mode;
        state.pin_set = desktop_pin_code_is_set();
        state.brightness = desktop->notification->settings.display_brightness;
        state.volume = desktop->notification->settings.speaker_volume;
        state.vibro = desktop->notification->settings.vibro_on;
        desktop_scene_control_centre_app_label(
            desktop, DesktopKeybindControl1, state.app_label[0], sizeof(state.app_label[0]));
        desktop_scene_control_centre_app_label(
            desktop, DesktopKeybindControl2, state.app_label[1], sizeof(state.app_label[1]));
    }
    desktop_scene_control_centre_time(desktop, &state);
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

    scene_manager_set_scene_state(
        desktop->scene_manager, DesktopSceneControlCentre, CONTROL_CENTRE_CLEAN);
    desktop_control_centre_set_callback(
        desktop->control_centre, desktop_scene_control_centre_callback, desktop);
    desktop_control_centre_reset_focus(desktop->control_centre);
    desktop_scene_control_centre_refresh(desktop, true);

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
        desktop->settings.dark_mode = !desktop->settings.dark_mode;
        desktop_settings_save(&desktop->settings);
        desktop_set_dark_mode(desktop, desktop->settings.dark_mode);
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
        const DesktopKeybind bind = tile == ControlCentreTileApp1 ? DesktopKeybindControl1 :
                                                                    DesktopKeybindControl2;
        desktop_scene_control_centre_close(desktop);
        // a tile with no app takes you to where one is chosen
        if(!desktop_keybind_run(desktop, bind)) {
            loader_start_detached_with_gui_error(desktop->loader, HADAL_SETTINGS_APP, NULL);
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
            settings->speaker_volume = state.volume;
            notification_message(desktop->notification, &sequence_volume_probe);
            scene_manager_set_scene_state(
                desktop->scene_manager, DesktopSceneControlCentre, CONTROL_CENTRE_DIRTY);
            break;
        case DesktopControlCentreEventVibro:
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
    desktop_scene_control_centre_save(desktop);
    desktop->gui->status_bar_hidden = false;
    gui_update(desktop->gui);
}
