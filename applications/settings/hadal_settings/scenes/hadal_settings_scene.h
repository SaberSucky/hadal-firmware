#pragma once

#include <gui/scene_manager.h>

#define ADD_SCENE(prefix, name, id) HadalSettingsScene##id,
typedef enum {
#include "hadal_settings_scene_config.h"
    HadalSettingsSceneNum,
} HadalSettingsScene;
#undef ADD_SCENE

extern const SceneManagerHandlers hadal_settings_scene_handlers;

#define ADD_SCENE(prefix, name, id) void prefix##_scene_##name##_on_enter(void*);
#include "hadal_settings_scene_config.h"
#undef ADD_SCENE

#define ADD_SCENE(prefix, name, id) \
    bool prefix##_scene_##name##_on_event(void* context, SceneManagerEvent event);
#include "hadal_settings_scene_config.h"
#undef ADD_SCENE

#define ADD_SCENE(prefix, name, id) void prefix##_scene_##name##_on_exit(void* context);
#include "hadal_settings_scene_config.h"
#undef ADD_SCENE
