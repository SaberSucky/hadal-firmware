#include "keybinds.h"

#include <flipper_application/flipper_application.h>
#include <strings.h>

// the desktop's own parser, so that both read the file the same way
#include "../../services/desktop/helpers/desktop_keybinds.c"

// and its file reader and writer
#include "../../services/desktop/helpers/desktop_keybinds_file.c"

bool hadal_keybinds_get(Storage* storage, DesktopKeybind bind, FuriString* action) {
    return desktop_keybinds_file_get(storage, bind, action);
}

bool hadal_keybinds_set(Storage* storage, DesktopKeybind bind, const char* action) {
    return desktop_keybinds_file_set(storage, bind, action);
}

void hadal_keybinds_label(Storage* storage, const char* action, FuriString* label) {
    // the desktop's actions as the action list and the control centre call them
    static const struct {
        DesktopKeybindAction action;
        const char* label;
    } labels[] = {
        {DesktopKeybindActionLockMenu, "Control Centre"},
        {DesktopKeybindActionLock, "Lock with PIN"},
        {DesktopKeybindActionLockKeypad, "Lock (no PIN)"},
    };
    const DesktopKeybindAction desktop_action = desktop_keybind_action(action);
    for(size_t i = 0; i < COUNT_OF(labels); i++) {
        if(labels[i].action == desktop_action) {
            furi_string_set_str(label, labels[i].label);
            return;
        }
    }
    // an app on the SD card by the name in its manifest, as the control centre and the main
    // menu show it; anything else by its name without its folders
    const size_t length = strlen(action);
    if(action[0] == '/' && length > 4 && strcasecmp(action + length - 4, ".fap") == 0) {
        FuriString* path = furi_string_alloc_set_str(action);
        uint8_t* icon = malloc(FAP_MANIFEST_MAX_ICON_SIZE);
        const bool named = flipper_application_load_name_and_icon(path, storage, &icon, label);
        free(icon);
        furi_string_free(path);
        if(named) return;
    }
    const char* slash = strrchr(action, '/');
    furi_string_set_str(label, slash ? slash + 1 : action);
    if(furi_string_end_with_str(label, ".fap")) {
        furi_string_left(label, furi_string_size(label) - 4);
    }
}
