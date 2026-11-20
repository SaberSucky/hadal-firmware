#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/menu.h>
#include <gui/modules/submenu.h>
#include <assets_icons.h>
#include <applications.h>
#include <archive/helpers/archive_favorites.h>

#include <gui/icon_i.h>
#include <storage/storage.h>
#include <flipper_application/flipper_application.h>

#include "loader.h"
#include "loader_menu.h"
#include "loader_menu_list.h"

#define TAG "LoaderMenu"

// The list file is a few lines; anything larger is not one
#define LOADER_MENU_LIST_MAX_FILE (4096U)

typedef enum {
    LoaderMenuEntryApps,
    LoaderMenuEntrySettings,
    LoaderMenuEntryApp,
} LoaderMenuEntryKind;

// One main menu item. Labels and icons are kept by pointer by the menu, so an entry for an app
// on the SD card owns its name, its path and its icon until the menu is freed.
typedef struct {
    LoaderMenuEntryKind kind;
    const char* label;
    const char* start_name; /**< what loader_start() is given */
    const Icon* icon;
    char* owned_label;
    char* owned_path;
    Icon* owned_icon;
    uint8_t* owned_icon_data;
} LoaderMenuEntry;

struct LoaderMenu {
    FuriThread* thread;
    void (*closed_cb)(void*);
    void* context;
    Menu* primary_menu;
    ViewDispatcher* view_dispatcher;
    LoaderMenuEntry* entries;
    size_t entry_count;
};

typedef enum {
    LoaderMenuViewPrimary,
    LoaderMenuViewSettings,
} LoaderMenuView;

static void loader_menu_start(const char* name) {
    Loader* loader = furi_record_open(RECORD_LOADER);
    loader_start_with_gui_error(loader, name, NULL);
    furi_record_close(RECORD_LOADER);
}

static void
    loader_menu_settings_menu_callback(void* context, InputType input_type, uint32_t index) {
    UNUSED(context);
    if(input_type == InputTypeShort) {
        loader_menu_start((const char*)index);
    } else if(input_type == InputTypeLong) {
        archive_favorites_handle_setting_pin_unpin((const char*)index, NULL);
    }
}

static uint32_t loader_menu_switch_to_primary(void* context) {
    UNUSED(context);
    return LoaderMenuViewPrimary;
}

static uint32_t loader_menu_exit(void* context) {
    UNUSED(context);
    return VIEW_NONE;
}

static void loader_menu_entry_callback(void* context, uint32_t index) {
    LoaderMenu* loader_menu = context;
    if(index >= loader_menu->entry_count) return;
    const LoaderMenuEntry* entry = &loader_menu->entries[index];
    if(entry->kind == LoaderMenuEntrySettings) {
        view_dispatcher_switch_to_view(loader_menu->view_dispatcher, LoaderMenuViewSettings);
    } else {
        loader_menu_start(entry->start_name);
    }
}

static LoaderMenuEntry* loader_menu_add_entry(LoaderMenu* loader_menu, LoaderMenuEntryKind kind) {
    loader_menu->entries =
        realloc(loader_menu->entries, (loader_menu->entry_count + 1) * sizeof(LoaderMenuEntry));
    LoaderMenuEntry* entry = &loader_menu->entries[loader_menu->entry_count++];
    memset(entry, 0, sizeof(LoaderMenuEntry));
    entry->kind = kind;
    return entry;
}

static void loader_menu_add_apps(LoaderMenu* loader_menu) {
    LoaderMenuEntry* entry = loader_menu_add_entry(loader_menu, LoaderMenuEntryApps);
    entry->label = LOADER_APPLICATIONS_NAME;
    entry->start_name = LOADER_APPLICATIONS_NAME;
    entry->icon = &A_Plugins_14;
}

static void loader_menu_add_settings(LoaderMenu* loader_menu) {
    LoaderMenuEntry* entry = loader_menu_add_entry(loader_menu, LoaderMenuEntrySettings);
    entry->label = "Settings";
    entry->icon = &A_Settings_14;
}

static void loader_menu_add_app(LoaderMenu* loader_menu, const char* name, const Icon* icon) {
    LoaderMenuEntry* entry = loader_menu_add_entry(loader_menu, LoaderMenuEntryApp);
    entry->label = name;
    entry->start_name = name;
    entry->icon = icon;
}

