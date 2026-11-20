#pragma once

#include "desktop.h"
#include "desktop_settings.h"

#include "animations/animation_manager.h"
#include "views/desktop_view_pin_timeout.h"
#include "views/desktop_view_pin_input.h"
#include "views/desktop_view_locked.h"
#include "views/desktop_view_main.h"
#include "views/desktop_view_control_centre.h"
#include "helpers/desktop_keybinds.h"
#include "helpers/desktop_keybinds_file.h"
#include "helpers/desktop_apps.h"
#include "views/desktop_view_debug.h"
#include "views/desktop_view_slideshow.h"

#include <gui/gui.h>
#include <gui/view_stack.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/popup.h>
#include <gui/scene_manager.h>

#include <loader/loader.h>
#include <notification/notification_app.h>

#define STATUS_BAR_Y_SHIFT 13

typedef enum {
    DesktopViewIdMain,
    DesktopViewIdControlCentre,
    DesktopViewIdLocked,
    DesktopViewIdDebug,
    DesktopViewIdPopup,
    DesktopViewIdPinInput,
    DesktopViewIdPinTimeout,
    DesktopViewIdSlideshow,
    DesktopViewIdTotal,
} DesktopViewId;

typedef struct {
    uint8_t hour;
    uint8_t minute;
    bool format_12; // 1 - 12 hour, 0 - 24H
} DesktopClock;

struct Desktop {
    FuriThread* scene_thread;

    Gui* gui;
    ViewDispatcher* view_dispatcher;
    SceneManager* scene_manager;

    Popup* popup;
    DesktopControlCentreView* control_centre;
    DesktopDebugView* debug_view;
    DesktopViewLocked* locked_view;
    DesktopMainView* main_view;
    DesktopViewPinTimeout* pin_timeout_view;
    DesktopSlideshowView* slideshow_view;
    DesktopViewPinInput* pin_input_view;

    ViewStack* main_view_stack;
    ViewStack* locked_view_stack;

    ViewPort* lock_icon_viewport;
    ViewPort* dummy_mode_icon_viewport;
    ViewPort* clock_viewport;
    ViewPort* stealth_mode_icon_viewport;

    Loader* loader;
    Storage* storage;
    NotificationApp* notification;

    FuriPubSub* status_pubsub;
    FuriPubSub* input_events_pubsub;
    FuriPubSubSubscription* input_events_subscription;

    FuriTimer* auto_lock_timer;
    FuriTimer* update_clock_timer;
    /** Ends the asset pack crash watch once the pack has been in use for a while. */
    FuriTimer* asset_pack_stable_timer;
    /** Tells the boot guard that this firmware starts and runs: see update_util/boot_guard.h */
    FuriTimer* boot_stable_timer;
    /** Writes the last crash's report once the SD card is ready (helpers/crash_report_save.h) */
    FuriTimer* crash_report_timer;

    AnimationManager* animation_manager;
    FuriSemaphore* animation_semaphore;

    DesktopClock clock;
    DesktopSettings settings;

    bool in_transition;
    bool asset_pack_guard_done;
    bool app_running;
    /** An app closed while the menu covered the desktop: its animation loads once in sight */
    bool animation_reload_pending;
    bool locked;
    /** The lock in place, or the next one, opens without the PIN (control centre's Lock) */
    bool keypad_lock;

    /** The control centre's app tiles: icons of apps on the SD card, read from their manifests */
    DesktopFapIcon control_app_icons[2];
    /** The apps the control centre's picker shows, while it is open */
    DesktopAppList control_apps;
};

void desktop_lock(Desktop* desktop);
void desktop_unlock(Desktop* desktop);
void desktop_set_dummy_mode_state(Desktop* desktop, bool enabled);
void desktop_set_stealth_mode_state(Desktop* desktop, bool enabled);
/** Show the screen inverted or not (DesktopSettings.dark_mode is the caller's to keep) */
void desktop_set_dark_mode(Desktop* desktop, bool enabled);
/** Whether locking now asks for the PIN to unlock: one is set and this is not a keypad lock */
bool desktop_lock_needs_pin(const Desktop* desktop);
/** What a button or a control centre tile is bound to (desktop_keybinds.h); false if unbound */
bool desktop_keybind_read(Desktop* desktop, DesktopKeybind bind, FuriString* out);
/** Do what a button or a control centre tile is bound to (desktop_keybinds.h); false if unbound */
bool desktop_keybind_run(Desktop* desktop, DesktopKeybind bind);
/** Load the idle animation an app's run left unloaded, if it is still waiting */
void desktop_animation_reload_if_pending(Desktop* desktop);
