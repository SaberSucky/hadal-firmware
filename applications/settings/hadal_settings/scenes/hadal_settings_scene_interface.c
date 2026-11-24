#include "../hadal_settings_i.h"

#include <gui/elements.h>

/* Interface. What is one of a few values is chosen here, in place: the main menu's style and the
 * headers' (OK lists the choices). What needs a page of its own opens one, ">" after its value:
 * the main menu's apps, the asset packs (each shown playing), the desktop's keys, the lock screen,
 * the file browser */

enum {
    RowMainMenu,
    RowMenuStyle,
    RowHeaderStyle,
    RowAssetPacks,
    RowDesktopKeys,
    RowLockScreen,
    RowFileBrowser,
};

static const char* const header_styles[ElementsHeaderStyleCount] = {
    [ElementsHeaderStyleFade] = "Fade",
    [ElementsHeaderStyleRule] = "Rule",
    [ElementsHeaderStyleDots] = "Dots",
    [ElementsHeaderStylePlain] = "Plain",
};

/* The menu styles on the card, while the page is open: their names are the dropdown's choices */
typedef struct {
    HadalMenuStyles styles;
    const char** names; /**< "Built-in", then each style's own name */
} InterfaceState;

static InterfaceState* interface_state;

static void menu_style_changed(VariableItem* item) {
    HadalSettings* app = variable_item_get_context(item);
    const uint8_t index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, interface_state->names[index]);
    DesktopSettings* settings = hadal_settings_desktop_get(app);
    if(index == 0 || index > interface_state->styles.count) {
        settings->menu_style[0] = '\0';
    } else {
        strlcpy(
            settings->menu_style,
            furi_string_get_cstr(interface_state->styles.styles[index - 1].file),
            sizeof(settings->menu_style));
    }
    hadal_settings_desktop_set(app, settings);
}

static void header_style_changed(VariableItem* item) {
    HadalSettings* app = variable_item_get_context(item);
    const uint8_t index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, header_styles[index]);
    // drawn so at once, this page's own header first; the desktop keeps it
    elements_set_header_style(index);
    DesktopSettings* settings = hadal_settings_desktop_get(app);
    settings->header_style = index;
    hadal_settings_desktop_set(app, settings);
}

static VariableItem* interface_add_opens(HadalSettings* app, const char* label, const char* value) {
    VariableItem* item = variable_item_list_add(app->variable_list, label, 1, NULL, app);
    variable_item_set_opens(item, true);
    if(value) variable_item_set_current_value_text(item, value);
    return item;
}

void hadal_settings_scene_interface_on_enter(void* context) {
    HadalSettings* app = context;
    interface_state = malloc(sizeof(InterfaceState));
    hadal_menu_styles_scan(
        &interface_state->styles, app->storage, sizeof(((DesktopSettings*)0)->menu_style));
    const size_t style_count = MIN(interface_state->styles.count, (size_t)UINT8_MAX - 1);
    interface_state->names = malloc((style_count + 1) * sizeof(const char*));
    interface_state->names[0] = "Built-in";
    for(size_t i = 0; i < style_count; i++) {
        interface_state->names[i + 1] = furi_string_get_cstr(interface_state->styles.styles[i].name);
    }

    DesktopSettings* settings = hadal_settings_desktop_get(app);
    VariableItemList* list = app->variable_list;
    variable_item_list_reset(list);
    variable_item_list_set_header(list, "Interface");

    interface_add_opens(app, "Main menu", NULL);

    uint8_t style = 0;
    for(size_t i = 0; i < style_count; i++) {
        if(furi_string_equal_str(interface_state->styles.styles[i].file, settings->menu_style)) {
            style = i + 1;
        }
    }
    VariableItem* item =
        variable_item_list_add(list, "Menu style", style_count + 1, menu_style_changed, app);
    variable_item_set_current_value_index(item, style);
    variable_item_set_current_value_text(item, interface_state->names[style]);
    variable_item_set_values_text(item, interface_state->names);

    const uint8_t header =
        settings->header_style < ElementsHeaderStyleCount ? settings->header_style : 0;
    item = variable_item_list_add(
        list, "Header style", ElementsHeaderStyleCount, header_style_changed, app);
    variable_item_set_current_value_index(item, header);
    variable_item_set_current_value_text(item, header_styles[header]);
    variable_item_set_values_text(item, header_styles);

    interface_add_opens(
        app,
        "Asset packs",
        settings->active_asset_pack[0] ? settings->active_asset_pack : "Built-in");
    interface_add_opens(app, "Desktop keys", NULL);
    interface_add_opens(app, "Lock screen", NULL);
    interface_add_opens(app, "File browser", NULL);
    free(settings);

    variable_item_list_set_enter_callback(list, hadal_settings_submenu_callback, app);
    variable_item_list_set_selected_item(
        list, scene_manager_get_scene_state(app->scene_manager, HadalSettingsSceneInterface));
    view_dispatcher_switch_to_view(app->view_dispatcher, HadalViewVariableList);
}

bool hadal_settings_scene_interface_on_event(void* context, SceneManagerEvent event) {
    HadalSettings* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;
    scene_manager_set_scene_state(app->scene_manager, HadalSettingsSceneInterface, event.event);
    switch(event.event) {
    case RowMainMenu:
        scene_manager_set_scene_state(app->scene_manager, HadalSettingsSceneMainMenu, 0);
        scene_manager_next_scene(app->scene_manager, HadalSettingsSceneMainMenu);
        break;
    case RowAssetPacks:
        scene_manager_next_scene(app->scene_manager, HadalSettingsSceneAssetPack);
        break;
    case RowDesktopKeys:
        scene_manager_set_scene_state(app->scene_manager, HadalSettingsSceneDesktopKeys, 0);
        scene_manager_next_scene(app->scene_manager, HadalSettingsSceneDesktopKeys);
        break;
    case RowLockScreen:
        scene_manager_next_scene(app->scene_manager, HadalSettingsSceneLockScreen);
        break;
    case RowFileBrowser:
        scene_manager_next_scene(app->scene_manager, HadalSettingsSceneFileBrowser);
        break;
    default:
        break; // the two styles change in place
    }
    return true;
}

void hadal_settings_scene_interface_on_exit(void* context) {
    HadalSettings* app = context;
    variable_item_list_reset(app->variable_list);
    hadal_menu_styles_free(&interface_state->styles);
    free(interface_state->names);
    free(interface_state);
    interface_state = NULL;
}
