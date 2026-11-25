#include "main_menu_edit.h"

#include <applications.h>
#include <assets_icons.h>
#include <gui/icon_i.h>
#include <loader/loader.h>
#include <flipper_application/flipper_application.h>
#include <toolbox/path.h>

#define SETTINGS_ENTRY "Settings"
#define APPS_ENTRY     LOADER_APPLICATIONS_NAME
#define MAX_FILE       4096u

struct HadalFapIcon {
    Icon icon;
    const uint8_t* frame; /**< the icon's one frame: data */
    uint8_t data[FAP_MANIFEST_MAX_ICON_SIZE];
};

const Icon* hadal_main_menu_named_icon(const char* entry) {
    if(strcmp(entry, APPS_ENTRY) == 0) return &A_Plugins_14;
    if(strcmp(entry, SETTINGS_ENTRY) == 0) return &A_Settings_14;
    for(size_t i = 0; i < FLIPPER_APPS_COUNT; i++) {
        if(strcmp(FLIPPER_APPS[i].name, entry) == 0) return FLIPPER_APPS[i].icon;
    }
    for(size_t i = 0; i < FLIPPER_EXTERNAL_APPS_COUNT; i++) {
        if(strcmp(FLIPPER_EXTERNAL_APPS[i].name, entry) == 0) return FLIPPER_EXTERNAL_APPS[i].icon;
    }
    return NULL;
}

/* What the menu shows for an entry: an app on the SD card by the name and icon in its manifest */
static void describe(Storage* storage, HadalMainMenuItem* item) {
    const char* entry = furi_string_get_cstr(item->entry);
    item->icon = NULL;
    item->fap_icon = NULL;
    if(entry[0] != '/') {
        furi_string_set(item->label, entry);
        item->icon = hadal_main_menu_named_icon(entry);
        return;
    }
    HadalFapIcon* fap_icon = malloc(sizeof(HadalFapIcon));
    memset(fap_icon->data, 0, sizeof(fap_icon->data));
    uint8_t* data = fap_icon->data;
    FuriString* path = furi_string_alloc_set(entry);
    if(flipper_application_load_name_and_icon(path, storage, &data, item->label)) {
        bool drawn = false; // a manifest with no icon leaves it all zero
        for(size_t i = 0; i < sizeof(fap_icon->data); i++) {
            if(fap_icon->data[i]) drawn = true;
        }
        if(drawn) {
            fap_icon->frame = fap_icon->data;
            const Icon icon = {
                .width = 10,
                .height = 10,
                .frame_count = 1,
                .frame_rate = 0,
                .frames = &fap_icon->frame,
            };
            memcpy(&fap_icon->icon, &icon, sizeof(Icon));
            item->icon = &fap_icon->icon;
            item->fap_icon = fap_icon;
        }
    } else {
        path_extract_filename(path, item->label, true);
    }
    if(!item->fap_icon) free(fap_icon);
    furi_string_free(path);
}

static void item_clear(HadalMainMenuItem* item) {
    furi_string_free(item->entry);
    furi_string_free(item->label);
    free(item->fap_icon);
}

static void push(HadalMainMenu* menu, Storage* storage, const char* entry) {
    // the loader's rule: at most one fewer than the maximum, so Settings always has a place
    const size_t limit = strcmp(entry, SETTINGS_ENTRY) == 0 ? HADAL_MAIN_MENU_MAX :
                                                              HADAL_MAIN_MENU_MAX - 1;
    if(menu->count >= limit || hadal_main_menu_contains(menu, entry)) return;
    menu->items = realloc(menu->items, (menu->count + 1) * sizeof(HadalMainMenuItem));
    HadalMainMenuItem* item = &menu->items[menu->count++];
    item->entry = furi_string_alloc_set(entry);
    item->label = furi_string_alloc();
    describe(storage, item);
}

static void load_defaults(HadalMainMenu* menu, Storage* storage) {
    push(menu, storage, APPS_ENTRY);
    for(size_t i = 0; i < FLIPPER_APPS_COUNT; i++) {
        push(menu, storage, FLIPPER_APPS[i].name);
    }
    for(size_t i = 0; i < FLIPPER_EXTERNAL_APPS_COUNT; i++) {
        push(menu, storage, FLIPPER_EXTERNAL_APPS[i].name);
    }
    push(menu, storage, SETTINGS_ENTRY);
}

