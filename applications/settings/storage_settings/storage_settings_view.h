#pragma once

#include <gui/view.h>
#include <storage/storage.h>

/* Storage at a glance: the SD card and the internal storage as two gauges, how full each is and
 * what is free. OK opens a drive's page: what it is, and what can be done with it as buttons. */

typedef enum {
    StorageSettingsActionMount, /**< or unmount, as the card is */
    StorageSettingsActionFormat,
    StorageSettingsActionBenchmark,
    StorageSettingsActionFactoryReset,
} StorageSettingsAction;

typedef void (*StorageSettingsViewCallback)(void* context, StorageSettingsAction action);

typedef struct StorageSettingsView StorageSettingsView;

StorageSettingsView* storage_settings_view_alloc(void);
void storage_settings_view_free(StorageSettingsView* instance);
View* storage_settings_view_get_view(StorageSettingsView* instance);
void storage_settings_view_set_callback(
    StorageSettingsView* instance,
    StorageSettingsViewCallback callback,
    void* context);

/** Reads both drives again (the SD card's free space can take a moment on a big card) */
void storage_settings_view_refresh(StorageSettingsView* instance, Storage* storage);
