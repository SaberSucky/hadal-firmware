#include "../hadal_settings_i.h"

enum {
    MiscSpoofing,
};

void hadal_settings_scene_misc_on_enter(void* context) {
    HadalSettings* app = context;
    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, "Misc");
    submenu_add_item(app->submenu, "Spoofing", MiscSpoofing, hadal_settings_submenu_callback, app);
    view_dispatcher_switch_to_view(app->view_dispatcher, HadalViewSubmenu);
}

bool hadal_settings_scene_misc_on_event(void* context, SceneManagerEvent event) {
    HadalSettings* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;
    if(event.event == MiscSpoofing) scene_manager_next_scene(app->scene_manager, HadalSettingsSceneSpoof);
    return true;
}

void hadal_settings_scene_misc_on_exit(void* context) {
    HadalSettings* app = context;
    submenu_reset(app->submenu);
}
