#include "../hadal_settings_i.h"

#include <applications.h>
#include <flipper_application/flipper_application.h>
#include <assets_icons.h>

/* What a desktop button does: what it always did, a desktop action, an app or a file */

enum {
    ActionDefault,
    ActionFirstWord, /* + index in action_words */
    ActionSdApp = 20,
    ActionFile,
    ActionBuiltin = 100, /* + index in FLIPPER_APPS */
    ActionExternal = 400, /* + index in FLIPPER_EXTERNAL_APPS */
};

static const char* const action_words[] = {
    "Main Menu",
    "Lock Menu",
    "Lock",
    "Archive",
    "Power Off",
    "Nothing",
};

static bool sd_app_loader(FuriString* path, void* context, uint8_t** icon, FuriString* name) {
    HadalSettings* app = context;
    return flipper_application_load_name_and_icon(path, app->storage, icon, name);
}

static void bind_to(HadalSettings* app, const char* action) {
    hadal_keybinds_set(app->storage, app->keybind_selected, action);
    scene_manager_previous_scene(app->scene_manager);
}

void hadal_settings_scene_keybind_action_on_enter(void* context) {
    HadalSettings* app = context;
    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, "Do what?");
    submenu_add_item(
        app->submenu, "What it always did", ActionDefault, hadal_settings_submenu_callback, app);
    for(size_t i = 0; i < COUNT_OF(action_words); i++) {
        submenu_add_item(
            app->submenu, action_words[i], ActionFirstWord + i, hadal_settings_submenu_callback, app);
    }
    submenu_add_item(
        app->submenu, "App on the SD card...", ActionSdApp, hadal_settings_submenu_callback, app);
    submenu_add_item(app->submenu, "Open a file...", ActionFile, hadal_settings_submenu_callback, app);
    for(size_t i = 0; i < FLIPPER_APPS_COUNT; i++) {
        submenu_add_item(
            app->submenu, FLIPPER_APPS[i].name, ActionBuiltin + i, hadal_settings_submenu_callback, app);
    }
    for(size_t i = 0; i < FLIPPER_EXTERNAL_APPS_COUNT; i++) {
        submenu_add_item(
            app->submenu,
            FLIPPER_EXTERNAL_APPS[i].name,
            ActionExternal + i,
            hadal_settings_submenu_callback,
            app);
    }
    view_dispatcher_switch_to_view(app->view_dispatcher, HadalViewSubmenu);
}

bool hadal_settings_scene_keybind_action_on_event(void* context, SceneManagerEvent event) {
    HadalSettings* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;
    const uint32_t choice = event.event;

    if(choice == ActionDefault) {
        bind_to(app, NULL);
    } else if(choice >= ActionFirstWord && choice < ActionFirstWord + COUNT_OF(action_words)) {
        bind_to(app, action_words[choice - ActionFirstWord]);
    } else if(choice == ActionSdApp || choice == ActionFile) {
        const bool fap = choice == ActionSdApp;
        const DialogsFileBrowserOptions options = {
            .extension = fap ? ".fap" : "*",
            .base_path = fap ? EXT_PATH("apps") : STORAGE_EXT_PATH_PREFIX,
            .skip_assets = true,
            .hide_dot_files = true,
            .icon = fap ? &I_unknown_10px : NULL,
            .hide_ext = fap,
            .item_loader_callback = fap ? sd_app_loader : NULL,
            .item_loader_context = app,
        };
        FuriString* path = furi_string_alloc_set_str(options.base_path);
        if(dialog_file_browser_show(app->dialogs, path, path, &options)) {
            bind_to(app, furi_string_get_cstr(path));
        }
        furi_string_free(path);
    } else if(choice >= ActionExternal && choice - ActionExternal < FLIPPER_EXTERNAL_APPS_COUNT) {
        bind_to(app, FLIPPER_EXTERNAL_APPS[choice - ActionExternal].name);
    } else if(choice >= ActionBuiltin && choice - ActionBuiltin < FLIPPER_APPS_COUNT) {
        bind_to(app, FLIPPER_APPS[choice - ActionBuiltin].name);
    }
    return true;
}

void hadal_settings_scene_keybind_action_on_exit(void* context) {
    HadalSettings* app = context;
    submenu_reset(app->submenu);
}
