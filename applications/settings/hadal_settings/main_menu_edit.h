#pragma once

#include <furi.h>
#include <storage/storage.h>
#include <gui/icon.h>

/* The main menu as the user arranges it, read from and written to LOADER_MAIN_MENU_PATH (the
 * format is in applications/services/loader/loader_menu_list.h). Without that file the menu has
 * the firmware's built-in order, which is what this starts from. */

/* the loader's LOADER_MENU_LIST_MAX_ENTRIES: 63 entries and Settings */
#define HADAL_MAIN_MENU_MAX 64u

/** An SD card app's 10x10 icon from its manifest, on the heap: the items move, and it with them */
typedef struct HadalFapIcon HadalFapIcon;

typedef struct {
    FuriString* entry; /**< the line in the file: an app name, a .fap path, Apps or Settings */
    FuriString* label; /**< what the menu shows */
    const Icon* icon; /**< the menu's icon for it; NULL: none, its initial stands in */
    HadalFapIcon* fap_icon; /**< what icon points into, for an app on the SD card */
} HadalMainMenuItem;

typedef struct {
    HadalMainMenuItem* items;
    size_t count;
    bool customised; /**< the file exists */
} HadalMainMenu;

void hadal_main_menu_load(HadalMainMenu* menu, Storage* storage);
bool hadal_main_menu_save(const HadalMainMenu* menu, Storage* storage);
/** Back to the built-in order: the file is removed */
void hadal_main_menu_reset(HadalMainMenu* menu, Storage* storage);
void hadal_main_menu_free(HadalMainMenu* menu);

bool hadal_main_menu_contains(const HadalMainMenu* menu, const char* entry);
bool hadal_main_menu_is_settings(const HadalMainMenu* menu, size_t index);
/** @return the item's new index */
size_t hadal_main_menu_move(HadalMainMenu* menu, size_t index, int delta);
void hadal_main_menu_remove(HadalMainMenu* menu, size_t index);
/** Adds before Settings when Settings is last, else at the end */
bool hadal_main_menu_add(HadalMainMenu* menu, Storage* storage, const char* entry);
/** @return where entry is in the menu, or menu->count */
size_t hadal_main_menu_find(const HadalMainMenu* menu, const char* entry);

/** A built-in entry's icon, by its name (Apps, Settings, an app): NULL for others */
const Icon* hadal_main_menu_named_icon(const char* entry);
