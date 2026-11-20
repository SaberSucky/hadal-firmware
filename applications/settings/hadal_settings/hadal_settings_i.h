#pragma once

#include <furi.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/scene_manager.h>
#include <gui/modules/submenu.h>
#include <gui/modules/widget.h>
#include <gui/modules/dialog_ex.h>
#include <gui/modules/variable_item_list.h>
#include <gui/modules/number_input.h>
#include <gui/modules/byte_input.h>
#include <dialogs/dialogs.h>
#include <storage/storage.h>
#include <desktop/desktop.h>
#include <desktop/desktop_settings.h>

#include "scenes/hadal_settings_scene.h"
#include "pack_info.h"
#include "main_menu_edit.h"
#include "menu_styles.h"
#include "keybinds.h"

#define HADAL_MAX_PACKS 40u

typedef enum {
    HadalViewSubmenu,
    HadalViewWidget,
    HadalViewDialog,
    HadalViewVariableList,
    HadalViewNumberInput,
    HadalViewByteInput,
} HadalView;

typedef struct {
    Gui* gui;
    Storage* storage;
    Desktop* desktop;
    DialogsApp* dialogs;
    ViewDispatcher* view_dispatcher;
    SceneManager* scene_manager;

    Submenu* submenu;
    Widget* widget;
    DialogEx* dialog;
    VariableItemList* variable_list;
    NumberInput* number_input;
    ByteInput* byte_input;
    uint8_t bytes[8]; /**< what the byte input edits */

    HadalMainMenu main_menu;
    size_t main_menu_selected; /**< the entry the item page is about */

    DesktopKeybind keybind_selected; /**< the button the action page is about */

    char pack_names[HADAL_MAX_PACKS][HADAL_PACK_NAME_SIZE];
    size_t pack_count;
    size_t pack_chosen; /**< entry of the pack list being looked at, 0 is None */
} HadalSettings;

/** The desktop's settings (menu style, asset pack); a copy the caller frees */
DesktopSettings* hadal_settings_desktop_get(HadalSettings* app);
/** Hands the copy back to the desktop, which applies it, and frees it */
void hadal_settings_desktop_set(HadalSettings* app, DesktopSettings* settings);

/** Submenu callbacks: the item's index becomes the scene's custom event */
void hadal_settings_submenu_callback(void* context, uint32_t index);
