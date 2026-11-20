#include "desktop_apps.h"
#include "desktop_keybinds.h"

#include <applications.h>
#include <assets_icons.h>
#include <loader/loader.h>
#include <loader/loader_menu_list.h>
#include <storage/storage.h>
#include <toolbox/path.h>
#include <flipper_application/flipper_application.h>

#define TAG "DesktopApps"

// Reading an app's manifest from the SD card takes more stack than the desktop's own thread
// has to spare, so it is done on a short-lived thread of its own
#define DESKTOP_APPS_WORKER_STACK (3 * 1024)

// The main menu file is a few lines; anything larger is not one
#define DESKTOP_APPS_LIST_MAX_FILE (4096U)

/** A built-in app's icon by its name, as the main menu shows it */
static const Icon* desktop_apps_named_icon(const char* name) {
    if(strcmp(name, LOADER_APPLICATIONS_NAME) == 0) return &A_Plugins_14;
    for(size_t i = 0; i < FLIPPER_APPS_COUNT; i++) {
        if(strcmp(FLIPPER_APPS[i].name, name) == 0) return FLIPPER_APPS[i].icon;
    }
    for(size_t i = 0; i < FLIPPER_EXTERNAL_APPS_COUNT; i++) {
        if(strcmp(FLIPPER_EXTERNAL_APPS[i].name, name) == 0) return FLIPPER_EXTERNAL_APPS[i].icon;
    }
    return NULL;
}

static bool desktop_apps_is_named(const char* name) {
    if(strcmp(name, LOADER_APPLICATIONS_NAME) == 0) return true;
    for(size_t i = 0; i < FLIPPER_APPS_COUNT; i++) {
        if(strcmp(FLIPPER_APPS[i].name, name) == 0) return true;
    }
    for(size_t i = 0; i < FLIPPER_EXTERNAL_APPS_COUNT; i++) {
        if(strcmp(FLIPPER_EXTERNAL_APPS[i].name, name) == 0) return true;
    }
    return false;
}

static bool desktop_apps_is_fap(const char* path) {
    const size_t length = strlen(path);
    return path[0] == '/' && length > 4 && strcasecmp(path + length - 4, ".fap") == 0;
}

/** An SD card app's name and icon from its manifest; false if it cannot be read */
static bool desktop_apps_read_fap(
    Storage* storage,
    const char* path,
    FuriString* name,
    DesktopFapIcon* fap_icon) {
    FuriString* fap_path = furi_string_alloc_set_str(path);
    memset(fap_icon->data, 0, sizeof(fap_icon->data));
    uint8_t* data = fap_icon->data;
    const bool loaded = flipper_application_load_name_and_icon(fap_path, storage, &data, name);
    furi_string_free(fap_path);
    if(!loaded) return false;

    // An app icon is one 10x10 frame in the firmware's own icon format
    fap_icon->frame = fap_icon->data;
    const Icon icon = {
        .width = 10,
        .height = 10,
        .frame_count = 1,
        .frame_rate = 0,
        .frames = &fap_icon->frame,
    };
    memcpy(&fap_icon->icon, &icon, sizeof(Icon));
    return true;
}

/** Whether the manifest had an icon: one that has none leaves the data all zero */
static bool desktop_apps_fap_has_icon(const DesktopFapIcon* fap_icon) {
    for(size_t i = 0; i < sizeof(fap_icon->data); i++) {
        if(fap_icon->data[i]) return true;
    }
    return false;
}

/* ---- the list ---- */

static DesktopApp* desktop_app_list_add(DesktopAppList* list, const char* label, const char* action) {
    list->items = realloc(list->items, (list->count + 1) * sizeof(DesktopApp)); //-V701
    DesktopApp* app = &list->items[list->count++];
    memset(app, 0, sizeof(DesktopApp));
    app->label = strdup(label);
    app->action = action ? strdup(action) : NULL;
    return app;
}

static void desktop_app_list_add_named(DesktopAppList* list, const char* name, const Icon* icon) {
    DesktopApp* app = desktop_app_list_add(list, name, name);
    app->icon = icon;
}

static void desktop_app_list_add_fap(DesktopAppList* list, Storage* storage, const char* path) {
    if(!desktop_apps_is_fap(path)) return;
    FuriString* name = furi_string_alloc();
    DesktopFapIcon* fap_icon = malloc(sizeof(DesktopFapIcon));
    if(desktop_apps_read_fap(storage, path, name, fap_icon)) {
        DesktopApp* app = desktop_app_list_add(list, furi_string_get_cstr(name), path);
        app->fap_icon = fap_icon;
        if(desktop_apps_fap_has_icon(fap_icon)) app->icon = &fap_icon->icon;
    } else {
        FURI_LOG_W(TAG, "Cannot read %s", path);
        free(fap_icon);
    }
    furi_string_free(name);
}

