#include "../hadal_settings_i.h"

enum {
    InterfaceMainMenu,
    InterfaceMenuStyle,
    InterfaceAssetPack,
    InterfaceDesktopKeys,
    InterfaceLockScreen,
};

void hadal_settings_scene_interface_on_enter(void* context) {
    HadalSettings* app = context;
    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, "Interface");
    submenu_add_item(
        app->submenu, "Main Menu", InterfaceMainMenu, hadal_settings_submenu_callback, app);
    submenu_add_item(
        app->submenu, "Menu Style", InterfaceMenuStyle, hadal_settings_submenu_callback, app);
    submenu_add_item(
        app->submenu, "Asset Pack", InterfaceAssetPack, hadal_settings_submenu_callback, app);
    submenu_add_item(
        app->submenu, "Desktop Keys", InterfaceDesktopKeys, hadal_settings_submenu_callback, app);
    submenu_add_item(
        app->submenu, "Lock Screen", InterfaceLockScreen, hadal_settings_submenu_callback, app);
    submenu_set_selected_item(
        app->submenu,
        scene_manager_get_scene_state(app->scene_manager, HadalSettingsSceneInterface));
    view_dispatcher_switch_to_view(app->view_dispatcher, HadalViewSubmenu);
}

bool hadal_settings_scene_interface_on_event(void* context, SceneManagerEvent event) {
    HadalSettings* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;
    scene_manager_set_scene_state(app->scene_manager, HadalSettingsSceneInterface, event.event);
    switch(event.event) {
    case InterfaceMainMenu:
        scene_manager_set_scene_state(app->scene_manager, HadalSettingsSceneMainMenu, 0);
        scene_manager_next_scene(app->scene_manager, HadalSettingsSceneMainMenu);
        break;
    case InterfaceMenuStyle:
        scene_manager_next_scene(app->scene_manager, HadalSettingsSceneMenuStyle);
        break;
    case InterfaceAssetPack:
        scene_manager_next_scene(app->scene_manager, HadalSettingsSceneAssetPack);
        break;
    case InterfaceDesktopKeys:
        scene_manager_set_scene_state(app->scene_manager, HadalSettingsSceneDesktopKeys, 0);
        scene_manager_next_scene(app->scene_manager, HadalSettingsSceneDesktopKeys);
        break;
    case InterfaceLockScreen:
        scene_manager_next_scene(app->scene_manager, HadalSettingsSceneLockScreen);
        break;
    }
    return true;
}

void hadal_settings_scene_interface_on_exit(void* context) {
    HadalSettings* app = context;
    submenu_reset(app->submenu);
}
