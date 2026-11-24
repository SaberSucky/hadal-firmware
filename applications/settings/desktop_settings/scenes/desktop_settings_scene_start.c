#include <applications.h>
#include <lib/toolbox/value_index.h>

#include "../desktop_settings_app.h"
#include "desktop_settings_scene.h"
#include "desktop_settings_scene_i.h"
#include <power/power_service/power.h>
#include <loader/loader.h>

typedef enum {
    DesktopSettingsPinSetup = 0,
    DesktopSettingsAutoLockDelay,
    DesktopSettingsAutoPowerOff,
    DesktopSettingsBatteryDisplay,
    DesktopSettingsClockDisplay,
    DesktopSettingsAnimateScreenOff,
    DesktopSettingsChangeName,
    DesktopSettingsHappyMode,
    DesktopSettingsButtons,
    DesktopSettingsDummyLeft,
    DesktopSettingsDummyLeftLong,
    DesktopSettingsDummyRight,
    DesktopSettingsDummyRightLong,
    DesktopSettingsDummyUpLong,
    DesktopSettingsDummyDown,
    DesktopSettingsDummyDownLong,
    DesktopSettingsDummyOk,
    DesktopSettingsDummyOkLong,
} DesktopSettingsEntry;

#define AUTO_LOCK_DELAY_COUNT 9
const char* const auto_lock_delay_text[AUTO_LOCK_DELAY_COUNT] = {
    "OFF",
    "10s",
    "15s",
    "30s",
    "60s",
    "90s",
    "2min",
    "5min",
    "10min",
};

const uint32_t auto_lock_delay_value[AUTO_LOCK_DELAY_COUNT] =
    {0, 10000, 15000, 30000, 60000, 90000, 120000, 300000, 600000};

#define USB_INHIBIT_AUTO_LOCK_DELAY_COUNT 2

const char* const usb_inhibit_auto_lock_delay_text[USB_INHIBIT_AUTO_LOCK_DELAY_COUNT] = {
    "OFF",
    "ON",
};

const uint32_t usb_inhibit_auto_lock_delay_value[USB_INHIBIT_AUTO_LOCK_DELAY_COUNT] = {0, 1};

#define CLOCK_ENABLE_COUNT 2
const char* const clock_enable_text[CLOCK_ENABLE_COUNT] = {
    "OFF",
    "ON",
};

const uint32_t clock_enable_value[CLOCK_ENABLE_COUNT] = {0, 1};

#define BATTERY_VIEW_COUNT 6

const char* const battery_view_count_text[BATTERY_VIEW_COUNT] =
    {"Bar", "%", "Inv. %", "Retro 3", "Retro 5", "Bar %"};

const uint32_t displayBatteryPercentage_value[BATTERY_VIEW_COUNT] = {
    DISPLAY_BATTERY_BAR,
    DISPLAY_BATTERY_PERCENT,
    DISPLAY_BATTERY_INVERTED_PERCENT,
    DISPLAY_BATTERY_RETRO_3,
    DISPLAY_BATTERY_RETRO_5,
    DISPLAY_BATTERY_BAR_PERCENT};

static void desktop_settings_scene_start_var_list_enter_callback(void* context, uint32_t index) {
    DesktopSettingsApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

static void desktop_settings_scene_start_battery_view_changed(VariableItem* item) {
    DesktopSettingsApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);

    variable_item_set_current_value_text(item, battery_view_count_text[index]);
    app->settings.displayBatteryPercentage = index;
}

static void desktop_settings_scene_start_clock_enable_changed(VariableItem* item) {
    DesktopSettingsApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);

    variable_item_set_current_value_text(item, clock_enable_text[index]);
    app->settings.display_clock = index;
}

// The idle animation plays only while the screen is lit (the default: it saves the battery), or
// always
static const char* const animate_screen_off_text[] = {"Screen on", "Always"};

static void desktop_settings_scene_start_animate_screen_off_changed(VariableItem* item) {
    DesktopSettingsApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);

    variable_item_set_current_value_text(item, animate_screen_off_text[index]);
    app->settings.animate_screen_off = index;
}

static void desktop_settings_scene_start_auto_lock_delay_changed(VariableItem* item) {
    DesktopSettingsApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);

    variable_item_set_current_value_text(item, auto_lock_delay_text[index]);
    app->settings.auto_lock_delay_ms = auto_lock_delay_value[index];
    // "not on USB" means nothing while it never locks by itself
    variable_item_set_hidden(
        variable_item_list_get(app->variable_item_list, DesktopSettingsAutoLockDelay + 1),
        index == 0);
}

static void desktop_settings_scene_start_usb_inhibit_auto_lock_delay_changed(VariableItem* item) {
    DesktopSettingsApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);

    variable_item_set_current_value_text(item, usb_inhibit_auto_lock_delay_text[index]);
    app->settings.usb_inhibit_auto_lock = usb_inhibit_auto_lock_delay_value[index];
}

