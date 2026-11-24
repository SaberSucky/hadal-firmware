/**
 * @file keybinds.h
 * @brief Reading and changing the desktop's keybinds file, with the desktop's own parser
 * (applications/services/desktop/helpers/desktop_keybinds.h).
 */
#pragma once

#include <furi.h>
#include <storage/storage.h>

#include "../../services/desktop/helpers/desktop_keybinds.h"

/** The action a button is bound to; false when it does what it always did */
bool hadal_keybinds_get(Storage* storage, DesktopKeybind bind, FuriString* action);

/** Bind a button (action NULL: back to what it always did); other lines are kept */
bool hadal_keybinds_set(Storage* storage, DesktopKeybind bind, const char* action);

/** A short label for an action: a desktop action as the lists call it, an app on the SD card by
 * its manifest's name, a file by its name without its folders */
void hadal_keybinds_label(Storage* storage, const char* action, FuriString* label);
