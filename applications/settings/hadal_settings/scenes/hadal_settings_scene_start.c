#include "../hadal_settings_i.h"

#include <furi_hal_version.h>

enum {
    StartInterface,
    StartProtocols,
    StartSpoofing,
};

void hadal_settings_scene_start_on_enter(void* context) {
    HadalSettings* app = context;
    submenu_reset(app->submenu);
    // "HADAL" and the build's commit, so which firmware this is shows at a glance
    const Version* version = furi_hal_version_get_firmware_version();
    const char* githash = version ? version_get_githash(version) : NULL;
    FuriString* header = furi_string_alloc_printf("Hadal %.8s", githash ? githash : "");
    furi_string_trim(header);
    submenu_set_header(app->submenu, furi_string_get_cstr(header));
    furi_string_free(header);
    submenu_add_item(
        app->submenu, "Interface", StartInterface, hadal_settings_submenu_callback, app);
    submenu_add_item(
        app->submenu, "Protocols", StartProtocols, hadal_settings_submenu_callback, app);
    submenu_add_item(
        app->submenu, "Spoofing", StartSpoofing, hadal_settings_submenu_callback, app);
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
    } else if(event.event == StartSpoofing) {
        scene_manager_next_scene(app->scene_manager, HadalSettingsSceneSpoof);
    }
    return true;
}

void hadal_settings_scene_start_on_exit(void* context) {
    HadalSettings* app = context;
    submenu_reset(app->submenu);
}