void desktop_settings_scene_start_on_enter(void* context) {
    DesktopSettingsApp* app = context;
    VariableItemList* variable_item_list = app->variable_item_list;

    VariableItem* item;
    uint8_t value_index;

    variable_item_list_add(variable_item_list, "PIN Setup", 1, NULL, NULL);

    item = variable_item_list_add(
        variable_item_list,
        "Auto Lock Time",
        AUTO_LOCK_DELAY_COUNT,
        desktop_settings_scene_start_auto_lock_delay_changed,
        app);

    value_index = value_index_uint32(
        app->settings.auto_lock_delay_ms, auto_lock_delay_value, AUTO_LOCK_DELAY_COUNT);
    variable_item_set_current_value_index(item, value_index);
    variable_item_set_current_value_text(item, auto_lock_delay_text[value_index]);
    variable_item_set_values_text(item, auto_lock_delay_text);

    const bool never_locks = value_index == 0;
    // USB connection Inhibit autolock OFF|ON|with opened RPC session
    item = variable_item_list_add(
        variable_item_list,
        "Not on USB",
        USB_INHIBIT_AUTO_LOCK_DELAY_COUNT,
        desktop_settings_scene_start_usb_inhibit_auto_lock_delay_changed,
        app);

    value_index = value_index_uint32(
        app->settings.usb_inhibit_auto_lock,
        usb_inhibit_auto_lock_delay_value,
        USB_INHIBIT_AUTO_LOCK_DELAY_COUNT);
    variable_item_set_current_value_index(item, value_index);
    variable_item_set_current_value_text(item, usb_inhibit_auto_lock_delay_text[value_index]);
    variable_item_set_values_text(item, usb_inhibit_auto_lock_delay_text);
    variable_item_set_indent(item, 1);
    variable_item_set_hidden(item, never_locks);

    item = variable_item_list_add(
        variable_item_list,
        "Battery View",
        BATTERY_VIEW_COUNT,
        desktop_settings_scene_start_battery_view_changed,
        app);

    value_index = value_index_uint32(
        app->settings.displayBatteryPercentage,
        displayBatteryPercentage_value,
        BATTERY_VIEW_COUNT);
    variable_item_set_current_value_index(item, value_index);
    variable_item_set_current_value_text(item, battery_view_count_text[value_index]);
    variable_item_set_values_text(item, battery_view_count_text);

    item = variable_item_list_add(
        variable_item_list,
        "Show Clock",
        CLOCK_ENABLE_COUNT,
        desktop_settings_scene_start_clock_enable_changed,
        app);

    value_index =
        value_index_uint32(app->settings.display_clock, clock_enable_value, CLOCK_ENABLE_COUNT);
    variable_item_set_current_value_index(item, value_index);
    variable_item_set_current_value_text(item, clock_enable_text[value_index]);
    variable_item_set_values_text(item, clock_enable_text);

    item = variable_item_list_add(
        variable_item_list,
        "Animation",
        COUNT_OF(animate_screen_off_text),
        desktop_settings_scene_start_animate_screen_off_changed,
        app);
    value_index = app->settings.animate_screen_off ? 1 : 0;
    variable_item_set_current_value_index(item, value_index);
    variable_item_set_current_value_text(item, animate_screen_off_text[value_index]);
    variable_item_set_values_text(item, animate_screen_off_text);

    // the menu style and the asset pack are set in Settings > Hadal > Interface

    variable_item_list_add(variable_item_list, "Change Flipper Name", 0, NULL, app);

    variable_item_list_add(variable_item_list, "Happy Mode", 1, NULL, NULL);

    // what each button does is set in one place, Hadal > Desktop Keys: this opens it
    variable_item_list_add(variable_item_list, "Buttons...", 1, NULL, NULL);

    variable_item_list_add(variable_item_list, "Dummy: Left", 1, NULL, NULL);
    variable_item_list_add(variable_item_list, "Dummy: Left hold", 1, NULL, NULL);
    variable_item_list_add(variable_item_list, "Dummy: Right", 1, NULL, NULL);
    variable_item_list_add(variable_item_list, "Dummy: Right hold", 1, NULL, NULL);
    variable_item_list_add(variable_item_list, "Dummy: Up hold", 1, NULL, NULL);
    variable_item_list_add(variable_item_list, "Dummy: Down", 1, NULL, NULL);
    variable_item_list_add(variable_item_list, "Dummy: Down hold", 1, NULL, NULL);
    variable_item_list_add(variable_item_list, "Dummy: OK", 1, NULL, NULL);
    variable_item_list_add(variable_item_list, "Dummy: OK hold", 1, NULL, NULL);

    variable_item_list_set_enter_callback(
        variable_item_list, desktop_settings_scene_start_var_list_enter_callback, app);

    // back from Desktop Keys: where it was left
    if(app->open_at_buttons) {
        app->open_at_buttons = false;
        variable_item_list_set_selected_item(variable_item_list, DesktopSettingsButtons);
    }

    view_dispatcher_switch_to_view(app->view_dispatcher, DesktopSettingsAppViewVarItemList);
}