static bool load_file(HadalMainMenu* menu, Storage* storage) {
    bool loaded = false;
    File* file = storage_file_alloc(storage);
    if(storage_file_open(file, LOADER_MAIN_MENU_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        const uint64_t size = storage_file_size(file);
        if(size <= MAX_FILE) {
            char* text = malloc(size + 1);
            if(storage_file_read(file, text, size) == size) {
                text[size] = '\0';
                // the same rules as the loader: trimmed lines, '#' comments, blank lines skipped
                char* line = text;
                while(line && *line) {
                    char* next = strchr(line, '\n');
                    if(next) *next++ = '\0';
                    while(*line == ' ' || *line == '\t')
                        line++;
                    size_t length = strlen(line);
                    while(length && (line[length - 1] == ' ' || line[length - 1] == '\t' ||
                                     line[length - 1] == '\r')) {
                        line[--length] = '\0';
                    }
                    if(length && line[0] != '#') push(menu, storage, line);
                    line = next;
                }
                loaded = true;
            }
            free(text);
        }
    }
    storage_file_free(file);
    if(loaded) push(menu, storage, SETTINGS_ENTRY); // the loader adds it too
    return loaded;
}

void hadal_main_menu_load(HadalMainMenu* menu, Storage* storage) {
    menu->items = NULL;
    menu->count = 0;
    menu->customised = load_file(menu, storage);
    if(!menu->customised) {
        hadal_main_menu_free(menu);
        load_defaults(menu, storage);
    }
}

bool hadal_main_menu_save(const HadalMainMenu* menu, Storage* storage) {
    // the folders, each in turn: a missing /ext/apps_data made the save fail without a word
    storage_simply_mkdir(storage, EXT_PATH("apps_data"));
    FuriString* folder = furi_string_alloc_set(LOADER_MAIN_MENU_PATH);
    path_extract_dirname(LOADER_MAIN_MENU_PATH, folder);
    storage_simply_mkdir(storage, furi_string_get_cstr(folder));
    furi_string_free(folder);

    bool saved = false;
    File* file = storage_file_alloc(storage);
    if(storage_file_open(file, LOADER_MAIN_MENU_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        const char* header =
            "# The main menu, top to bottom. Edit it in Settings > Hadal > Interface > Main Menu.\n"
            "# One entry per line: an app's name, an app's path on the SD card, Apps or Settings.\n";
        saved = storage_file_write(file, header, strlen(header)) == strlen(header);
        for(size_t i = 0; saved && i < menu->count; i++) {
            const char* entry = furi_string_get_cstr(menu->items[i].entry);
            saved = storage_file_write(file, entry, strlen(entry)) == strlen(entry) &&
                    storage_file_write(file, "\n", 1) == 1;
        }
        storage_file_close(file);
    }
    storage_file_free(file);
    return saved;
}

void hadal_main_menu_reset(HadalMainMenu* menu, Storage* storage) {
    storage_simply_remove(storage, LOADER_MAIN_MENU_PATH);
    hadal_main_menu_free(menu);
    hadal_main_menu_load(menu, storage);
}

void hadal_main_menu_free(HadalMainMenu* menu) {
    for(size_t i = 0; i < menu->count; i++) {
        item_clear(&menu->items[i]);
    }
    free(menu->items);
    menu->items = NULL;
    menu->count = 0;
}

size_t hadal_main_menu_find(const HadalMainMenu* menu, const char* entry) {
    for(size_t i = 0; i < menu->count; i++) {
        if(furi_string_equal_str(menu->items[i].entry, entry)) return i;
    }
    return menu->count;
}

bool hadal_main_menu_contains(const HadalMainMenu* menu, const char* entry) {
    return hadal_main_menu_find(menu, entry) < menu->count;
}

bool hadal_main_menu_is_settings(const HadalMainMenu* menu, size_t index) {
    return index < menu->count && furi_string_equal_str(menu->items[index].entry, SETTINGS_ENTRY);
}

size_t hadal_main_menu_move(HadalMainMenu* menu, size_t index, int delta) {
    if(index >= menu->count) return index;
    if(delta < 0 && index == 0) return index;
    if(delta > 0 && index + 1 >= menu->count) return index;
    const size_t other = delta < 0 ? index - 1 : index + 1;
    HadalMainMenuItem swap = menu->items[index];
    menu->items[index] = menu->items[other];
    menu->items[other] = swap;
    return other;
}

void hadal_main_menu_remove(HadalMainMenu* menu, size_t index) {
    if(index >= menu->count || hadal_main_menu_is_settings(menu, index)) return;
    item_clear(&menu->items[index]);
    memmove(
        &menu->items[index],
        &menu->items[index + 1],
        (menu->count - index - 1) * sizeof(HadalMainMenuItem));
    menu->count--;
}

bool hadal_main_menu_add(HadalMainMenu* menu, Storage* storage, const char* entry) {
    const size_t before = menu->count;
    push(menu, storage, entry);
    if(menu->count == before) return false;
    // Settings stays last if it was
    if(menu->count >= 2 && hadal_main_menu_is_settings(menu, menu->count - 2)) {
        hadal_main_menu_move(menu, menu->count - 1, -1);
    }
    return true;
}
