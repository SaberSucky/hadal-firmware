#include "../hadal_settings_i.h"

/* The main menu, top to bottom: pick an entry to move or remove it */

#define MAIN_MENU_ADD   1000u
#define MAIN_MENU_RESET 1001u

void hadal_settings_scene_main_menu_on_enter(void* context) {
    HadalSettings* app = context;
    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, "Main menu");
    submenu_set_hints(app->submenu, "ok=Move or remove;back=Done");
    for(size_t i = 0; i < app->main_menu.count; i++) {
        submenu_add_item(
            app->submenu,
            furi_string_get_cstr(app->main_menu.items[i].label),
            i,
            hadal_settings_submenu_callback,
            app);
    }
    if(app->main_menu.count < HADAL_MAIN_MENU_MAX) {
        submenu_add_item(
            app->submenu, "+ Add an app", MAIN_MENU_ADD, hadal_settings_submenu_callback, app);
    }
    if(app->main_menu.customised) {
        submenu_add_item(
            app->submenu,
            "Reset to default",
            MAIN_MENU_RESET,
            hadal_settings_submenu_callback,
            app);
    }
    submenu_set_selected_item(
        app->submenu,
        scene_manager_get_scene_state(app->scene_manager, HadalSettingsSceneMainMenu));
    view_dispatcher_switch_to_view(app->view_dispatcher, HadalViewSubmenu);
}

bool hadal_settings_scene_main_menu_on_event(void* context, SceneManagerEvent event) {
    HadalSettings* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;
    scene_manager_set_scene_state(app->scene_manager, HadalSettingsSceneMainMenu, event.event);
    if(event.event == MAIN_MENU_ADD) {
        scene_manager_next_scene(app->scene_manager, HadalSettingsSceneMainMenuAdd);
    } else if(event.event == MAIN_MENU_RESET) {
        scene_manager_next_scene(app->scene_manager, HadalSettingsSceneMainMenuReset);
    } else if(event.event < app->main_menu.count) {
        app->main_menu_selected = event.event;
        scene_manager_next_scene(app->scene_manager, HadalSettingsSceneMainMenuItem);
    }
    return true;
}

void hadal_settings_scene_main_menu_on_exit(void* context) {
    HadalSettings* app = context;
    submenu_reset(app->submenu);
}
