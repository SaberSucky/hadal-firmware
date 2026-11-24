#include <furi.h>
#include <furi_hal.h>
#include <applications.h>
#include <assets_icons.h>
#include <loader/loader.h>

#include "../desktop_i.h"
#include "../views/desktop_events.h"
#include "../views/desktop_view_main.h"
#include "../helpers/desktop_keybinds.h"
#include "desktop_scene.h"

#define TAG "DesktopSrv"

static void desktop_scene_main_new_idle_animation_callback(void* context) {
    furi_assert(context);
    Desktop* desktop = context;
    view_dispatcher_send_custom_event(
        desktop->view_dispatcher, DesktopAnimationEventNewIdleAnimation);
}

static void desktop_scene_main_check_animation_callback(void* context) {
    furi_assert(context);
    Desktop* desktop = context;
    view_dispatcher_send_custom_event(
        desktop->view_dispatcher, DesktopAnimationEventCheckAnimation);
}

static void desktop_scene_main_interact_animation_callback(void* context) {
    furi_assert(context);
    Desktop* desktop = context;
    view_dispatcher_send_custom_event(
        desktop->view_dispatcher, DesktopAnimationEventInteractAnimation);
}

#ifdef APP_ARCHIVE
static void
    desktop_switch_to_app(Desktop* desktop, const FlipperInternalApplication* flipper_app) {
    furi_assert(desktop);
    furi_assert(flipper_app);
    furi_assert(flipper_app->app);
    furi_assert(flipper_app->name);

    if(furi_thread_get_state(desktop->scene_thread) != FuriThreadStateStopped) {
        FURI_LOG_E("Desktop", "Thread is already running");
        return;
    }

    FuriHalRtcHeapTrackMode mode = furi_hal_rtc_get_heap_track_mode();
    if(mode > FuriHalRtcHeapTrackModeNone) {
        furi_thread_enable_heap_trace(desktop->scene_thread);
    } else {
        furi_thread_disable_heap_trace(desktop->scene_thread);
    }

    furi_thread_set_name(desktop->scene_thread, flipper_app->name);
    furi_thread_set_stack_size(desktop->scene_thread, flipper_app->stack_size);
    furi_thread_set_callback(desktop->scene_thread, flipper_app->app);

    furi_thread_start(desktop->scene_thread);
}
#endif

static inline bool desktop_scene_main_check_none(const char* str) {
    return (str[1] == '\0' && str[0] == '?');
}

static void desktop_scene_main_open_app_or_profile(Desktop* desktop, FavoriteApp* application) {
    bool load_ok = false;
    if(strlen(application->name_or_path) > 0) {
        if(!desktop_scene_main_check_none(application->name_or_path)) {
            // Load app
            loader_start_detached_with_gui_error(desktop->loader, application->name_or_path, NULL);
        }
        load_ok = true;
    }
    // In case of "default" setting
    if(!load_ok) {
        loader_start_detached_with_gui_error(desktop->loader, "Passport", NULL);
    }
}

static void desktop_scene_main_start_favorite(Desktop* desktop, FavoriteApp* application) {
    if(strlen(application->name_or_path) > 0) {
        if(!desktop_scene_main_check_none(application->name_or_path)) {
            loader_start_detached_with_gui_error(desktop->loader, application->name_or_path, NULL);
        }
    } else {
        loader_start_detached_with_gui_error(desktop->loader, LOADER_APPLICATIONS_NAME, NULL);
    }
}

/* ---- keybinds: what a button does, from the SD card (helpers/desktop_keybinds.h) ---- */

_Static_assert(
    DesktopMainEventKeybindEnd - DesktopMainEventKeybind == DesktopKeybindButtonCount,
    "an event for every button's keybind");

// The action bound to a button or tile, the file's last line for it; false if there is none
bool desktop_keybind_read(Desktop* desktop, DesktopKeybind bind, FuriString* out) {
    return desktop_keybinds_file_get(desktop->storage, bind, out);
}

// What a button did before keybinds: the event the scene already knows
static uint32_t desktop_scene_main_default_event(Desktop* desktop, DesktopKeybind bind) {
    const bool dummy = desktop->settings.dummy_mode;
    switch(bind) {
    case DesktopKeybindUpPress:
        return DesktopMainEventOpenLockMenu;
    case DesktopKeybindUpHold:
        return dummy ? DesktopDummyEventOpenUpLong : DesktopMainEventLock;
    case DesktopKeybindDownPress:
        return dummy ? DesktopDummyEventOpenDown : DesktopMainEventOpenArchive;
    case DesktopKeybindDownHold:
        return dummy ? DesktopDummyEventOpenDownLong : DesktopMainEventOpenDebug;
    case DesktopKeybindLeftPress:
        return dummy ? DesktopDummyEventOpenLeft : DesktopMainEventOpenFavoriteLeftShort;
    case DesktopKeybindLeftHold:
        return dummy ? DesktopDummyEventOpenLeftLong : DesktopMainEventOpenFavoriteLeftLong;
    case DesktopKeybindRightHold:
        return dummy ? DesktopDummyEventOpenRightLong : DesktopMainEventOpenFavoriteRightLong;
    case DesktopKeybindOkPress:
        return dummy ? DesktopDummyEventOpenOk : DesktopMainEventOpenMenu;
    case DesktopKeybindOkHold:
        if(dummy) return DesktopDummyEventOpenOkLong;
        return furi_hal_rtc_is_flag_set(FuriHalRtcFlagDebug) ?
                   DesktopAnimationEventNewIdleAnimation :
                   DesktopMainEventOpenFavoriteOkLong;
    default:
        // a short Right press is the animation's
        return DesktopAnimationEventInteractAnimation;
    }
}

