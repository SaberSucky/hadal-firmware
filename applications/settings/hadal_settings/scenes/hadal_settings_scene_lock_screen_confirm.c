#include "../hadal_settings_i.h"

void hadal_settings_lock_screen_set_erase(HadalSettings* app);

static void confirm_callback(DialogExResult result, void* context) {
    HadalSettings* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, result);
}

void hadal_settings_scene_lock_screen_confirm_on_enter(void* context) {
    HadalSettings* app = context;
    dialog_ex_reset(app->dialog);
    dialog_ex_set_header(app->dialog, "Erase on wrong PINs?", 64, 0, AlignCenter, AlignTop);
    dialog_ex_set_text(
        app->dialog,
        "It erases settings, PIN,\npairings and saved keys.\nThe SD card is kept.",
        64,
        14,
        AlignCenter,
        AlignTop);
    dialog_ex_set_left_button_text(app->dialog, "Cancel");
    dialog_ex_set_right_button_text(app->dialog, "Erase");
    dialog_ex_set_context(app->dialog, app);
    dialog_ex_set_result_callback(app->dialog, confirm_callback);
    view_dispatcher_switch_to_view(app->view_dispatcher, HadalViewDialog);
}

bool hadal_settings_scene_lock_screen_confirm_on_event(void* context, SceneManagerEvent event) {
    HadalSettings* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;
    if(event.event == DialogExResultRight) hadal_settings_lock_screen_set_erase(app);
    // the list shows what the settings now say
    scene_manager_previous_scene(app->scene_manager);
    return true;
}

void hadal_settings_scene_lock_screen_confirm_on_exit(void* context) {
    HadalSettings* app = context;
    dialog_ex_reset(app->dialog);
}
