#include "../hadal_settings_i.h"

/* The main menu's look: the built-in list, or a style plugin from the SD card. "* " marks the
 * one in use; choosing one applies it straight away. */

#define STYLE_DEFAULT 0u /* the styles follow, from 1 */

typedef struct {
    HadalMenuStyles styles;
} MenuStyleState;

static MenuStyleState* style_state;

static void style_build(HadalSettings* app, uint32_t selected) {
    DesktopSettings* settings = hadal_settings_desktop_get(app);
    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, "Menu Style");

    const bool none = settings->menu_style[0] == '\0';
    FuriString* label = furi_string_alloc_printf("%sBuilt-in", none ? "* " : "  ");
    submenu_add_item(
        app->submenu,
        furi_string_get_cstr(label),
        STYLE_DEFAULT,
        hadal_settings_submenu_callback,
        app);
    uint32_t current = STYLE_DEFAULT;
    for(size_t i = 0; i < style_state->styles.count; i++) {
        const HadalMenuStyle* style = &style_state->styles.styles[i];
        const bool in_use = furi_string_equal_str(style->file, settings->menu_style);
        if(in_use) current = i + 1;
        furi_string_printf(label, "%s%s", in_use ? "* " : "  ", furi_string_get_cstr(style->name));
        submenu_add_item(
            app->submenu, furi_string_get_cstr(label), i + 1, hadal_settings_submenu_callback, app);
    }
    furi_string_free(label);
    free(settings);
    submenu_set_selected_item(app->submenu, selected == UINT32_MAX ? current : selected);
}

void hadal_settings_scene_menu_style_on_enter(void* context) {
    HadalSettings* app = context;
    style_state = malloc(sizeof(MenuStyleState));
    hadal_menu_styles_scan(
        &style_state->styles, app->storage, sizeof(((DesktopSettings*)0)->menu_style));
    style_build(app, UINT32_MAX);
    view_dispatcher_switch_to_view(app->view_dispatcher, HadalViewSubmenu);
}

bool hadal_settings_scene_menu_style_on_event(void* context, SceneManagerEvent event) {
    HadalSettings* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;
    DesktopSettings* settings = hadal_settings_desktop_get(app);
    if(event.event == STYLE_DEFAULT) {
        settings->menu_style[0] = '\0';
    } else if(event.event - 1 < style_state->styles.count) {
        strlcpy(
            settings->menu_style,
            furi_string_get_cstr(style_state->styles.styles[event.event - 1].file),
            sizeof(settings->menu_style));
    }
    hadal_settings_desktop_set(app, settings);
    style_build(app, event.event);
    return true;
}

void hadal_settings_scene_menu_style_on_exit(void* context) {
    HadalSettings* app = context;
    submenu_reset(app->submenu);
    hadal_menu_styles_free(&style_state->styles);
    free(style_state);
    style_state = NULL;
}
