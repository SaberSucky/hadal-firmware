#include "../hadal_settings_i.h"

/* Every desktop button, pressed and held, and what it is bound to */

static const char* const button_labels[DesktopKeybindCount] = {
    [DesktopKeybindUpPress] = "Up",
    [DesktopKeybindUpHold] = "Up, held",
    [DesktopKeybindDownPress] = "Down",
    [DesktopKeybindDownHold] = "Down, held",
    [DesktopKeybindLeftPress] = "Left",
    [DesktopKeybindLeftHold] = "Left, held",
    [DesktopKeybindRightPress] = "Right",
    [DesktopKeybindRightHold] = "Right, held",
    [DesktopKeybindOkPress] = "OK",
    [DesktopKeybindOkHold] = "OK, held",
    // the control centre's two app tiles (Up on the desktop)
    [DesktopKeybindControl1] = "Tile 1",
    [DesktopKeybindControl2] = "Tile 2",
};

/* What a button does with nothing bound to it (desktop_scene_main.c's defaults, out of Dummy
 * mode): said by name, not "Default". An old favourite left empty opens Apps */
static const char* const default_actions[DesktopKeybindCount] = {
    [DesktopKeybindUpPress] = "Control Centre",
    [DesktopKeybindUpHold] = "Lock with PIN",
    [DesktopKeybindDownPress] = "Archive",
    [DesktopKeybindDownHold] = "Debug",
    [DesktopKeybindLeftPress] = "Apps",
    [DesktopKeybindLeftHold] = "Apps",
    [DesktopKeybindRightPress] = "Passport",
    [DesktopKeybindRightHold] = "Apps",
    [DesktopKeybindOkPress] = "Main Menu",
    [DesktopKeybindOkHold] = "Apps",
    [DesktopKeybindControl1] = "None",
    [DesktopKeybindControl2] = "None",
};

void hadal_settings_scene_desktop_keys_on_enter(void* context) {
    HadalSettings* app = context;
    VariableItemList* list = app->variable_list;
    variable_item_list_reset(list);
    variable_item_list_set_header(list, "Desktop keys");
    // a row a button: what it does on the right, as every setting has it; OK chooses another
    FuriString* action = furi_string_alloc();
    FuriString* label = furi_string_alloc();
    for(size_t bind = 0; bind < DesktopKeybindCount; bind++) {
        if(hadal_keybinds_get(app->storage, bind, action)) {
            hadal_keybinds_label(app->storage, furi_string_get_cstr(action), label);
        } else {
            furi_string_set_str(label, default_actions[bind]);
        }
        VariableItem* item = variable_item_list_add(list, button_labels[bind], 1, NULL, app);
        variable_item_set_opens(item, true);
        variable_item_set_current_value_text(item, furi_string_get_cstr(label));
    }
    furi_string_free(label);
    furi_string_free(action);
    variable_item_list_set_enter_callback(list, hadal_settings_submenu_callback, app);
    variable_item_list_set_selected_item(
        list, scene_manager_get_scene_state(app->scene_manager, HadalSettingsSceneDesktopKeys));
    view_dispatcher_switch_to_view(app->view_dispatcher, HadalViewVariableList);
}

bool hadal_settings_scene_desktop_keys_on_event(void* context, SceneManagerEvent event) {
    HadalSettings* app = context;
    if(event.type != SceneManagerEventTypeCustom || event.event >= DesktopKeybindCount) {
        return false;
    }
    scene_manager_set_scene_state(app->scene_manager, HadalSettingsSceneDesktopKeys, event.event);
    app->keybind_selected = event.event;
    scene_manager_next_scene(app->scene_manager, HadalSettingsSceneKeybindAction);
    return true;
}

void hadal_settings_scene_desktop_keys_on_exit(void* context) {
    HadalSettings* app = context;
    variable_item_list_reset(app->variable_list);
}
