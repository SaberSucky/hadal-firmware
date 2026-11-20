/* Host stand-in for lib/flipper_application/flipper_application.h: only what the asset pack
 * manager uses. The test driver gives the "app" (asset_packs_host.c). */
#pragma once

#include <stddef.h>

typedef struct FlipperApplication FlipperApplication;

typedef void (*FlipperApplicationObjectCallback)(const char* name, const void* address, void* context);

void flipper_application_for_each_object(
    FlipperApplication* app,
    size_t object_size,
    FlipperApplicationObjectCallback callback,
    void* context);
