#include "hadal_settings_scene.h"

#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_enter,
void (*const hadal_settings_on_enter_handlers[])(void*) = {
#include "hadal_settings_scene_config.h"
};
#undef ADD_SCENE

#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_event,
bool (*const hadal_settings_on_event_handlers[])(void* context, SceneManagerEvent event) = {
#include "hadal_settings_scene_config.h"
};
#undef ADD_SCENE

#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_exit,
void (*const hadal_settings_on_exit_handlers[])(void* context) = {
#include "hadal_settings_scene_config.h"
};
#undef ADD_SCENE

const SceneManagerHandlers hadal_settings_scene_handlers = {
    .on_enter_handlers = hadal_settings_on_enter_handlers,
    .on_event_handlers = hadal_settings_on_event_handlers,
    .on_exit_handlers = hadal_settings_on_exit_handlers,
    .scene_num = HadalSettingsSceneNum,
};
