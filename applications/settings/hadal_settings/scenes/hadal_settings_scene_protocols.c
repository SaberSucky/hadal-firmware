#include "../hadal_settings_i.h"

#include <flipper_format/flipper_format.h>
#include <power/power_service/power.h>
#include <loader/loader.h>
#include <furi_hal_subghz.h>

/* Sub-GHz: the range beyond the radio chip's specification. The firmware reads the setting when
 * it starts, so a change asks for a restart. */

#define DANGEROUS_PATH EXT_PATH("subghz/assets/dangerous_settings")
#define DANGEROUS_KEY  "yes_i_want_to_destroy_my_flipper"

/* Frequency Atlas: which region's band plan Remote Analyzer shows. Read whenever the atlas opens,
 * so it applies at once. The atlas's own region picker writes the same file. A region is a file
 * name in /ext/frequency_atlas/ without .txt.
 * Changing it changes nothing else: Sub-GHz frequencies offers "Use <region> frequencies" for
 * the hopper, as a step of its own the user takes there, never as a side effect of this row. */
#define ATLAS_REGION_PATH EXT_PATH("apps_data/remote_analyzer/atlas_region")
#define ATLAS_REGION_KEY  "Region"
#define ATLAS_APP_PATH    EXT_PATH("apps/Sub-GHz/remote_analyzer.fap")
#define ATLAS_APP_ARGS    "atlas"
#define ATLAS_CODE_SIZE   HADAL_REGION_CODE_SIZE

enum {
    ProtocolsExtended,
    ProtocolsRestart,
    ProtocolsFrequencies,
    ProtocolsRegion,
    ProtocolsAtlas,
};

/* Coming back from the atlas, Hadal opens on this row: hadal_settings.c names it. */
_Static_assert(ProtocolsAtlas == HADAL_PROTOCOLS_ROW_ATLAS, "Atlas row moved");

enum {
    EventExtendedOff = 10,
    EventExtendedOn,
    EventRestart,
    EventFrequencies,
    EventAtlas,
};

static const char* const off_on[] = {"Off", "On"};

/** The shipped regions, then Off. A region the atlas picked from another file shows last. */
static const char* const region_codes[] = {"uk", "eu", "us", "off"};
static const char* const region_names[] = {"UK", "EU", "US", "Off"};
#define REGION_KNOWN (sizeof(region_codes) / sizeof(region_codes[0]))
#define REGION_OFF   (REGION_KNOWN - 1)
/** That other region: its code as saved, and in capitals for the list */
static char region_other_code[ATLAS_CODE_SIZE];
static char region_other_name[ATLAS_CODE_SIZE];
static const char* region_values[REGION_KNOWN + 1];

void hadal_settings_atlas_region_get(Storage* storage, char* code) {
    strlcpy(code, "off", ATLAS_CODE_SIZE);
    FuriString* value = furi_string_alloc();
    FlipperFormat* file = flipper_format_file_alloc(storage);
    if(flipper_format_file_open_existing(file, ATLAS_REGION_PATH) &&
       flipper_format_read_string(file, ATLAS_REGION_KEY, value)) {
        furi_string_trim(value);
        if(furi_string_size(value) && furi_string_size(value) < ATLAS_CODE_SIZE) {
            strlcpy(code, furi_string_get_cstr(value), ATLAS_CODE_SIZE);
        }
    }
    flipper_format_free(file);
    furi_string_free(value);
}

static bool atlas_region_set(Storage* storage, const char* code) {
    // The atlas makes its folder the first time it runs, which may not have happened yet
    storage_simply_mkdir(storage, EXT_PATH("apps_data"));
    storage_simply_mkdir(storage, EXT_PATH("apps_data/remote_analyzer"));
    bool saved = false;
    File* file = storage_file_alloc(storage);
    if(storage_file_open(file, ATLAS_REGION_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        FuriString* text = furi_string_alloc_printf(
            "# Frequency Atlas region: Settings > Hadal > Protocols, or the atlas itself.\n"
            "# A file name in /ext/frequency_atlas without .txt, or off.\n"
            "%s: %s\n",
            ATLAS_REGION_KEY,
            code);
        const size_t length = furi_string_size(text);
        saved = storage_file_write(file, furi_string_get_cstr(text), length) == length;
        furi_string_free(text);
    }
    storage_file_free(file);
    return saved;
}

static void region_changed(VariableItem* item) {
    HadalSettings* app = variable_item_get_context(item);
    const uint8_t index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, region_values[index]);
    atlas_region_set(app->storage, index < REGION_KNOWN ? region_codes[index] : region_other_code);
}

