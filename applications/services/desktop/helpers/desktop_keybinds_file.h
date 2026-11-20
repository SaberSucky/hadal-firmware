/**
 * @file desktop_keybinds_file.h
 * @brief Reading and changing the keybinds file (desktop_keybinds.h), for the desktop and for
 * the settings app that edits it, so that both read and write it the same way.
 */
#pragma once

#include <furi.h>
#include <storage/storage.h>

#include "desktop_keybinds.h"

#ifdef __cplusplus
extern "C" {
#endif

/** The action a button or tile is bound to, the file's last line for it; false if none */
bool desktop_keybinds_file_get(Storage* storage, DesktopKeybind bind, FuriString* action);

/** Bind a button or tile (action NULL: back to what it always did); other lines are kept */
bool desktop_keybinds_file_set(Storage* storage, DesktopKeybind bind, const char* action);

#ifdef __cplusplus
}
#endif