bool desktop_settings_scene_start_on_event(void* context, SceneManagerEvent event) {
    DesktopSettingsApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        switch(event.event) {
        case DesktopSettingsPinSetup:
            scene_manager_next_scene(app->scene_manager, DesktopSettingsAppScenePinMenu);
            break;

            // case DesktopSettingsAutoLockDelay:
            // case DesktopSettingsBatteryDisplay:
            // case DesktopSettingsClockDisplay:
            // Proces in default

        case DesktopSettingsChangeName:
            scene_manager_next_scene(app->scene_manager, DesktopSettingsAppSceneChangeName);
            break;

        case DesktopSettingsButtons: {
            // set where every button and tile is set; the Desktop page closes for it
            Loader* loader = furi_record_open(RECORD_LOADER);
            loader_enqueue_launch(loader, "Hadal", "desktop_keys", LoaderDeferredLaunchFlagGui);
            // and Back there comes back here, on this item
            loader_enqueue_launch(
                loader, "Desktop", DESKTOP_SETTINGS_ARG_BUTTONS, LoaderDeferredLaunchFlagGui);
            furi_record_close(RECORD_LOADER);
            scene_manager_stop(app->scene_manager);
            view_dispatcher_stop(app->view_dispatcher);
            break;
        }

        case DesktopSettingsDummyLeft:
            scene_manager_set_scene_state(
                app->scene_manager,
                DesktopSettingsAppSceneFavorite,
                SCENE_STATE_SET_DUMMY_APP | DummyAppLeftShort);
            scene_manager_next_scene(app->scene_manager, DesktopSettingsAppSceneFavorite);
            break;
        case DesktopSettingsDummyLeftLong:
            scene_manager_set_scene_state(
                app->scene_manager,
                DesktopSettingsAppSceneFavorite,
                SCENE_STATE_SET_DUMMY_APP | DummyAppLeftLong);
            scene_manager_next_scene(app->scene_manager, DesktopSettingsAppSceneFavorite);
            break;
        case DesktopSettingsDummyRight:
            scene_manager_set_scene_state(
                app->scene_manager,
                DesktopSettingsAppSceneFavorite,
                SCENE_STATE_SET_DUMMY_APP | DummyAppRightShort);
            scene_manager_next_scene(app->scene_manager, DesktopSettingsAppSceneFavorite);
            break;
        case DesktopSettingsDummyRightLong:
            scene_manager_set_scene_state(
                app->scene_manager,
                DesktopSettingsAppSceneFavorite,
                SCENE_STATE_SET_DUMMY_APP | DummyAppRightLong);
            scene_manager_next_scene(app->scene_manager, DesktopSettingsAppSceneFavorite);
            break;
        case DesktopSettingsDummyUpLong:
            scene_manager_set_scene_state(
                app->scene_manager,
                DesktopSettingsAppSceneFavorite,
                SCENE_STATE_SET_DUMMY_APP | DummyAppUpLong);
            scene_manager_next_scene(app->scene_manager, DesktopSettingsAppSceneFavorite);
            break;
        case DesktopSettingsDummyDown:
            scene_manager_set_scene_state(
                app->scene_manager,
                DesktopSettingsAppSceneFavorite,
                SCENE_STATE_SET_DUMMY_APP | DummyAppDownShort);
            scene_manager_next_scene(app->scene_manager, DesktopSettingsAppSceneFavorite);
            break;
        case DesktopSettingsDummyDownLong:
            scene_manager_set_scene_state(
                app->scene_manager,
                DesktopSettingsAppSceneFavorite,
                SCENE_STATE_SET_DUMMY_APP | DummyAppDownLong);
            scene_manager_next_scene(app->scene_manager, DesktopSettingsAppSceneFavorite);
            break;
        case DesktopSettingsDummyOk:
            scene_manager_set_scene_state(
                app->scene_manager,
                DesktopSettingsAppSceneFavorite,
                SCENE_STATE_SET_DUMMY_APP | DummyAppOkShort);
            scene_manager_next_scene(app->scene_manager, DesktopSettingsAppSceneFavorite);
            break;
        case DesktopSettingsDummyOkLong:
            scene_manager_set_scene_state(
                app->scene_manager,
                DesktopSettingsAppSceneFavorite,
                SCENE_STATE_SET_DUMMY_APP | DummyAppOkLong);
            scene_manager_next_scene(app->scene_manager, DesktopSettingsAppSceneFavorite);
            break;

        case DesktopSettingsHappyMode:
            scene_manager_next_scene(app->scene_manager, DesktopSettingsAppSceneHappyMode);
            break;

        default:
            break;
        }
        consumed = true;
    }
    return consumed;
}

void desktop_settings_scene_start_on_exit(void* context) {
    DesktopSettingsApp* app = context;
    variable_item_list_reset(app->variable_item_list);
    desktop_settings_save(&app->settings);

    // Trigger UI update in case we changed battery layout
    Power* power = furi_record_open(RECORD_POWER);
    power_trigger_ui_update(power);
    furi_record_close(RECORD_POWER);
}
