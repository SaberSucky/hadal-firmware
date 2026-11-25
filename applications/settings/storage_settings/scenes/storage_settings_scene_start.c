#include "../storage_settings.h"

/* The drives at a glance (storage_settings_view.h); a drive's buttons go on to the pages that
 * were the list's: unmount or mount, format, the speed test, the factory reset */

static void storage_settings_scene_start_callback(void* context, StorageSettingsAction action) {
    StorageSettings* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, action);
}

void storage_settings_scene_start_on_enter(void* context) {
    StorageSettings* app = context;
    storage_settings_view_set_callback(
        app->storage_view, storage_settings_scene_start_callback, app);
    // read again each time: back from a format or an unmount, it shows what is now
    storage_settings_view_refresh(app->storage_view, app->fs_api);
    view_dispatcher_switch_to_view(app->view_dispatcher, StorageSettingsViewDrives);
}

bool storage_settings_scene_start_on_event(void* context, SceneManagerEvent event) {
    StorageSettings* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;
    switch(event.event) {
    case StorageSettingsActionMount:
        scene_manager_next_scene(app->scene_manager, StorageSettingsUnmountConfirm);
        break;
    case StorageSettingsActionFormat:
        scene_manager_next_scene(app->scene_manager, StorageSettingsFormatConfirm);
        break;
    case StorageSettingsActionBenchmark:
        scene_manager_next_scene(app->scene_manager, StorageSettingsBenchmarkConfirm);
        break;
    case StorageSettingsActionFactoryReset:
        scene_manager_next_scene(app->scene_manager, StorageSettingsFactoryReset);
        break;
    default:
        return false;
    }
    return true;
}

void storage_settings_scene_start_on_exit(void* context) {
    StorageSettings* app = context;
    storage_settings_view_set_callback(app->storage_view, NULL, NULL);
}
