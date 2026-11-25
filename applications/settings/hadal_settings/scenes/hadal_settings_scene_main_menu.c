#include "../hadal_settings_i.h"

#include <applications.h>
#include <assets_icons.h>
#include <loader/loader.h>
#include <flipper_application/flipper_application.h>

/* The main menu as a row of tiles (main_menu_view.h), as the control centre picks its apps: lift
 * an app and carry it, Down takes it out, + brings one in (and it is then carried to its place),
 * and a changed menu can go back to the firmware's own order. Every change is saved as it is
 * made, as the loader reads the file each time the menu opens. */

#define EVENT(event, index)  (((uint32_t)(event) << 16) | ((uint32_t)(index) & 0xFFFFu))
#define EVENT_TYPE(event)    ((event) >> 16)
#define EVENT_INDEX(event)   ((event) & 0xFFFFu)

static void main_menu_view_callback(void* context, MainMenuViewEvent event, size_t index) {
    HadalSettings* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, EVENT(event, index));
}

static void tile_set(
    MainMenuTile* tile,
    const char* label,
    const Icon* icon,
    MainMenuTileKind kind,
    bool fixed) {
    strlcpy(tile->label, label, sizeof(tile->label));
    tile->icon = icon;
    tile->kind = kind;
    tile->fixed = fixed;
}

/** The menu's apps, then + and, for a changed menu, reset. Called with the model held */
static void main_menu_show(HadalSettings* app, MainMenuViewModel* model, size_t selected, bool lifted) {
    const HadalMainMenu* menu = &app->main_menu;
    strlcpy(model->title, "Main menu", sizeof(model->title));
    size_t count = 0;
    for(size_t i = 0; i < menu->count && count < MAIN_MENU_VIEW_TILES_MAX - 2; i++) {
        tile_set(
            &model->tiles[count++],
            furi_string_get_cstr(menu->items[i].label),
            menu->items[i].icon,
            MainMenuTileApp,
            hadal_main_menu_is_settings(menu, i));
    }
    model->apps = count;
    if(menu->count < HADAL_MAIN_MENU_MAX) {
        tile_set(&model->tiles[count++], "Add an app", NULL, MainMenuTileAdd, true);
    }
    if(menu->customised) {
        tile_set(&model->tiles[count++], "Reset to default", NULL, MainMenuTileReset, true);
    }
    model->count = count;
    model->selected = count ? MIN(selected, count - 1) : 0;
    model->lifted = lifted && model->selected < model->apps;
    model->adding = false;
}

/** What can come in: an app on the SD card, Apps, the built-in apps not in the menu */
static void main_menu_show_adding(HadalSettings* app, MainMenuViewModel* model) {
    const HadalMainMenu* menu = &app->main_menu;
    strlcpy(model->title, "Add an app", sizeof(model->title));
    size_t count = 0;
    app->main_menu_candidates[count] = NULL; // the file browser
    tile_set(
        &model->tiles[count++], "App on the SD card", &I_SDcardMounted_11x8, MainMenuTileApp, true);
    if(!hadal_main_menu_contains(menu, LOADER_APPLICATIONS_NAME)) {
        app->main_menu_candidates[count] = LOADER_APPLICATIONS_NAME;
        tile_set(
            &model->tiles[count++],
            "Apps (all on the card)",
            hadal_main_menu_named_icon(LOADER_APPLICATIONS_NAME),
            MainMenuTileApp,
            true);
    }
    for(size_t i = 0; i < FLIPPER_APPS_COUNT && count < MAIN_MENU_VIEW_TILES_MAX; i++) {
        if(hadal_main_menu_contains(menu, FLIPPER_APPS[i].name)) continue;
        app->main_menu_candidates[count] = FLIPPER_APPS[i].name;
        tile_set(
            &model->tiles[count++],
            FLIPPER_APPS[i].name,
            FLIPPER_APPS[i].icon,
            MainMenuTileApp,
            true);
    }
    for(size_t i = 0; i < FLIPPER_EXTERNAL_APPS_COUNT && count < MAIN_MENU_VIEW_TILES_MAX; i++) {
        if(hadal_main_menu_contains(menu, FLIPPER_EXTERNAL_APPS[i].name)) continue;
        app->main_menu_candidates[count] = FLIPPER_EXTERNAL_APPS[i].name;
        tile_set(
            &model->tiles[count++],
            FLIPPER_EXTERNAL_APPS[i].name,
            FLIPPER_EXTERNAL_APPS[i].icon,
            MainMenuTileApp,
            true);
    }
    model->count = count;
    model->apps = count;
    model->selected = 0;
    model->lifted = false;
    model->adding = true;
}

static void main_menu_refresh(HadalSettings* app, size_t selected, bool lifted) {
    MainMenuViewModel* model = main_menu_view_lock(app->main_menu_view);
    main_menu_show(app, model, selected, lifted);
    main_menu_view_unlock(app->main_menu_view);
}

static void main_menu_save(HadalSettings* app) {
    // a menu not saved is not the one the Flipper shows: say so
    if(hadal_main_menu_save(&app->main_menu, app->storage)) {
        app->main_menu.customised = true;
    } else {
        dialog_message_show_storage_error(app->dialogs, "Cannot save\nthe main menu");
    }
}

static bool main_menu_add_item_loader(
    FuriString* path,
    void* context,
    uint8_t** icon,
    FuriString* name) {
    HadalSettings* app = context;
    return flipper_application_load_name_and_icon(path, app->storage, icon, name);
}

