#include "../hadal_settings_i.h"

#include <gui/elements.h>

/* Header Style: how every page's header is drawn. This page's own header shows each one as it
 * is chosen; it is kept as the desktop's setting */

static const char* const style_names[ElementsHeaderStyleCount] = {
    [ElementsHeaderStyleFade] = "Fade",
    [ElementsHeaderStyleRule] = "Rule",
    [ElementsHeaderStyleDots] = "Dots",
    [ElementsHeaderStylePlain] = "Plain",
};

static void style_changed(VariableItem* item) {
    HadalSettings* app = variable_item_get_context(item);
    const uint8_t index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, style_names[index]);
    // drawn so at once, this page's header first; the desktop keeps it
    elements_set_header_style(index);
    DesktopSettings* settings = hadal_settings_desktop_get(app);
    settings->header_style = index;
    hadal_settings_desktop_set(app, settings);
}

void hadal_settings_scene_header_style_on_enter(void* context) {
    HadalSettings* app = context;
    DesktopSettings* settings = hadal_settings_desktop_get(app);
    const uint8_t style =
        settings->header_style < ElementsHeaderStyleCount ? settings->header_style : 0;
    free(settings);

    variable_item_list_reset(app->variable_list);
    variable_item_list_set_header(app->variable_list, "Header Style");
    VariableItem* item = variable_item_list_add(
        app->variable_list, "Style", ElementsHeaderStyleCount, style_changed, app);
    variable_item_set_current_value_index(item, style);
    variable_item_set_current_value_text(item, style_names[style]);
    variable_item_set_values_text(item, style_names);
    view_dispatcher_switch_to_view(app->view_dispatcher, HadalViewVariableList);
}

bool hadal_settings_scene_header_style_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void hadal_settings_scene_header_style_on_exit(void* context) {
    HadalSettings* app = context;
    variable_item_list_reset(app->variable_list);
}
