/**
 * @file desktop_apps.h
 * @brief The apps a control centre tile can open, with their icons: the main menu's apps, in
 * the main menu's order, as the user arranged it (loader/loader_menu_list.h) or as built in.
 */
#pragma once

#include <furi.h>
#include <gui/icon.h>
#include <gui/icon_i.h>
#include <flipper_application/application_manifest.h>

#ifdef __cplusplus
extern "C" {
#endif

/** An SD card app's 10x10 icon, from its manifest, and the Icon that draws it */
typedef struct {
    Icon icon;
    const uint8_t* frame; /**< the icon's one frame: data */
    uint8_t data[FAP_MANIFEST_MAX_ICON_SIZE];
} DesktopFapIcon;

typedef struct {
    char* label; /**< what the app is called */
    char* action; /**< what a tile is bound to for it (keybinds); NULL: no app */
    const Icon* icon; /**< NULL: none, the label's initial stands in */
    DesktopFapIcon* fap_icon; /**< what icon points into, for an app on the SD card */
} DesktopApp;

typedef struct {
    DesktopApp* items;
    size_t count;
} DesktopAppList;

/** Fill an empty list: "None" first, then the main menu's apps. Reads the SD card */
void desktop_app_list_load(DesktopAppList* list);

/** Free what the list holds, leaving it empty */
void desktop_app_list_clear(DesktopAppList* list);

/** The entry bound to action, or 0 ("None") when it is not in the list */
size_t desktop_app_list_find(const DesktopAppList* list, const char* action);

/**
 * What a bound action looks like on a tile: its label (an app's name, a file's name) and its
 * icon (an app's own, or that of the app a file opens in); NULL when it has none. An SD card
 * app's icon is read into fap_icon, which the returned Icon then draws from.
 */
const Icon* desktop_app_describe(const char* action, FuriString* label, DesktopFapIcon* fap_icon);

#ifdef __cplusplus
}
#endif
