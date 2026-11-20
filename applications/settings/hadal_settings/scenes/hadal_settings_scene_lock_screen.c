#include "../hadal_settings_i.h"

/* The lock screen: lock at start, the clock, and what too many wrong PINs do */

enum {
    EventChanged = 10,
    EventEraseChosen,
};

static const char* const on_off[] = {"On", "Off"}; /* lock at start: 0 is on */
static const char* const off_on[] = {"Off", "On"};
static const char* const fail_actions[] = {"Nothing", "Power off", "Erase"};
static const uint8_t fail_limits[] = {3, 5, 10, 20};
static const char* const fail_limit_texts[] = {"3", "5", "10", "20"};
#define FAIL_LIMIT_DEFAULT 2u /* 10 */

static size_t fail_limit_index(uint8_t limit) {
    for(size_t i = 0; i < COUNT_OF(fail_limits); i++) {
        if(fail_limits[i] == limit) return i;
    }
    return FAIL_LIMIT_DEFAULT;
}

static void update(HadalSettings* app, void (*change)(DesktopSettings*, uint8_t), uint8_t value) {
    DesktopSettings* settings = hadal_settings_desktop_get(app);
    change(settings, value);
    hadal_settings_desktop_set(app, settings);
}

static void set_boot(DesktopSettings* s, uint8_t v) {
    s->boot_unlocked = v;
}
static void set_clock(DesktopSettings* s, uint8_t v) {
    s->lock_clock = v;
}
static void set_action(DesktopSettings* s, uint8_t v) {
    s->pin_fail_action = v;
    if(!s->pin_fail_limit) s->pin_fail_limit = fail_limits[FAIL_LIMIT_DEFAULT];
}
static void set_limit(DesktopSettings* s, uint8_t v) {
    s->pin_fail_limit = v;
}

static void boot_changed(VariableItem* item) {
    const uint8_t index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, on_off[index]);
    update(variable_item_get_context(item), set_boot, index);
}

static void clock_changed(VariableItem* item) {
    const uint8_t index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, off_on[index]);
    update(variable_item_get_context(item), set_clock, index);
}

static void action_changed(VariableItem* item) {
    HadalSettings* app = variable_item_get_context(item);
    const uint8_t index = variable_item_get_current_value_index(item);
    if(index == DesktopPinFailActionFactoryReset) {
        // only after reading what it means: the page asks, and comes back here
        view_dispatcher_send_custom_event(app->view_dispatcher, EventEraseChosen);
        return;
    }
    variable_item_set_current_value_text(item, fail_actions[index]);
    update(app, set_action, index);
}

static void limit_changed(VariableItem* item) {
    const uint8_t index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, fail_limit_texts[index]);
    update(variable_item_get_context(item), set_limit, fail_limits[index]);
}

void hadal_settings_scene_lock_screen_on_enter(void* context) {
    HadalSettings* app = context;
    DesktopSettings* settings = hadal_settings_desktop_get(app);
    variable_item_list_reset(app->variable_list);

    VariableItem* item =
        variable_item_list_add(app->variable_list, "Lock at start", 2, boot_changed, app);
    variable_item_set_current_value_index(item, settings->boot_unlocked ? 1 : 0);
    variable_item_set_current_value_text(item, on_off[settings->boot_unlocked ? 1 : 0]);
    variable_item_set_values_text(item, on_off);

    item = variable_item_list_add(app->variable_list, "Clock", 2, clock_changed, app);
    variable_item_set_current_value_index(item, settings->lock_clock ? 1 : 0);
    variable_item_set_current_value_text(item, off_on[settings->lock_clock ? 1 : 0]);
    variable_item_set_values_text(item, off_on);

    item = variable_item_list_add(
        app->variable_list, "Wrong PINs do", COUNT_OF(fail_actions), action_changed, app);
    const uint8_t action =
        settings->pin_fail_action < COUNT_OF(fail_actions) ? settings->pin_fail_action : 0;
    variable_item_set_current_value_index(item, action);
    variable_item_set_current_value_text(item, fail_actions[action]);
    variable_item_set_values_text(item, fail_actions);

    item = variable_item_list_add(
        app->variable_list, "After this many", COUNT_OF(fail_limits), limit_changed, app);
    const size_t limit = fail_limit_index(settings->pin_fail_limit);
    variable_item_set_current_value_index(item, limit);
    variable_item_set_current_value_text(item, fail_limit_texts[limit]);
    variable_item_set_values_text(item, fail_limit_texts);

    free(settings);
    variable_item_list_set_selected_item(
        app->variable_list,
        scene_manager_get_scene_state(app->scene_manager, HadalSettingsSceneLockScreen));
    view_dispatcher_switch_to_view(app->view_dispatcher, HadalViewVariableList);
}

bool hadal_settings_scene_lock_screen_on_event(void* context, SceneManagerEvent event) {
    HadalSettings* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;
    if(event.event == EventEraseChosen) {
        scene_manager_set_scene_state(
            app->scene_manager,
            HadalSettingsSceneLockScreen,
            variable_item_list_get_selected_item_index(app->variable_list));
        scene_manager_next_scene(app->scene_manager, HadalSettingsSceneLockScreenConfirm);
    }
    return true;
}

void hadal_settings_scene_lock_screen_on_exit(void* context) {
    HadalSettings* app = context;
    variable_item_list_reset(app->variable_list);
}

void hadal_settings_lock_screen_set_erase(HadalSettings* app) {
    update(app, set_action, DesktopPinFailActionFactoryReset);
}
