#include "../hadal_settings_i.h"

#include <strings.h>

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

/* The desktop's actions: the word in keybinds.txt, and what the list calls it (the two locks as
 * the control centre's tiles call them) */
static const struct {
    const char* word;
    const char* label;
} action_words[] = {
    {"Main Menu", "Main Menu"},
    {"Lock Menu", "Control Centre"},
    {"Lock", "Lock with PIN"},
    {"Lock Keypad", "Lock (no PIN)"},
    {"Archive", "Archive"},
    {"Power Off", "Power off"},
    {"Nothing", "Nothing"},
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
    submenu_add_item(app->submenu, "Default", ActionDefault, hadal_settings_submenu_callback, app);
    for(size_t i = 0; i < COUNT_OF(action_words); i++) {
        submenu_add_item(
            app->submenu,
            action_words[i].label,
            ActionFirstWord + i,
            hadal_settings_submenu_callback,
            app);
    }
    submenu_add_item(
        app->submenu, "App on the SD card...", ActionSdApp, hadal_settings_submenu_callback, app);
    submenu_add_item(
        app->submenu, "Open a file...", ActionFile, hadal_settings_submenu_callback, app);
    for(size_t i = 0; i < FLIPPER_APPS_COUNT; i++) {
        submenu_add_item(
            app->submenu,
            FLIPPER_APPS[i].name,
            ActionBuiltin + i,
            hadal_settings_submenu_callback,
            app);
    }
    for(size_t i = 0; i < FLIPPER_EXTERNAL_APPS_COUNT; i++) {
        submenu_add_item(
            app->submenu,
            FLIPPER_EXTERNAL_APPS[i].name,
            ActionExternal + i,
            hadal_settings_submenu_callback,
            app);
    }

    // on what it does now, as the control centre's picker and the lists open on theirs
    uint32_t current = ActionDefault;
    FuriString* action = furi_string_alloc();
    if(hadal_keybinds_get(app->storage, app->keybind_selected, action)) {
        const char* text = furi_string_get_cstr(action);
        current = text[0] == '/' ?
                      (furi_string_end_withi_str(action, ".fap") ? ActionSdApp : ActionFile) :
                      ActionDefault;
        for(size_t i = 0; i < COUNT_OF(action_words); i++) {
            if(strcasecmp(text, action_words[i].word) == 0) current = ActionFirstWord + i;
        }
        for(size_t i = 0; i < FLIPPER_APPS_COUNT; i++) {
            if(strcmp(text, FLIPPER_APPS[i].name) == 0) current = ActionBuiltin + i;
        }
        for(size_t i = 0; i < FLIPPER_EXTERNAL_APPS_COUNT; i++) {
            if(strcmp(text, FLIPPER_EXTERNAL_APPS[i].name) == 0) current = ActionExternal + i;
        }
    }
    furi_string_free(action);
    submenu_set_selected_item(app->submenu, current);
    view_dispatcher_switch_to_view(app->view_dispatcher, HadalViewSubmenu);
}

bool hadal_settings_scene_keybind_action_on_event(void* context, SceneManagerEvent event) {
    HadalSettings* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;
    const uint32_t choice = event.event;

    if(choice == ActionDefault) {
        bind_to(app, NULL);
    } else if(choice >= ActionFirstWord && choice < ActionFirstWord + COUNT_OF(action_words)) {
        bind_to(app, action_words[choice - ActionFirstWord].word);
    } else if(choice == ActionSdApp || choice == ActionFile) {
        const bool fap = choice == ActionSdApp;
        // only the files a button can open (desktop_keybinds.c): any other did nothing at all
        const DialogsFileBrowserOptions options = {
            .extension = fap ? ".fap" : ".sub|.nfc|.rfid|.ibtn|.ir|.js|.txt",
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
            // a text file opens only as a Bad USB script or a Sub-GHz Remote map
            if(!fap && furi_string_end_withi_str(path, ".txt") &&
               !furi_string_start_with_str(path, EXT_PATH("badusb/")) &&
               !furi_string_start_with_str(path, EXT_PATH("subghz_remote/"))) {
                DialogMessage* message = dialog_message_alloc();
                dialog_message_set_header(message, "Cannot open it", 64, 2, AlignCenter, AlignTop);
                dialog_message_set_text(
                    message,
                    "Only Bad USB and Sub-GHz\nRemote .txt files open\nfrom a button",
                    64,
                    34,
                    AlignCenter,
                    AlignCenter);
                dialog_message_set_buttons(message, NULL, "OK", NULL);
                dialog_message_show(app->dialogs, message);
                dialog_message_free(message);
            } else {
                bind_to(app, furi_string_get_cstr(path));
            }
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