/** The chosen app comes in and is carried to its place; at the + if it could not */
static void main_menu_pick(HadalSettings* app, size_t index) {
    FuriString* entry = furi_string_alloc();
    if(index < MAIN_MENU_VIEW_TILES_MAX && app->main_menu_candidates[index]) {
        furi_string_set(entry, app->main_menu_candidates[index]);
    } else {
        const DialogsFileBrowserOptions options = {
            .extension = ".fap",
            .base_path = EXT_PATH("apps"),
            .skip_assets = true,
            .hide_dot_files = true,
            .icon = &I_unknown_10px,
            .hide_ext = true,
            .item_loader_callback = main_menu_add_item_loader,
            .item_loader_context = app,
        };
        FuriString* path = furi_string_alloc_set_str(EXT_PATH("apps"));
        if(dialog_file_browser_show(app->dialogs, path, path, &options)) {
            furi_string_set(entry, path);
        }
        furi_string_free(path);
    }

    size_t where = app->main_menu.count; // the + tile
    bool carried = false;
    if(furi_string_size(entry)) {
        MainMenuViewModel* model = main_menu_view_lock(app->main_menu_view);
        carried = hadal_main_menu_add(&app->main_menu, app->storage, furi_string_get_cstr(entry));
        where = hadal_main_menu_find(&app->main_menu, furi_string_get_cstr(entry));
        if(where >= app->main_menu.count) where = app->main_menu.count;
        app->main_menu_origin = where;
        main_menu_show(app, model, where, carried);
        main_menu_view_unlock(app->main_menu_view);
        // saved with the model let go: a failure's dialog waits for a key
        if(carried) {
            main_menu_save(app);
            main_menu_refresh(app, where, true);
        }
    } else {
        main_menu_refresh(app, where, false);
    }
    furi_string_free(entry);
}

void hadal_settings_scene_main_menu_on_enter(void* context) {
    HadalSettings* app = context;
    main_menu_view_set_callback(app->main_menu_view, main_menu_view_callback, app);
    main_menu_refresh(
        app, scene_manager_get_scene_state(app->scene_manager, HadalSettingsSceneMainMenu), false);
    view_dispatcher_switch_to_view(app->view_dispatcher, HadalViewMainMenu);
}

bool hadal_settings_scene_main_menu_on_event(void* context, SceneManagerEvent event) {
    HadalSettings* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;
    const MainMenuViewEvent type = EVENT_TYPE(event.event);
    const size_t index = EVENT_INDEX(event.event);
    HadalMainMenu* menu = &app->main_menu;

    if(type == MainMenuViewEventLift) {
        app->main_menu_origin = index;
    } else if(type == MainMenuViewEventMove || type == MainMenuViewEventCancel) {
        MainMenuViewModel* model = main_menu_view_lock(app->main_menu_view);
        size_t at = model->selected;
        if(type == MainMenuViewEventMove) {
            at = hadal_main_menu_move(menu, at, index ? 1 : -1);
        } else {
            // put back where it was picked up: what is saved already
            while(at != app->main_menu_origin && at < menu->count) {
                const size_t next =
                    hadal_main_menu_move(menu, at, at > app->main_menu_origin ? -1 : 1);
                if(next == at) break;
                at = next;
            }
        }
        main_menu_show(app, model, at, type == MainMenuViewEventMove);
        main_menu_view_unlock(app->main_menu_view);
    } else if(type == MainMenuViewEventDrop) {
        main_menu_save(app);
        MainMenuViewModel* model = main_menu_view_lock(app->main_menu_view);
        main_menu_show(app, model, model->selected, false); // reset comes in once it is changed
        main_menu_view_unlock(app->main_menu_view);
    } else if(type == MainMenuViewEventHide) {
        // the next app takes its place; the last one's neighbour, if it was the last
        MainMenuViewModel* model = main_menu_view_lock(app->main_menu_view);
        hadal_main_menu_remove(menu, index);
        const size_t at = index < menu->count ? index : menu->count - 1;
        main_menu_show(app, model, at, false);
        main_menu_view_unlock(app->main_menu_view);
        // saved with the model let go: a failure's dialog waits for a key
        main_menu_save(app);
        main_menu_refresh(app, at, false); // reset comes in once it is changed
    } else if(type == MainMenuViewEventOpenAdd) {
        MainMenuViewModel* model = main_menu_view_lock(app->main_menu_view);
        main_menu_show_adding(app, model);
        main_menu_view_unlock(app->main_menu_view);
    } else if(type == MainMenuViewEventCloseAdd) {
        main_menu_refresh(app, menu->count, false); // back on the +
    } else if(type == MainMenuViewEventPick) {
        main_menu_pick(app, index);
    } else if(type == MainMenuViewEventReset) {
        scene_manager_set_scene_state(app->scene_manager, HadalSettingsSceneMainMenu, 0);
        scene_manager_next_scene(app->scene_manager, HadalSettingsSceneMainMenuReset);
    }
    return true;
}

void hadal_settings_scene_main_menu_on_exit(void* context) {
    HadalSettings* app = context;
    // closed while an app was carried (the Flipper's own Back): it stays where it is now
    MainMenuViewModel* model = main_menu_view_lock(app->main_menu_view);
    const bool lifted = model->lifted;
    model->lifted = false;
    main_menu_view_unlock(app->main_menu_view);
    if(lifted) main_menu_save(app);
    main_menu_view_set_callback(app->main_menu_view, NULL, NULL);
}
