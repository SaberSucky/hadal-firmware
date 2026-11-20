#include "../hadal_settings_i.h"

/* One main menu entry: move it up or down (the page stays, so it can be moved several places),
 * or take it out. Settings cannot be taken out. */

enum {
    ItemMoveUp,
    ItemMoveDown,
    ItemRemove,
};

static void item_build(HadalSettings* app, uint32_t selected) {
    submenu_reset(app->submenu);
    const size_t index = app->main_menu_selected;
    FuriString* header = furi_string_alloc_printf(
        "%s (%u of %u)",
        furi_string_get_cstr(app->main_menu.items[index].label),
        (unsigned)(index + 1),
        (unsigned)app->main_menu.count);
    submenu_set_header(app->submenu, furi_string_get_cstr(header));
    furi_string_free(header);

    submenu_add_item(app->submenu, "Move up", ItemMoveUp, hadal_settings_submenu_callback, app);
    submenu_add_item(
        app->submenu, "Move down", ItemMoveDown, hadal_settings_submenu_callback, app);
    if(!hadal_main_menu_is_settings(&app->main_menu, index)) {
        submenu_add_item(
            app->submenu, "Remove from menu", ItemRemove, hadal_settings_submenu_callback, app);
    }
    submenu_set_selected_item(app->submenu, selected);
}

static void item_saved(HadalSettings* app) {
    hadal_main_menu_save(&app->main_menu, app->storage);
    app->main_menu.customised = true;
}

void hadal_settings_scene_main_menu_item_on_enter(void* context) {
    HadalSettings* app = context;
    item_build(app, ItemMoveUp);
    view_dispatcher_switch_to_view(app->view_dispatcher, HadalViewSubmenu);
}

bool hadal_settings_scene_main_menu_item_on_event(void* context, SceneManagerEvent event) {
    HadalSettings* app = context;
    if(event.type == SceneManagerEventTypeBack) {
        // back on the list, on the entry where it is now
        scene_manager_set_scene_state(
            app->scene_manager, HadalSettingsSceneMainMenu, app->main_menu_selected);
        return false;
    }
    if(event.type != SceneManagerEventTypeCustom) return false;

    if(event.event == ItemMoveUp || event.event == ItemMoveDown) {
        app->main_menu_selected = hadal_main_menu_move(
            &app->main_menu, app->main_menu_selected, event.event == ItemMoveUp ? -1 : 1);
        item_saved(app);
        item_build(app, event.event);
    } else if(event.event == ItemRemove) {
        hadal_main_menu_remove(&app->main_menu, app->main_menu_selected);
        item_saved(app);
        const size_t count = app->main_menu.count;
        scene_manager_set_scene_state(
            app->scene_manager,
            HadalSettingsSceneMainMenu,
            app->main_menu_selected < count ? app->main_menu_selected : count - 1);
        scene_manager_previous_scene(app->scene_manager);
    }
    return true;
}

void hadal_settings_scene_main_menu_item_on_exit(void* context) {
    HadalSettings* app = context;
    submenu_reset(app->submenu);
}
