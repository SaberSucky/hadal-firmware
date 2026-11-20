#include "ibutton_settings_plugin.h"

#include <flipper_application/flipper_application.h>
#include <ibutton/ibutton_settings.h>

static struct {
    iButtonWriteTargetMask mask;
} page;

static void ibutton_settings_write_targets_changed(VariableItem* item) {
    iButtonWriteTarget target = (iButtonWriteTarget)(uintptr_t)variable_item_get_context(item);
    bool enabled = variable_item_get_current_value_index(item) != 0;

    variable_item_set_current_value_text(item, enabled ? "ON" : "OFF");

    if(enabled) {
        page.mask |= IBUTTON_WRITE_TARGET_BIT(target);
    } else {
        page.mask &= ~IBUTTON_WRITE_TARGET_BIT(target);
    }
}

static const char* const write_target_text[] = {"OFF", "ON"};

static void ibutton_settings_write_targets_on_enter(VariableItemList* list) {
    page.mask = ibutton_settings_get_write_targets();

    for(iButtonWriteTarget target = 0; target < iButtonWriteTargetMax; target++) {
        bool enabled = (page.mask & IBUTTON_WRITE_TARGET_BIT(target)) != 0;

        VariableItem* item = variable_item_list_add(
            list,
            ibutton_write_target_name(target),
            2,
            ibutton_settings_write_targets_changed,
            (void*)(uintptr_t)target);

        // a switch: OK turns it over, as Left and Right do
        variable_item_set_values_text(item, write_target_text);
        variable_item_set_current_value_index(item, enabled ? 1 : 0);
        variable_item_set_current_value_text(item, enabled ? "ON" : "OFF");
    }

}

static bool ibutton_settings_write_targets_on_save(void) {
    // Once on the way out rather than on every keypress. Written even when nothing changed:
    // an unreadable file reads back as the defaults this page is already showing, so skipping
    // the save would leave it unreadable forever.
    return ibutton_settings_set_write_targets(page.mask);
}

static const iButtonSettingsPluginPage ibutton_settings_write_targets_page = {
    .on_enter = ibutton_settings_write_targets_on_enter,
    .on_save = ibutton_settings_write_targets_on_save,
};

static const iButtonSettingsPlugin ibutton_settings_plugin = {
    .write_targets = &ibutton_settings_write_targets_page,
};

static const FlipperAppPluginDescriptor ibutton_settings_plugin_descriptor = {
    .appid = IBUTTON_SETTINGS_PLUGIN_APP_ID,
    .ep_api_version = IBUTTON_SETTINGS_PLUGIN_API_VERSION,
    .entry_point = &ibutton_settings_plugin,
};

const FlipperAppPluginDescriptor* ibutton_settings_plugin_ep(void) {
    return &ibutton_settings_plugin_descriptor;
}
