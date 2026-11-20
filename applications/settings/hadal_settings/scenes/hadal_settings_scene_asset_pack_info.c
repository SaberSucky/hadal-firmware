#include "../hadal_settings_i.h"

/* What a pack holds, and a button to use it */

#define EVENT_USE 1u

static void use_callback(GuiButtonType result, InputType type, void* context) {
    HadalSettings* app = context;
    if(result == GuiButtonTypeCenter && type == InputTypeShort) {
        view_dispatcher_send_custom_event(app->view_dispatcher, EVENT_USE);
    }
}

void hadal_settings_scene_asset_pack_info_on_enter(void* context) {
    HadalSettings* app = context;
    DesktopSettings* settings = hadal_settings_desktop_get(app);
    const bool none = app->pack_chosen == 0;
    const char* name = none ? "Built-in" : app->pack_names[app->pack_chosen - 1];
    const bool current = none ? settings->active_asset_pack[0] == '\0' :
                                strcmp(name, settings->active_asset_pack) == 0;
    free(settings);

    widget_reset(app->widget);
    widget_add_string_element(app->widget, 64, 2, AlignCenter, AlignTop, FontPrimary, name);
    FuriString* text = furi_string_alloc();
    if(none) {
        furi_string_set(text, "The animations, icons and\nfonts the firmware has.");
    } else {
        HadalPackInfo info;
        hadal_pack_info_scan(app->storage, name, &info);
        furi_string_printf(
            text, "Animations: %u\nIcons: %u\nFonts: %u", info.animations, info.icons, info.fonts);
        if(!info.animations && !info.icons && !info.fonts) {
            furi_string_cat(text, "\nNothing in it the firmware\ncan use.");
        }
    }
    widget_add_string_multiline_element(
        app->widget, 64, 16, AlignCenter, AlignTop, FontSecondary, furi_string_get_cstr(text));
    furi_string_free(text);
    widget_add_button_element(
        app->widget,
        GuiButtonTypeCenter,
        current ? "In use" : "Use",
        current ? NULL : use_callback,
        app);
    view_dispatcher_switch_to_view(app->view_dispatcher, HadalViewWidget);
}

bool hadal_settings_scene_asset_pack_info_on_event(void* context, SceneManagerEvent event) {
    HadalSettings* app = context;
    if(event.type != SceneManagerEventTypeCustom || event.event != EVENT_USE) return false;
    DesktopSettings* settings = hadal_settings_desktop_get(app);
    strlcpy(
        settings->active_asset_pack,
        app->pack_chosen == 0 ? "" : app->pack_names[app->pack_chosen - 1],
        sizeof(settings->active_asset_pack));
    hadal_settings_desktop_set(app, settings);
    scene_manager_previous_scene(app->scene_manager);
    return true;
}

void hadal_settings_scene_asset_pack_info_on_exit(void* context) {
    HadalSettings* app = context;
    widget_reset(app->widget);
}