/** The Region row: the saved region selected, kept as its own entry when Settings has none */
static void region_add(HadalSettings* app) {
    char code[ATLAS_CODE_SIZE];
    hadal_settings_atlas_region_get(app->storage, code);
    size_t region = REGION_OFF;
    size_t count = REGION_KNOWN;
    for(size_t i = 0; i < REGION_KNOWN; i++) {
        region_values[i] = region_names[i];
        if(strcmp(code, region_codes[i]) == 0) region = i;
    }
    if(region == REGION_OFF && strcmp(code, region_codes[REGION_OFF]) != 0) {
        strlcpy(region_other_code, code, sizeof(region_other_code));
        size_t i = 0;
        for(; code[i] && i + 1 < sizeof(region_other_name); i++) {
            const char c = code[i];
            region_other_name[i] = (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
        }
        region_other_name[i] = '\0';
        region_values[REGION_KNOWN] = region_other_name;
        region = REGION_KNOWN;
        count = REGION_KNOWN + 1;
    }
    VariableItem* item =
        variable_item_list_add(app->variable_list, "Region", count, region_changed, app);
    variable_item_set_current_value_index(item, region);
    variable_item_set_current_value_text(item, region_values[region]);
    variable_item_set_values_text(item, region_values);
}

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

/** "Restart to apply" only while what is saved is not what the radio started with */
static void restart_update(HadalSettings* app, bool on) {
    variable_item_set_hidden(
        variable_item_list_get(app->variable_list, ProtocolsRestart),
        on == furi_hal_subghz_get_dangerous_frequency());
}

static void extended_changed(VariableItem* item) {
    HadalSettings* app = variable_item_get_context(item);
    const uint8_t index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, off_on[index]);
    if(!index) restart_update(app, false); // on waits for the warning to be read
    view_dispatcher_send_custom_event(
        app->view_dispatcher, index ? EventExtendedOn : EventExtendedOff);
}

static void enter_callback(void* context, uint32_t index) {
    HadalSettings* app = context;
    if(index == ProtocolsRestart)
        view_dispatcher_send_custom_event(app->view_dispatcher, EventRestart);
    if(index == ProtocolsFrequencies)
        view_dispatcher_send_custom_event(app->view_dispatcher, EventFrequencies);
    if(index == ProtocolsAtlas)
        view_dispatcher_send_custom_event(app->view_dispatcher, EventAtlas);
}

void hadal_settings_scene_protocols_on_enter(void* context) {
    HadalSettings* app = context;
    variable_item_list_reset(app->variable_list);
    // the header every Hadal page has
    variable_item_list_set_header(app->variable_list, "Protocols");
    VariableItem* item =
        variable_item_list_add(app->variable_list, "Extended Sub-GHz", 2, extended_changed, app);
    const bool on = hadal_settings_extended_range_get(app->storage);
    variable_item_set_current_value_index(item, on);
    variable_item_set_current_value_text(item, off_on[on]);
    variable_item_set_values_text(item, off_on);
    item = variable_item_list_add(app->variable_list, "Restart to apply", 0, NULL, app);
    variable_item_set_indent(item, 1);
    restart_update(app, on);
    item = variable_item_list_add(app->variable_list, "Sub-GHz frequencies", 0, NULL, app);
    variable_item_set_opens(item, true);
    region_add(app);
    item = variable_item_list_add(app->variable_list, "Frequency Atlas", 0, NULL, app);
    variable_item_set_opens(item, true);
    variable_item_list_set_enter_callback(app->variable_list, enter_callback, app);
    // Set only on the way back from the atlas, so that is the row it opens on
    variable_item_list_set_selected_item(
        app->variable_list,
        scene_manager_get_scene_state(app->scene_manager, HadalSettingsSceneProtocols));
    scene_manager_set_scene_state(app->scene_manager, HadalSettingsSceneProtocols, 0);
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
    } else if(event.event == EventAtlas) {
        /* Another app, and the loader runs one at a time, so a plain start would be refused
         * while this one is open. Queued instead: Remote Analyzer with "atlas" (straight to the
         * atlas, Back closes it), then this app again on this row, and this one closes - so Back
         * from the atlas lands here. A missing app gets the loader's own error dialog, and the
         * queue still carries on back to here. */
        Loader* loader = furi_record_open(RECORD_LOADER);
        loader_enqueue_launch(loader, ATLAS_APP_PATH, ATLAS_APP_ARGS, LoaderDeferredLaunchFlagGui);
        FuriString* self = furi_string_alloc();
        if(loader_get_application_launch_path(loader, self)) {
            loader_enqueue_launch(
                loader, furi_string_get_cstr(self), "protocols", LoaderDeferredLaunchFlagGui);
        }
        furi_string_free(self);
        furi_record_close(RECORD_LOADER);
        view_dispatcher_stop(app->view_dispatcher);
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
