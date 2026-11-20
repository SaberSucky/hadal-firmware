#include "desktop_keybinds_file.h"

#define KEYBINDS_DIR      EXT_PATH("apps_data/desktop")
#define KEYBINDS_FILE_MAX (4096U)

static size_t desktop_keybinds_file_read_all(Storage* storage, char* text) {
    File* file = storage_file_alloc(storage);
    size_t size = 0;
    if(storage_file_open(file, DESKTOP_KEYBINDS_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        size = storage_file_read(file, text, KEYBINDS_FILE_MAX);
    }
    storage_file_free(file);
    return size;
}

bool desktop_keybinds_file_get(Storage* storage, DesktopKeybind bind, FuriString* action) {
    furi_check(storage);
    furi_check(action);
    char* text = malloc(KEYBINDS_FILE_MAX);
    const size_t size = desktop_keybinds_file_read_all(storage, text);
    bool found = false;
    for(size_t start = 0; start < size;) {
        const char* line = text + start;
        const char* end = memchr(line, '\n', size - start);
        const size_t length = end ? (size_t)(end - line) : size - start;
        DesktopKeybind line_bind;
        const char* line_action;
        size_t action_length;
        if(desktop_keybinds_parse_line(line, length, &line_bind, &line_action, &action_length) &&
           line_bind == bind) {
            furi_string_set_strn(action, line_action, action_length);
            found = true;
        }
        start += length + 1;
    }
    free(text);
    return found;
}

bool desktop_keybinds_file_set(Storage* storage, DesktopKeybind bind, const char* action) {
    furi_check(storage);
    char* text = malloc(KEYBINDS_FILE_MAX);
    const size_t size = desktop_keybinds_file_read_all(storage, text);
    if(size == KEYBINDS_FILE_MAX) {
        // larger than this reads: rewriting it would lose its end
        free(text);
        return false;
    }
    FuriString* out = furi_string_alloc();
    if(!size) {
        furi_string_cat_str(
            out,
            "# What the desktop's buttons do: \"<button> <press|hold>: <action>\", the action a\n"
            "# desktop action (Main Menu, Lock Menu, Lock, Archive, Power Off, Nothing), an app's\n"
            "# name, an app on the SD card or any file. Set in Settings > Hadal > Interface.\n");
    }
    // every line but this one's, as it was
    for(size_t start = 0; start < size;) {
        const char* line = text + start;
        const char* end = memchr(line, '\n', size - start);
        const size_t length = end ? (size_t)(end - line) : size - start;
        DesktopKeybind line_bind;
        const char* line_action;
        size_t action_length;
        const bool this_bind =
            desktop_keybinds_parse_line(line, length, &line_bind, &line_action, &action_length) &&
            line_bind == bind;
        if(!this_bind && length) {
            furi_string_cat_printf(out, "%.*s\n", (int)length, line);
        }
        start += length + 1;
    }
    if(action) furi_string_cat_printf(out, "%s: %s\n", desktop_keybind_name(bind), action);
    free(text);

    storage_simply_mkdir(storage, EXT_PATH("apps_data"));
    storage_simply_mkdir(storage, KEYBINDS_DIR);
    File* file = storage_file_alloc(storage);
    const size_t length = furi_string_size(out);
    const bool saved =
        storage_file_open(file, DESKTOP_KEYBINDS_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS) &&
        storage_file_write(file, furi_string_get_cstr(out), length) == length;
    storage_file_free(file);
    furi_string_free(out);
    return saved;
}