// Do what the button (or control centre tile) is bound to; false if it is not bound
bool desktop_keybind_run(Desktop* desktop, DesktopKeybind bind) {
    FuriString* action = furi_string_alloc();
    const bool bound = desktop_keybind_read(desktop, bind, action);
    if(bound) {
        const char* text = furi_string_get_cstr(action);
        static const uint32_t events[] = {
            [DesktopKeybindActionMainMenu] = DesktopMainEventOpenMenu,
            [DesktopKeybindActionLockMenu] = DesktopMainEventOpenLockMenu,
            [DesktopKeybindActionLock] = DesktopMainEventLock,
            [DesktopKeybindActionLockKeypad] = DesktopMainEventLockKeypad,
            [DesktopKeybindActionArchive] = DesktopMainEventOpenArchive,
            [DesktopKeybindActionPowerOff] = DesktopMainEventOpenPowerOff,
        };
        const DesktopKeybindAction desktop_action = desktop_keybind_action(text);
        if(desktop_action == DesktopKeybindActionNothing) {
            // bound to nothing: the button does nothing
        } else if(desktop_action != DesktopKeybindActionNone) {
            view_dispatcher_send_custom_event(desktop->view_dispatcher, events[desktop_action]);
        } else if(text[0] != '/' || furi_string_end_withi_str(action, ".fap")) {
            // an app, by name or on the SD card
            loader_start_detached_with_gui_error(desktop->loader, text, NULL);
        } else {
            // a file: in the app it belongs to
            const char* app = desktop_keybind_app_for_file(text);
            if(app) loader_start_detached_with_gui_error(desktop->loader, app, text);
        }
    }
    furi_string_free(action);
    return bound;
}

void desktop_scene_main_callback(DesktopEvent event, void* context) {
    Desktop* desktop = (Desktop*)context;
    if(desktop->in_transition) return;
    view_dispatcher_send_custom_event(desktop->view_dispatcher, event);
}

void desktop_scene_main_on_enter(void* context) {
    Desktop* desktop = (Desktop*)context;
    DesktopMainView* main_view = desktop->main_view;

    animation_manager_set_context(desktop->animation_manager, desktop);
    animation_manager_set_new_idle_callback(
        desktop->animation_manager, desktop_scene_main_new_idle_animation_callback);
    animation_manager_set_check_callback(
        desktop->animation_manager, desktop_scene_main_check_animation_callback);
    animation_manager_set_interact_callback(
        desktop->animation_manager, desktop_scene_main_interact_animation_callback);

    desktop_main_set_callback(main_view, desktop_scene_main_callback, desktop);

    view_dispatcher_switch_to_view(desktop->view_dispatcher, DesktopViewIdMain);
}

