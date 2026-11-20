#include "keybinds.h"

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

void hadal_keybinds_label(const char* action, FuriString* label) {
    const char* slash = strrchr(action, '/');
    furi_string_set_str(label, slash ? slash + 1 : action);
    if(furi_string_end_with_str(label, ".fap")) {
        furi_string_left(label, furi_string_size(label) - 4);
    }
}
