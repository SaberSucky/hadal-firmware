#include "../hadal_settings_i.h"

static void reset_callback(DialogExResult result, void* context) {
    HadalSettings* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, result);
}

void hadal_settings_scene_main_menu_reset_on_enter(void* context) {
    HadalSettings* app = context;
    dialog_ex_reset(app->dialog);
    dialog_ex_set_header(app->dialog, "Reset the main menu?", 64, 8, AlignCenter, AlignCenter);
    dialog_ex_set_text(
        app->dialog,
        "Back to the firmware's own\norder, with every app in it.",
        64,
        30,
        AlignCenter,
        AlignCenter);
    dialog_ex_set_left_button_text(app->dialog, "Cancel");
    dialog_ex_set_right_button_text(app->dialog, "Reset");
    dialog_ex_set_context(app->dialog, app);
    dialog_ex_set_result_callback(app->dialog, reset_callback);
    view_dispatcher_switch_to_view(app->view_dispatcher, HadalViewDialog);
}

bool hadal_settings_scene_main_menu_reset_on_event(void* context, SceneManagerEvent event) {
    HadalSettings* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;
    if(event.event == DialogExResultRight) {
        hadal_main_menu_reset(&app->main_menu, app->storage);
        scene_manager_set_scene_state(app->scene_manager, HadalSettingsSceneMainMenu, 0);
    }
    scene_manager_previous_scene(app->scene_manager);
    return true;
}

void hadal_settings_scene_main_menu_reset_on_exit(void* context) {
    HadalSettings* app = context;
    dialog_ex_reset(app->dialog);
}
