#include "../hadal_settings_i.h"

enum {
    StartInterface,
    StartProtocols,
};

void hadal_settings_scene_start_on_enter(void* context) {
    HadalSettings* app = context;
    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, "Hadal");
    submenu_add_item(
        app->submenu, "Interface", StartInterface, hadal_settings_submenu_callback, app);
    submenu_add_item(
        app->submenu, "Protocols", StartProtocols, hadal_settings_submenu_callback, app);
    submenu_set_selected_item(
        app->submenu, scene_manager_get_scene_state(app->scene_manager, HadalSettingsSceneStart));
    view_dispatcher_switch_to_view(app->view_dispatcher, HadalViewSubmenu);
}

bool hadal_settings_scene_start_on_event(void* context, SceneManagerEvent event) {
    HadalSettings* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;
    scene_manager_set_scene_state(app->scene_manager, HadalSettingsSceneStart, event.event);
    if(event.event == StartInterface) {
        scene_manager_next_scene(app->scene_manager, HadalSettingsSceneInterface);
    } else if(event.event == StartProtocols) {
        scene_manager_next_scene(app->scene_manager, HadalSettingsSceneProtocols);
    }
    return true;
}

void hadal_settings_scene_start_on_exit(void* context) {
    HadalSettings* app = context;
    submenu_reset(app->submenu);
}
