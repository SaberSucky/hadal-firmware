#include "main_menu_edit.h"

#include <applications.h>
#include <loader/loader.h>
#include <flipper_application/flipper_application.h>
#include <toolbox/path.h>

#define SETTINGS_ENTRY "Settings"
#define APPS_ENTRY     LOADER_APPLICATIONS_NAME
#define MAX_FILE       4096u

static void label_for(Storage* storage, const char* entry, FuriString* label) {
    if(entry[0] != '/') {
        furi_string_set(label, entry);
        return;
    }
    // An app on the SD card is shown by the name in its manifest
    FuriString* path = furi_string_alloc_set(entry);
    uint8_t* icon = malloc(FAP_MANIFEST_MAX_ICON_SIZE);
    if(!flipper_application_load_name_and_icon(path, storage, &icon, label)) {
        path_extract_filename(path, label, true);
    }
    free(icon);
    furi_string_free(path);
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
    label_for(storage, entry, item->label);
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
        furi_string_free(menu->items[i].entry);
        furi_string_free(menu->items[i].label);
    }
    free(menu->items);
    menu->items = NULL;
    menu->count = 0;
}

bool hadal_main_menu_contains(const HadalMainMenu* menu, const char* entry) {
    for(size_t i = 0; i < menu->count; i++) {
        if(furi_string_equal_str(menu->items[i].entry, entry)) return true;
    }
    return false;
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
    furi_string_free(menu->items[index].entry);
    furi_string_free(menu->items[index].label);
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
