#include "hadal_settings_i.h"

#define TAG "HadalSettings"

DesktopSettings* hadal_settings_desktop_get(HadalSettings* app) {
    DesktopSettings* settings = malloc(sizeof(DesktopSettings));
    desktop_api_get_settings(app->desktop, settings);
    return settings;
}

void hadal_settings_desktop_set(HadalSettings* app, DesktopSettings* settings) {
    desktop_api_set_settings(app->desktop, settings);
    free(settings);
}

void hadal_settings_submenu_callback(void* context, uint32_t index) {
    HadalSettings* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

static bool hadal_settings_custom_event(void* context, uint32_t event) {
    HadalSettings* app = context;
    return scene_manager_handle_custom_event(app->scene_manager, event);
}

static bool hadal_settings_back_event(void* context) {
    HadalSettings* app = context;
    return scene_manager_handle_back_event(app->scene_manager);
}

static HadalSettings* hadal_settings_alloc(void) {
    HadalSettings* app = malloc(sizeof(HadalSettings));
    memset(app, 0, sizeof(*app));

    app->gui = furi_record_open(RECORD_GUI);
    app->storage = furi_record_open(RECORD_STORAGE);
    app->desktop = furi_record_open(RECORD_DESKTOP);
    app->dialogs = furi_record_open(RECORD_DIALOGS);

    app->view_dispatcher = view_dispatcher_alloc();
    app->scene_manager = scene_manager_alloc(&hadal_settings_scene_handlers, app);
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(app->view_dispatcher, hadal_settings_custom_event);
    view_dispatcher_set_navigation_event_callback(app->view_dispatcher, hadal_settings_back_event);

    app->submenu = submenu_alloc();
    app->widget = widget_alloc();
    app->dialog = dialog_ex_alloc();
    app->variable_list = variable_item_list_alloc();
    app->number_input = number_input_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, HadalViewNumberInput, number_input_get_view(app->number_input));
    view_dispatcher_add_view(
        app->view_dispatcher, HadalViewSubmenu, submenu_get_view(app->submenu));
    view_dispatcher_add_view(app->view_dispatcher, HadalViewWidget, widget_get_view(app->widget));
    view_dispatcher_add_view(
        app->view_dispatcher, HadalViewDialog, dialog_ex_get_view(app->dialog));
    view_dispatcher_add_view(
        app->view_dispatcher,
        HadalViewVariableList,
        variable_item_list_get_view(app->variable_list));
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    hadal_main_menu_load(&app->main_menu, app->storage);
    return app;
}

static void hadal_settings_free(HadalSettings* app) {
    view_dispatcher_remove_view(app->view_dispatcher, HadalViewSubmenu);
    view_dispatcher_remove_view(app->view_dispatcher, HadalViewWidget);
    view_dispatcher_remove_view(app->view_dispatcher, HadalViewDialog);
    view_dispatcher_remove_view(app->view_dispatcher, HadalViewVariableList);
    view_dispatcher_remove_view(app->view_dispatcher, HadalViewNumberInput);
    number_input_free(app->number_input);
    submenu_free(app->submenu);
    widget_free(app->widget);
    dialog_ex_free(app->dialog);
    variable_item_list_free(app->variable_list);
    scene_manager_free(app->scene_manager);
    view_dispatcher_free(app->view_dispatcher);

    hadal_main_menu_free(&app->main_menu);

    furi_record_close(RECORD_DIALOGS);
    furi_record_close(RECORD_DESKTOP);
    furi_record_close(RECORD_STORAGE);
    furi_record_close(RECORD_GUI);
    free(app);
}

int32_t hadal_settings_app(void* p) {
    UNUSED(p);
    HadalSettings* app = hadal_settings_alloc();
    scene_manager_next_scene(app->scene_manager, HadalSettingsSceneStart);
    view_dispatcher_run(app->view_dispatcher);
    hadal_settings_free(app);
    return 0;
}
