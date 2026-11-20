#include "../hadal_settings_i.h"

#include <flipper_format/flipper_format.h>
#include <power/power_service/power.h>

/* Sub-GHz: the range beyond the radio chip's specification. The firmware reads the setting when
 * it starts, so a change asks for a restart. */

#define DANGEROUS_PATH EXT_PATH("subghz/assets/dangerous_settings")
#define DANGEROUS_KEY  "yes_i_want_to_destroy_my_flipper"

enum {
    ProtocolsExtended,
    ProtocolsRestart,
    ProtocolsFrequencies,
};

enum {
    EventExtendedOff = 10,
    EventExtendedOn,
    EventRestart,
    EventFrequencies,
};

static const char* const off_on[] = {"Off", "On"};

bool hadal_settings_extended_range_get(Storage* storage) {
    bool on = false;
    FlipperFormat* file = flipper_format_file_alloc(storage);
    if(flipper_format_file_open_existing(file, DANGEROUS_PATH)) {
        flipper_format_read_bool(file, DANGEROUS_KEY, &on, 1);
    }
    flipper_format_free(file);
    return on;
}

bool hadal_settings_extended_range_set(Storage* storage, bool on) {
    bool saved = false;
    File* file = storage_file_alloc(storage);
    if(storage_file_open(file, DANGEROUS_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        FuriString* text = furi_string_alloc_printf(
            "# Set in Settings > Hadal > Protocols, read when the Flipper starts.\n"
            "# The CC1101 radio chip is specified for 300-348, 386-464 and 778-928 MHz.\n"
            "# On, the range extends to 281-361, 378-481 and 749-962 MHz: outside the\n"
            "# specification the chip may be damaged, and transmitting there may be illegal.\n"
            "%s: %s\n",
            DANGEROUS_KEY,
            on ? "true" : "false");
        const size_t length = furi_string_size(text);
        saved = storage_file_write(file, furi_string_get_cstr(text), length) == length;
        furi_string_free(text);
    }
    storage_file_free(file);
    return saved;
}

static void extended_changed(VariableItem* item) {
    HadalSettings* app = variable_item_get_context(item);
    const uint8_t index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, off_on[index]);
    view_dispatcher_send_custom_event(
        app->view_dispatcher, index ? EventExtendedOn : EventExtendedOff);
}

static void enter_callback(void* context, uint32_t index) {
    HadalSettings* app = context;
    if(index == ProtocolsRestart)
        view_dispatcher_send_custom_event(app->view_dispatcher, EventRestart);
    if(index == ProtocolsFrequencies)
        view_dispatcher_send_custom_event(app->view_dispatcher, EventFrequencies);
}

void hadal_settings_scene_protocols_on_enter(void* context) {
    HadalSettings* app = context;
    variable_item_list_reset(app->variable_list);
    VariableItem* item =
        variable_item_list_add(app->variable_list, "Extended Sub-GHz", 2, extended_changed, app);
    const bool on = hadal_settings_extended_range_get(app->storage);
    variable_item_set_current_value_index(item, on);
    variable_item_set_current_value_text(item, off_on[on]);
    variable_item_set_values_text(item, off_on);
    item = variable_item_list_add(app->variable_list, "Restart to apply", 0, NULL, app);
    item = variable_item_list_add(app->variable_list, "Sub-GHz Frequencies", 0, NULL, app);
    variable_item_list_set_enter_callback(app->variable_list, enter_callback, app);
    view_dispatcher_switch_to_view(app->view_dispatcher, HadalViewVariableList);
}

bool hadal_settings_scene_protocols_on_event(void* context, SceneManagerEvent event) {
    HadalSettings* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;
    if(event.event == EventExtendedOn) {
        // only after reading what it means
        scene_manager_next_scene(app->scene_manager, HadalSettingsSceneProtocolsConfirm);
    } else if(event.event == EventExtendedOff) {
        hadal_settings_extended_range_set(app->storage, false);
    } else if(event.event == EventFrequencies) {
        scene_manager_set_scene_state(app->scene_manager, HadalSettingsSceneSubghzFreqs, 0);
        scene_manager_next_scene(app->scene_manager, HadalSettingsSceneSubghzFreqs);
    } else if(event.event == EventRestart) {
        Power* power = furi_record_open(RECORD_POWER);
        power_reboot(power, PowerBootModeNormal);
        furi_record_close(RECORD_POWER);
    }
    return true;
}

void hadal_settings_scene_protocols_on_exit(void* context) {
    HadalSettings* app = context;
    variable_item_list_reset(app->variable_list);
}