/** The user's main menu list, if there is a readable one */
static bool desktop_app_list_read_menu(Storage* storage, LoaderMenuList* menu) {
    bool loaded = false;
    File* file = storage_file_alloc(storage);
    if(storage_file_open(file, LOADER_MAIN_MENU_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        const uint64_t size = storage_file_size(file);
        if(size > 0 && size <= DESKTOP_APPS_LIST_MAX_FILE) {
            char* text = malloc(size);
            if(storage_file_read(file, text, size) == size) {
                loader_menu_list_parse(text, size, menu);
                loaded = true;
            }
            free(text);
        }
    }
    storage_file_free(file);
    return loaded;
}

static int32_t desktop_app_list_worker(void* context) {
    DesktopAppList* list = context;
    Storage* storage = furi_record_open(RECORD_STORAGE);

    desktop_app_list_add(list, "None", NULL)->icon = &I_cc_none_12x12;

    LoaderMenuList menu;
    if(desktop_app_list_read_menu(storage, &menu)) {
        for(size_t i = 0; i < menu.count; i++) {
            const LoaderMenuListEntry* entry = &menu.entries[i];
            switch(entry->kind) {
            case LoaderMenuListEntryApps:
                desktop_app_list_add_named(list, LOADER_APPLICATIONS_NAME, &A_Plugins_14);
                break;
            case LoaderMenuListEntryName:
                if(desktop_apps_is_named(entry->text)) {
                    desktop_app_list_add_named(
                        list, entry->text, desktop_apps_named_icon(entry->text));
                }
                break;
            case LoaderMenuListEntryPath:
                desktop_app_list_add_fap(list, storage, entry->text);
                break;
            case LoaderMenuListEntrySettings:
                // a menu, not an app a tile can start
                break;
            }
        }
        loader_menu_list_clear(&menu);
    } else {
        desktop_app_list_add_named(list, LOADER_APPLICATIONS_NAME, &A_Plugins_14);
        for(size_t i = 0; i < FLIPPER_APPS_COUNT; i++) {
            desktop_app_list_add_named(list, FLIPPER_APPS[i].name, FLIPPER_APPS[i].icon);
        }
        for(size_t i = 0; i < FLIPPER_EXTERNAL_APPS_COUNT; i++) {
            desktop_app_list_add_named(
                list, FLIPPER_EXTERNAL_APPS[i].name, FLIPPER_EXTERNAL_APPS[i].icon);
        }
    }

    furi_record_close(RECORD_STORAGE);
    return 0;
}

static void desktop_apps_run_worker(FuriThreadCallback callback, void* context) {
    FuriThread* thread =
        furi_thread_alloc_ex("DesktopApps", DESKTOP_APPS_WORKER_STACK, callback, context);
    furi_thread_start(thread);
    furi_thread_join(thread);
    furi_thread_free(thread);
}

void desktop_app_list_load(DesktopAppList* list) {
    furi_check(list);
    furi_check(list->count == 0);
    desktop_apps_run_worker(desktop_app_list_worker, list);
}

void desktop_app_list_clear(DesktopAppList* list) {
    furi_check(list);
    for(size_t i = 0; i < list->count; i++) {
        free(list->items[i].label);
        free(list->items[i].action);
        free(list->items[i].fap_icon);
    }
    free(list->items);
    list->items = NULL;
    list->count = 0;
}

size_t desktop_app_list_find(const DesktopAppList* list, const char* action) {
    furi_check(list);
    if(!action) return 0;
    for(size_t i = 0; i < list->count; i++) {
        if(list->items[i].action && strcmp(list->items[i].action, action) == 0) return i;
    }
    return 0;
}

/* ---- one bound action ---- */

/** A path's file name without its extension */
static void desktop_apps_file_label(const char* path, FuriString* label) {
    FuriString* full = furi_string_alloc_set_str(path);
    path_extract_filename(full, label, true);
    furi_string_free(full);
}

typedef struct {
    const char* path;
    FuriString* name;
    DesktopFapIcon* fap_icon;
    bool loaded;
} DesktopAppsFapRead;

static int32_t desktop_apps_fap_worker(void* context) {
    DesktopAppsFapRead* read = context;
    Storage* storage = furi_record_open(RECORD_STORAGE);
    read->loaded = desktop_apps_read_fap(storage, read->path, read->name, read->fap_icon);
    furi_record_close(RECORD_STORAGE);
    return 0;
}

const Icon* desktop_app_describe(const char* action, FuriString* label, DesktopFapIcon* fap_icon) {
    furi_check(action);
    furi_check(label);
    furi_check(fap_icon);

    if(desktop_apps_is_fap(action)) {
        DesktopAppsFapRead read = {.path = action, .name = label, .fap_icon = fap_icon};
        desktop_apps_run_worker(desktop_apps_fap_worker, &read);
        if(read.loaded) {
            return desktop_apps_fap_has_icon(fap_icon) ? &fap_icon->icon : NULL;
        }
        // gone from the card, or not an app: its file name
        desktop_apps_file_label(action, label);
        return NULL;
    }

    if(action[0] == '/') {
        // a file, shown with the icon of the app it opens in
        desktop_apps_file_label(action, label);
        const char* app = desktop_keybind_app_for_file(action);
        return app ? desktop_apps_named_icon(app) : NULL;
    }

    // an app by its name, or a desktop action ("Lock Menu")
    furi_string_set_str(label, action);
    return desktop_apps_named_icon(action);
}
