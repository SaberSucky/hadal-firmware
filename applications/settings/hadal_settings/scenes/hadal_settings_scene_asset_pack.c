#include "../hadal_settings_i.h"

/* The packs in /ext/asset_packs, "None" first; "* " marks the one in use */

#define PACK_NONE 0u

static void packs_build(HadalSettings* app, uint32_t selected) {
    DesktopSettings* settings = hadal_settings_desktop_get(app);
    const char* in_use = settings->active_asset_pack;

    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, "Asset Pack");
    FuriString* label = furi_string_alloc_printf("%sBuilt-in", in_use[0] ? "  " : "* ");
    submenu_add_item(
        app->submenu, furi_string_get_cstr(label), PACK_NONE, hadal_settings_submenu_callback, app);
    uint32_t current = PACK_NONE;
    for(size_t i = 0; i < app->pack_count; i++) {
        const bool used = strcmp(app->pack_names[i], in_use) == 0;
        if(used) current = i + 1;
        furi_string_printf(label, "%s%s", used ? "* " : "  ", app->pack_names[i]);
        submenu_add_item(
            app->submenu, furi_string_get_cstr(label), i + 1, hadal_settings_submenu_callback, app);
    }
    furi_string_free(label);
    free(settings);
    submenu_set_selected_item(app->submenu, selected == UINT32_MAX ? current : selected);
}

void hadal_settings_scene_asset_pack_on_enter(void* context) {
    HadalSettings* app = context;
    app->pack_count = hadal_pack_list(app->storage, app->pack_names, HADAL_MAX_PACKS);
    const uint32_t state =
        scene_manager_get_scene_state(app->scene_manager, HadalSettingsSceneAssetPack);
    packs_build(app, state ? state - 1 : UINT32_MAX);
    view_dispatcher_switch_to_view(app->view_dispatcher, HadalViewSubmenu);
}

bool hadal_settings_scene_asset_pack_on_event(void* context, SceneManagerEvent event) {
    HadalSettings* app = context;
    if(event.type == SceneManagerEventTypeBack) {
        scene_manager_set_scene_state(app->scene_manager, HadalSettingsSceneAssetPack, 0);
        return false;
    }
    if(event.type != SceneManagerEventTypeCustom) return false;
    app->pack_chosen = event.event;
    // come back to the same entry (state 0 means: on the one in use)
    scene_manager_set_scene_state(
        app->scene_manager, HadalSettingsSceneAssetPack, event.event + 1);
    scene_manager_next_scene(app->scene_manager, HadalSettingsSceneAssetPackInfo);
    return true;
}

void hadal_settings_scene_asset_pack_on_exit(void* context) {
    HadalSettings* app = context;
    submenu_reset(app->submenu);
}