// An app known to the firmware, by the name the main menu shows
static bool loader_menu_add_named_app(LoaderMenu* loader_menu, const char* name) {
    for(size_t i = 0; i < FLIPPER_APPS_COUNT; i++) {
        if(strcmp(FLIPPER_APPS[i].name, name) == 0) {
            loader_menu_add_app(loader_menu, FLIPPER_APPS[i].name, FLIPPER_APPS[i].icon);
            return true;
        }
    }
    for(size_t i = 0; i < FLIPPER_EXTERNAL_APPS_COUNT; i++) {
        if(strcmp(FLIPPER_EXTERNAL_APPS[i].name, name) == 0) {
            loader_menu_add_app(
                loader_menu, FLIPPER_EXTERNAL_APPS[i].name, FLIPPER_EXTERNAL_APPS[i].icon);
            return true;
        }
    }
    return false;
}

// An app on the SD card: its name and its 10x10 icon come from its manifest
static bool loader_menu_add_sd_app(LoaderMenu* loader_menu, Storage* storage, const char* path) {
    const size_t length = strlen(path);
    if(length < 5 || strcmp(path + length - 4, ".fap") != 0) return false;

    FuriString* fap_path = furi_string_alloc_set_str(path);
    FuriString* name = furi_string_alloc();
    uint8_t* icon_data = malloc(FAP_MANIFEST_MAX_ICON_SIZE);
    bool loaded = flipper_application_load_name_and_icon(fap_path, storage, &icon_data, name);
    if(loaded) {
        LoaderMenuEntry* entry = loader_menu_add_entry(loader_menu, LoaderMenuEntryApp);
        entry->owned_label = strdup(furi_string_get_cstr(name));
        entry->owned_path = strdup(path);
        entry->owned_icon_data = icon_data;
        // An app icon is one 10x10 frame in the firmware's own icon format
        entry->owned_icon = malloc(sizeof(Icon));
        const Icon icon = {
            .width = 10,
            .height = 10,
            .frame_count = 1,
            .frame_rate = 0,
            .frames = (const uint8_t* const*)&entry->owned_icon_data,
        };
        memcpy(entry->owned_icon, &icon, sizeof(Icon));
        entry->label = entry->owned_label;
        entry->start_name = entry->owned_path;
        entry->icon = entry->owned_icon;
    } else {
        FURI_LOG_W(TAG, "Main menu: cannot read %s", path);
        free(icon_data);
    }
    furi_string_free(name);
    furi_string_free(fap_path);
    return loaded;
}