bool desktop_scene_main_on_event(void* context, SceneManagerEvent event) {
    Desktop* desktop = (Desktop*)context;
    bool consumed = false;
    // whatever a key does next with the animation finds it loaded
    if(event.type == SceneManagerEventTypeCustom) desktop_animation_reload_if_pending(desktop);

    if(event.type == SceneManagerEventTypeCustom && event.event >= DesktopMainEventKeybind &&
       event.event < DesktopMainEventKeybindEnd) {
        const DesktopKeybind bind = event.event - DesktopMainEventKeybind;
        if(!desktop_keybind_run(desktop, bind)) {
            view_dispatcher_send_custom_event(
                desktop->view_dispatcher, desktop_scene_main_default_event(desktop, bind));
        }
        return true;
    }

    if(event.type == SceneManagerEventTypeCustom) {
        switch(event.event) {
        case DesktopMainEventOpenMenu: {
            Loader* loader = furi_record_open(RECORD_LOADER);
            loader_show_menu(loader);
            furi_record_close(RECORD_LOADER);
            consumed = true;
        } break;

        case DesktopMainEventLock:
            scene_manager_set_scene_state(desktop->scene_manager, DesktopSceneControlCentre, 0);
            desktop_lock(desktop);
            consumed = true;
            break;

        case DesktopMainEventLockKeypad:
            // as the control centre's tile: locked, no PIN asked to unlock
            scene_manager_set_scene_state(desktop->scene_manager, DesktopSceneControlCentre, 0);
            desktop->keypad_lock = true;
            desktop_lock(desktop);
            consumed = true;
            break;

        case DesktopMainEventOpenLockMenu:
            scene_manager_next_scene(desktop->scene_manager, DesktopSceneControlCentre);
            consumed = true;
            break;

        case DesktopMainEventOpenDebug:
            scene_manager_next_scene(desktop->scene_manager, DesktopSceneDebug);
            consumed = true;
            break;

        case DesktopMainEventOpenArchive:
#ifdef APP_ARCHIVE
            desktop_switch_to_app(desktop, &FLIPPER_ARCHIVE);
#endif
            consumed = true;
            break;

        case DesktopMainEventOpenPowerOff: {
            loader_start_detached_with_gui_error(desktop->loader, "Power", "off");
            consumed = true;
            break;
        }

        case DesktopMainEventOpenFavoriteLeftShort:
            desktop_scene_main_start_favorite(
                desktop, &desktop->settings.favorite_apps[FavoriteAppLeftShort]);
            consumed = true;
            break;
        case DesktopMainEventOpenFavoriteLeftLong:
            desktop_scene_main_start_favorite(
                desktop, &desktop->settings.favorite_apps[FavoriteAppLeftLong]);
            consumed = true;
            break;
        case DesktopMainEventOpenFavoriteRightShort:
            desktop_scene_main_start_favorite(
                desktop, &desktop->settings.favorite_apps[FavoriteAppRightShort]);
            consumed = true;
            break;
        case DesktopMainEventOpenFavoriteRightLong:
            desktop_scene_main_start_favorite(
                desktop, &desktop->settings.favorite_apps[FavoriteAppRightLong]);
            consumed = true;
            break;
        case DesktopMainEventOpenFavoriteOkLong:
            desktop_scene_main_start_favorite(
                desktop, &desktop->settings.favorite_apps[FavoriteAppOkLong]);
            consumed = true;
            break;

        case DesktopAnimationEventCheckAnimation:
            animation_manager_check_blocking_process(desktop->animation_manager);
            consumed = true;
            break;
        case DesktopAnimationEventNewIdleAnimation:
            animation_manager_new_idle_process(desktop->animation_manager);
            consumed = true;
            break;
        case DesktopAnimationEventInteractAnimation:
            // a short Right press: the binding, if there is one, comes before the animation
            if(desktop_keybind_run(desktop, DesktopKeybindRightPress)) {
                // done
            } else if(!animation_manager_interact_process(desktop->animation_manager)) {
                if(!desktop->settings.dummy_mode) {
                    desktop_scene_main_open_app_or_profile(
                        desktop, &desktop->settings.favorite_apps[FavoriteAppRightShort]);
                } else {
                    desktop_scene_main_open_app_or_profile(
                        desktop, &desktop->settings.dummy_apps[DummyAppRightShort]);
                }
            }
            consumed = true;
            break;

        case DesktopDummyEventOpenLeft:
            desktop_scene_main_open_app_or_profile(
                desktop, &desktop->settings.dummy_apps[DummyAppLeftShort]);
            break;
        case DesktopDummyEventOpenDown:
            desktop_scene_main_open_app_or_profile(
                desktop, &desktop->settings.dummy_apps[DummyAppDownShort]);
            break;
        case DesktopDummyEventOpenOk:
            desktop_scene_main_open_app_or_profile(
                desktop, &desktop->settings.dummy_apps[DummyAppOkShort]);
            break;
        case DesktopDummyEventOpenUpLong:
            if(!desktop_scene_main_check_none(
                   desktop->settings.dummy_apps[DummyAppUpLong].name_or_path)) {
                desktop_scene_main_open_app_or_profile(
                    desktop, &desktop->settings.dummy_apps[DummyAppUpLong]);
            } else {
                scene_manager_set_scene_state(desktop->scene_manager, DesktopSceneControlCentre, 0);
                desktop_lock(desktop);
            }
            break;
        case DesktopDummyEventOpenDownLong:
            desktop_scene_main_open_app_or_profile(
                desktop, &desktop->settings.dummy_apps[DummyAppDownLong]);
            break;
        case DesktopDummyEventOpenLeftLong:
            desktop_scene_main_open_app_or_profile(
                desktop, &desktop->settings.dummy_apps[DummyAppLeftLong]);
            break;
        case DesktopDummyEventOpenRightLong:
            desktop_scene_main_open_app_or_profile(
                desktop, &desktop->settings.dummy_apps[DummyAppRightLong]);
            break;
        case DesktopDummyEventOpenOkLong:
            desktop_scene_main_open_app_or_profile(
                desktop, &desktop->settings.dummy_apps[DummyAppOkLong]);
            break;

        case DesktopLockedEventUpdate:
            desktop_view_locked_update(desktop->locked_view);
            consumed = true;
            break;

        default:
            break;
        }
    }

    return consumed;
}

void desktop_scene_main_on_exit(void* context) {
    Desktop* desktop = (Desktop*)context;

    animation_manager_set_new_idle_callback(desktop->animation_manager, NULL);
    animation_manager_set_check_callback(desktop->animation_manager, NULL);
    animation_manager_set_interact_callback(desktop->animation_manager, NULL);
    animation_manager_set_context(desktop->animation_manager, desktop);
}
