/**
 * @file loader_menu_list.h
 * @brief The main menu's own list, as the user arranged it.
 *
 * LOADER_MAIN_MENU_PATH, when it exists, lists the main menu one entry per line, top to bottom:
 *
 *     # comment
 *     Apps                       the "Apps" folder browser
 *     NFC                        an app by its name, as the main menu shows it
 *     /ext/apps/Tools/clock.fap  an app on the SD card, by its path
 *     Settings                   the settings menu
 *
 * Blank lines and lines starting with '#' are skipped, spaces around an entry are ignored, an
 * entry listed twice is kept once, and "Settings" is added at the end if the list leaves it out,
 * so that a list can never lock anyone out of the settings. No dependencies, for the host tests.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LOADER_MENU_LIST_MAX_ENTRIES   (64U)
#define LOADER_MENU_LIST_MAX_LINE      (127U)
#define LOADER_MENU_LIST_APPS_NAME     "Apps"
#define LOADER_MENU_LIST_SETTINGS_NAME "Settings"

typedef enum {
    LoaderMenuListEntryApps, /**< the "Apps" folder browser */
    LoaderMenuListEntrySettings, /**< the settings menu */
    LoaderMenuListEntryName, /**< an app by its main menu name */
    LoaderMenuListEntryPath, /**< an app on the SD card, by its absolute path */
} LoaderMenuListEntryKind;

typedef struct {
    LoaderMenuListEntryKind kind;
    char* text; /**< the line as written (trimmed), owned by the list */
} LoaderMenuListEntry;

typedef struct {
    LoaderMenuListEntry* entries;
    size_t count;
} LoaderMenuList;

/**
 * @brief Read a list from the text of the file.
 *
 * Lines longer than LOADER_MENU_LIST_MAX_LINE are skipped, entries past
 * LOADER_MENU_LIST_MAX_ENTRIES are dropped (Settings still gets its place).
 *
 * @param[in] text the file's contents, need not be terminated
 * @param[in] length bytes in text
 * @param[out] list filled in; empty lists hold Settings alone. Free with loader_menu_list_clear()
 */
void loader_menu_list_parse(const char* text, size_t length, LoaderMenuList* list);

/** @brief Free what loader_menu_list_parse() put in a list, leaving it empty. */
void loader_menu_list_clear(LoaderMenuList* list);

#ifdef __cplusplus
}
#endif
