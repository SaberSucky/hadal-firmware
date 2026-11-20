#include "../hadal_settings_i.h"

/* Every desktop button, pressed and held, and what it is bound to */

static const char* const button_labels[DesktopKeybindCount] = {
    [DesktopKeybindUpPress] = "Up",
    [DesktopKeybindUpHold] = "Up held",
    [DesktopKeybindDownPress] = "Down",
    [DesktopKeybindDownHold] = "Down held",
    [DesktopKeybindLeftPress] = "Left",
    [DesktopKeybindLeftHold] = "Left held",
    [DesktopKeybindRightPress] = "Right",
    [DesktopKeybindRightHold] = "Right held",
    [DesktopKeybindOkPress] = "OK",
    [DesktopKeybindOkHold] = "OK held",
};

void hadal_settings_scene_desktop_keys_on_enter(void* context) {
    HadalSettings* app = context;
    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, "Desktop Keys");
    FuriString* action = furi_string_alloc();
    FuriString* label = furi_string_alloc();
    FuriString* item = furi_string_alloc();
    for(size_t bind = 0; bind < DesktopKeybindCount; bind++) {
        if(hadal_keybinds_get(app->storage, bind, action)) {
            hadal_keybinds_label(furi_string_get_cstr(action), label);
        } else {
            furi_string_set_str(label, "default");
        }
        furi_string_printf(item, "%s: %s", button_labels[bind], furi_string_get_cstr(label));
        submenu_add_item(
            app->submenu, furi_string_get_cstr(item), bind, hadal_settings_submenu_callback, app);
    }
    furi_string_free(item);
    furi_string_free(label);
    furi_string_free(action);
    submenu_set_selected_item(
        app->submenu,
        scene_manager_get_scene_state(app->scene_manager, HadalSettingsSceneDesktopKeys));
    view_dispatcher_switch_to_view(app->view_dispatcher, HadalViewSubmenu);
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
    submenu_reset(app->submenu);
}