// The user's list, if there is a readable one
static bool loader_menu_load_list(LoaderMenuList* list) {
    bool loaded = false;
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    if(storage_file_open(file, LOADER_MAIN_MENU_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        const uint64_t size = storage_file_size(file);
        if(size > 0 && size <= LOADER_MENU_LIST_MAX_FILE) {
            char* text = malloc(size);
            if(storage_file_read(file, text, size) == size) {
                loader_menu_list_parse(text, size, list);
                loaded = true;
            }
            free(text);
        }
    }
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
    return loaded;
}

static void loader_menu_build_menu(LoaderMenu* loader_menu) {
    LoaderMenuList list;
    if(loader_menu_load_list(&list)) {
        Storage* storage = furi_record_open(RECORD_STORAGE);
        for(size_t i = 0; i < list.count; i++) {
            const LoaderMenuListEntry* item = &list.entries[i];
            switch(item->kind) {
            case LoaderMenuListEntryApps:
                loader_menu_add_apps(loader_menu);
                break;
            case LoaderMenuListEntrySettings:
                loader_menu_add_settings(loader_menu);
                break;
            case LoaderMenuListEntryName:
                // An app that is gone (renamed, or not in this firmware) just drops out
                if(!loader_menu_add_named_app(loader_menu, item->text)) {
                    FURI_LOG_W(TAG, "Main menu: no app named %s", item->text);
                }
                break;
            case LoaderMenuListEntryPath:
                loader_menu_add_sd_app(loader_menu, storage, item->text);
                break;
            }
        }
        furi_record_close(RECORD_STORAGE);
        loader_menu_list_clear(&list);
    } else {
        // The built-in order
        loader_menu_add_apps(loader_menu);
        for(size_t i = 0; i < FLIPPER_APPS_COUNT; i++) {
            loader_menu_add_app(loader_menu, FLIPPER_APPS[i].name, FLIPPER_APPS[i].icon);
        }
        for(size_t i = 0; i < FLIPPER_EXTERNAL_APPS_COUNT; i++) {
            loader_menu_add_app(
                loader_menu, FLIPPER_EXTERNAL_APPS[i].name, FLIPPER_EXTERNAL_APPS[i].icon);
        }
        loader_menu_add_settings(loader_menu);
    }

    for(size_t i = 0; i < loader_menu->entry_count; i++) {
        menu_add_item(
            loader_menu->primary_menu,
            loader_menu->entries[i].label,
            loader_menu->entries[i].icon,
            i,
            loader_menu_entry_callback,
            loader_menu);
    }
}

static void loader_menu_build_submenu(Submenu* submenu, LoaderMenu* loader_menu) {
    for(size_t i = 0; i < FLIPPER_EXTSETTINGS_APPS_COUNT; i++) {
        submenu_add_item_ex(
            submenu,
            FLIPPER_EXTSETTINGS_APPS[i].name,
            (uint32_t)FLIPPER_EXTSETTINGS_APPS[i].name,
            loader_menu_settings_menu_callback,
            loader_menu);
    }
    for(size_t i = 0; i < FLIPPER_SETTINGS_APPS_COUNT; i++) {
        submenu_add_item_ex(
            submenu,
            FLIPPER_SETTINGS_APPS[i].name,
            (uint32_t)FLIPPER_SETTINGS_APPS[i].name,
            loader_menu_settings_menu_callback,
            loader_menu);
    }
}

static int32_t loader_menu_thread(void* p) {
    LoaderMenu* loader_menu = p;
    furi_assert(loader_menu);

    Gui* gui = furi_record_open(RECORD_GUI);
    Submenu* settings_menu = submenu_alloc();
    loader_menu_build_submenu(settings_menu, loader_menu);

    loader_menu->view_dispatcher = view_dispatcher_alloc();

    View* primary_view = menu_get_view(loader_menu->primary_menu);
    view_set_previous_callback(primary_view, loader_menu_exit);
    view_dispatcher_add_view(loader_menu->view_dispatcher, LoaderMenuViewPrimary, primary_view);

    View* settings_view = submenu_get_view(settings_menu);
    view_set_previous_callback(settings_view, loader_menu_switch_to_primary);
    view_dispatcher_add_view(loader_menu->view_dispatcher, LoaderMenuViewSettings, settings_view);
    view_dispatcher_switch_to_view(loader_menu->view_dispatcher, LoaderMenuViewPrimary);

    view_dispatcher_attach_to_gui(loader_menu->view_dispatcher, gui, ViewDispatcherTypeFullscreen);
    view_dispatcher_run(loader_menu->view_dispatcher);

    if(loader_menu->closed_cb) {
        loader_menu->closed_cb(loader_menu->context);
    }

    view_dispatcher_remove_view(loader_menu->view_dispatcher, LoaderMenuViewPrimary);
    view_dispatcher_remove_view(loader_menu->view_dispatcher, LoaderMenuViewSettings);
    view_dispatcher_free(loader_menu->view_dispatcher);
    submenu_free(settings_menu);
    furi_record_close(RECORD_GUI);

    return 0;
}

LoaderMenu* loader_menu_alloc(void (*closed_cb)(void*), void* context, const MenuStyle* style) {
    LoaderMenu* loader_menu = malloc(sizeof(LoaderMenu));
    loader_menu->entries = NULL;
    loader_menu->entry_count = 0;
    loader_menu->closed_cb = closed_cb;
    loader_menu->context = context;
    loader_menu->primary_menu = menu_alloc();
    menu_set_style(loader_menu->primary_menu, style);
    loader_menu_build_menu(loader_menu);
    loader_menu->thread = furi_thread_alloc_ex(TAG, 1024, loader_menu_thread, loader_menu);
    furi_thread_start(loader_menu->thread);
    return loader_menu;
}

void loader_menu_free(LoaderMenu* loader_menu) {
    furi_assert(loader_menu);
    furi_thread_join(loader_menu->thread);
    furi_thread_free(loader_menu->thread);
    menu_free(loader_menu->primary_menu);
    // after the menu, which kept the labels and icons by pointer
    for(size_t i = 0; i < loader_menu->entry_count; i++) {
        LoaderMenuEntry* entry = &loader_menu->entries[i];
        free(entry->owned_label);
        free(entry->owned_path);
        free(entry->owned_icon);
        free(entry->owned_icon_data);
    }
    free(loader_menu->entries);
    free(loader_menu);
}

void loader_menu_set_style(LoaderMenu* loader_menu, const MenuStyle* style) {
    furi_assert(loader_menu);
    menu_set_style(loader_menu->primary_menu, style);
}
