#include "../hadal_settings_i.h"

#include <applications.h>
#include <loader/loader.h>
#include <flipper_application/flipper_application.h>
#include <assets_icons.h>

/* What can be added: an app from the SD card, and whatever built-in entry is not there yet */

#define ADD_SD_APP   0u
#define ADD_APPS     1u
#define ADD_BUILTIN  100u /* + index in FLIPPER_APPS */
#define ADD_EXTERNAL 400u /* + index in FLIPPER_EXTERNAL_APPS */

static bool add_item_loader(FuriString* path, void* context, uint8_t** icon, FuriString* name) {
    HadalSettings* app = context;
    return flipper_application_load_name_and_icon(path, app->storage, icon, name);
}

static void added(HadalSettings* app, const char* entry) {
    if(hadal_main_menu_add(&app->main_menu, app->storage, entry)) {
        // a menu not saved is not the one the Flipper shows: say so
        if(hadal_main_menu_save(&app->main_menu, app->storage)) {
            app->main_menu.customised = true;
        } else {
            dialog_message_show_storage_error(app->dialogs, "Cannot save\nthe main menu");
        }
        // back on the list, on the new entry
        for(size_t i = 0; i < app->main_menu.count; i++) {
            if(furi_string_equal_str(app->main_menu.items[i].entry, entry)) {
                scene_manager_set_scene_state(app->scene_manager, HadalSettingsSceneMainMenu, i);
            }
        }
    }
    scene_manager_previous_scene(app->scene_manager);
}

void hadal_settings_scene_main_menu_add_on_enter(void* context) {
    HadalSettings* app = context;
    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, "Add to the main menu");
    submenu_add_item(
        app->submenu, "App on the SD card...", ADD_SD_APP, hadal_settings_submenu_callback, app);
    if(!hadal_main_menu_contains(&app->main_menu, LOADER_APPLICATIONS_NAME)) {
        submenu_add_item(
            app->submenu,
            "Apps (all SD card apps)",
            ADD_APPS,
            hadal_settings_submenu_callback,
            app);
    }
    for(size_t i = 0; i < FLIPPER_APPS_COUNT; i++) {
        if(!hadal_main_menu_contains(&app->main_menu, FLIPPER_APPS[i].name)) {
            submenu_add_item(
                app->submenu,
                FLIPPER_APPS[i].name,
                ADD_BUILTIN + i,
                hadal_settings_submenu_callback,
                app);
        }
    }
    for(size_t i = 0; i < FLIPPER_EXTERNAL_APPS_COUNT; i++) {
        if(!hadal_main_menu_contains(&app->main_menu, FLIPPER_EXTERNAL_APPS[i].name)) {
            submenu_add_item(
                app->submenu,
                FLIPPER_EXTERNAL_APPS[i].name,
                ADD_EXTERNAL + i,
                hadal_settings_submenu_callback,
                app);
        }
    }
    view_dispatcher_switch_to_view(app->view_dispatcher, HadalViewSubmenu);
}

bool hadal_settings_scene_main_menu_add_on_event(void* context, SceneManagerEvent event) {
    HadalSettings* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;

    if(event.event == ADD_SD_APP) {
        const DialogsFileBrowserOptions options = {
            .extension = ".fap",
            .base_path = EXT_PATH("apps"),
            .skip_assets = true,
            .hide_dot_files = true,
            .icon = &I_unknown_10px,
            .hide_ext = true,
            .item_loader_callback = add_item_loader,
            .item_loader_context = app,
        };
        FuriString* path = furi_string_alloc_set_str(EXT_PATH("apps"));
        if(dialog_file_browser_show(app->dialogs, path, path, &options)) {
            added(app, furi_string_get_cstr(path));
        }
        furi_string_free(path);
    } else if(event.event == ADD_APPS) {
        added(app, LOADER_APPLICATIONS_NAME);
    } else if(event.event >= ADD_EXTERNAL && event.event - ADD_EXTERNAL < FLIPPER_EXTERNAL_APPS_COUNT) {
        added(app, FLIPPER_EXTERNAL_APPS[event.event - ADD_EXTERNAL].name);
    } else if(event.event >= ADD_BUILTIN && event.event - ADD_BUILTIN < FLIPPER_APPS_COUNT) {
        added(app, FLIPPER_APPS[event.event - ADD_BUILTIN].name);
    }
    return true;
}

void hadal_settings_scene_main_menu_add_on_exit(void* context) {
    HadalSettings* app = context;
    submenu_reset(app->submenu);
}
