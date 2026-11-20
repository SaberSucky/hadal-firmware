#include "../hadal_settings_i.h"

bool hadal_settings_extended_range_set(Storage* storage, bool on);

static void confirm_callback(DialogExResult result, void* context) {
    HadalSettings* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, result);
}

void hadal_settings_scene_protocols_confirm_on_enter(void* context) {
    HadalSettings* app = context;
    dialog_ex_reset(app->dialog);
    dialog_ex_set_header(app->dialog, "Extended Sub-GHz range", 64, 0, AlignCenter, AlignTop);
    dialog_ex_set_text(
        app->dialog,
        "Outside its specification the\nchip may be harmed, and\nsending may be illegal.",
        64,
        14,
        AlignCenter,
        AlignTop);
    dialog_ex_set_left_button_text(app->dialog, "Cancel");
    dialog_ex_set_right_button_text(app->dialog, "Turn on");
    dialog_ex_set_context(app->dialog, app);
    dialog_ex_set_result_callback(app->dialog, confirm_callback);
    view_dispatcher_switch_to_view(app->view_dispatcher, HadalViewDialog);
}

bool hadal_settings_scene_protocols_confirm_on_event(void* context, SceneManagerEvent event) {
    HadalSettings* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;
    if(event.event == DialogExResultRight) hadal_settings_extended_range_set(app->storage, true);
    // the list shows what the file now says
    scene_manager_previous_scene(app->scene_manager);
    return true;
}

void hadal_settings_scene_protocols_confirm_on_exit(void* context) {
    HadalSettings* app = context;
    dialog_ex_reset(app->dialog);
}
