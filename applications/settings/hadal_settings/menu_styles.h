#pragma once

#include <furi.h>
#include <storage/storage.h>

/* The main menu styles installed on the SD card (LOADER_MENU_STYLES_PATH), by display name. */

typedef struct {
    FuriString* file; /**< file name, as DesktopSettings.menu_style holds it */
    FuriString* name; /**< the style's own name */
} HadalMenuStyle;

typedef struct {
    HadalMenuStyle* styles;
    size_t count;
} HadalMenuStyles;

/** Scans the plugins folder; sorted by name. `max_file` is the setting's size. */
void hadal_menu_styles_scan(HadalMenuStyles* styles, Storage* storage, size_t max_file);
void hadal_menu_styles_free(HadalMenuStyles* styles);
