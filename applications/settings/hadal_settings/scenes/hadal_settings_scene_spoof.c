#include "../hadal_settings_i.h"
#include "../../../services/namechanger/namechanger.h"

#include <flipper_format/flipper_format.h>
#include <furi_hal_random.h>
#include <furi_hal_version.h>
#include <power/power_service/power.h>

/* Spoofing: who the Flipper says it is. "Own" is the Flipper itself; a profile is another
 * identity, kept to switch back to: a Bluetooth address (its own, a new random one at every start,
 * or a fixed one), a serial (its own or a fixed one) and a name (Desktop's, or its own). The one
 * chosen is written for the start to apply (namechanger.c): a restart is asked for, and offered
 * only while there is one still to come. */

#define SPOOF_PROFILES_MAX 5
#define SPOOF_NAME_SIZE    9 // 8 letters and digits, and the 0
#define SPOOF_LABEL_SIZE   12

typedef enum {
    SpoofMacOwn,
    SpoofMacRandom,
    SpoofMacFixed,
    SpoofMacModes,
} SpoofMacMode;

typedef struct {
    uint8_t mac_mode; // SpoofMacMode
    uint8_t mac[6]; // most significant first, as written and shown
    bool has_serial;
    uint8_t serial[8];
    char name[SPOOF_NAME_SIZE]; // "" for Desktop's
} SpoofProfile;

typedef enum {
    SpoofOverList,
    SpoofOverByteInput,
    SpoofOverTextInput,
    SpoofOverDialog,
} SpoofOver;

/* What a row of the list does with OK, by its place */
typedef enum {
    RowNone,
    RowMacValue,
    RowSerialValue,
    RowName,
    RowDelete,
    RowNew,
    RowRestart,
} SpoofRow;

enum {
    EventRow = 100, // + the row's place
    EventIdentity = 200,
    EventMacMode,
    EventSerialMode,
    EventMacEntered,
    EventSerialEntered,
    EventNameEntered,
    EventDeleteYes,
    EventDeleteNo,
};

typedef struct {
    uint32_t generation;
    uint8_t active; // 0 Own, n the profile n
    uint8_t count;
    SpoofProfile profiles[SPOOF_PROFILES_MAX];

    // the page
    char labels[SPOOF_PROFILES_MAX][SPOOF_LABEL_SIZE];
    const char* identities[SPOOF_PROFILES_MAX + 1];
    uint8_t rows[12]; // SpoofRow, by place
    uint8_t row_count;
    // the list keeps its labels' pointers: the address and the serial live as long as the page
    char mac_text[18];
    char serial_text[17];
    SpoofOver over;
} Spoof;

static Spoof* spoof;

static const char* const mac_modes[SpoofMacModes] = {"Own", "Random", "Fixed"};
static const char* const serial_modes[] = {"Own", "Fixed"};

static SpoofProfile* spoof_current(void) {
    return spoof->active ? &spoof->profiles[spoof->active - 1] : NULL;
}

static void spoof_random_mac(uint8_t mac[6]) {
    furi_hal_random_fill_buf(mac, 6);
    // one device, not a group, and marked as made up: what a random address should say
    mac[0] = (mac[0] & 0xFC) | 0x02;
}

/* ---- the file ---- */

static void spoof_key(FuriString* key, uint8_t profile, const char* what) {
    furi_string_printf(key, "P%u %s", profile, what);
}

static void spoof_load(Storage* storage) {
    memset(spoof, 0, sizeof(*spoof));
    FlipperFormat* file = flipper_format_file_alloc(storage);
    FuriString* text = furi_string_alloc();
    FuriString* key = furi_string_alloc();
    uint32_t version = 0;
    do {
        if(!flipper_format_file_open_existing(file, HADAL_SPOOF_PATH)) break;
        if(!flipper_format_read_header(file, text, &version)) break;
        if(furi_string_cmp_str(text, HADAL_SPOOF_HEADER)) break;
        flipper_format_read_uint32(file, HADAL_SPOOF_GENERATION, &spoof->generation, 1);

        if(version == 1) {
            // Mac and Serial alone: they become the first profile, and stay in use
            SpoofProfile* profile = &spoof->profiles[0];
            flipper_format_rewind(file);
            if(flipper_format_read_hex(file, HADAL_SPOOF_MAC, profile->mac, 6)) {
                profile->mac_mode = SpoofMacFixed;
            }
            flipper_format_rewind(file);
            profile->has_serial = flipper_format_read_hex(file, HADAL_SPOOF_SERIAL, profile->serial, 8);
            if(profile->mac_mode != SpoofMacOwn || profile->has_serial) {
                spoof->count = 1;
                spoof->active = 1;
            }
            break;
        }
        if(version != HADAL_SPOOF_VERSION) break;

        uint32_t value = 0;
        flipper_format_rewind(file);
        if(flipper_format_read_uint32(file, "Profiles", &value, 1)) {
            spoof->count = MIN(value, (uint32_t)SPOOF_PROFILES_MAX);
        }
        flipper_format_rewind(file);
        if(flipper_format_read_uint32(file, "Active", &value, 1) && value <= spoof->count) {
            spoof->active = value;
        }
        for(uint8_t i = 0; i < spoof->count; i++) {
            SpoofProfile* profile = &spoof->profiles[i];
            spoof_key(key, i + 1, "mac mode");
            flipper_format_rewind(file);
            if(flipper_format_read_uint32(file, furi_string_get_cstr(key), &value, 1) &&
               value < SpoofMacModes) {
                profile->mac_mode = value;
            }
            spoof_key(key, i + 1, "mac");
            flipper_format_rewind(file);
            if(!flipper_format_read_hex(file, furi_string_get_cstr(key), profile->mac, 6)) {
                spoof_random_mac(profile->mac);
                if(profile->mac_mode == SpoofMacFixed) profile->mac_mode = SpoofMacOwn;
            }
            spoof_key(key, i + 1, "serial");
            flipper_format_rewind(file);
            profile->has_serial =
                flipper_format_read_hex(file, furi_string_get_cstr(key), profile->serial, 8);
            spoof_key(key, i + 1, "name");
            flipper_format_rewind(file);
            if(flipper_format_read_string(file, furi_string_get_cstr(key), text)) {
                strlcpy(profile->name, furi_string_get_cstr(text), SPOOF_NAME_SIZE);
            }
        }
    } while(false);
    furi_string_free(key);
    furi_string_free(text);
    flipper_format_free(file);
}

/* Written whole: the identity in use as the start reads it, then every profile */
static void spoof_save(Storage* storage) {
    spoof->generation++;
    storage_simply_mkdir(storage, EXT_PATH("apps_data"));
    storage_simply_mkdir(storage, EXT_PATH("apps_data/spoof"));
    FlipperFormat* file = flipper_format_file_alloc(storage);
    FuriString* key = furi_string_alloc();
    do {
        if(!flipper_format_file_open_always(file, HADAL_SPOOF_PATH)) break;
        if(!flipper_format_write_header_cstr(file, HADAL_SPOOF_HEADER, HADAL_SPOOF_VERSION)) break;
        flipper_format_write_comment_cstr(file, "Set in Settings > Hadal > Spoofing");
        flipper_format_write_uint32(file, HADAL_SPOOF_GENERATION, &spoof->generation, 1);

        const SpoofProfile* current = spoof_current();
        if(current) {
            flipper_format_write_comment_cstr(file, "The identity the Flipper starts as");
            if(current->mac_mode == SpoofMacRandom) {
                const bool random = true;
                flipper_format_write_bool(file, HADAL_SPOOF_RANDOM, &random, 1);
            } else if(current->mac_mode == SpoofMacFixed) {
                flipper_format_write_hex(file, HADAL_SPOOF_MAC, current->mac, 6);
            }
            if(current->has_serial) {
                flipper_format_write_hex(file, HADAL_SPOOF_SERIAL, current->serial, 8);
            }
            if(current->name[0]) flipper_format_write_string_cstr(file, HADAL_SPOOF_NAME, current->name);
        }

        flipper_format_write_comment_cstr(file, "The profiles to choose from; Active 0 is Own");
        uint32_t value = spoof->active;
        flipper_format_write_uint32(file, "Active", &value, 1);
        value = spoof->count;
        flipper_format_write_uint32(file, "Profiles", &value, 1);
        for(uint8_t i = 0; i < spoof->count; i++) {
            const SpoofProfile* profile = &spoof->profiles[i];
            value = profile->mac_mode;
            spoof_key(key, i + 1, "mac mode");
            flipper_format_write_uint32(file, furi_string_get_cstr(key), &value, 1);
            spoof_key(key, i + 1, "mac");
            flipper_format_write_hex(file, furi_string_get_cstr(key), profile->mac, 6);
            if(profile->has_serial) {
                spoof_key(key, i + 1, "serial");
                flipper_format_write_hex(file, furi_string_get_cstr(key), profile->serial, 8);
            }
            if(profile->name[0]) {
                spoof_key(key, i + 1, "name");
                flipper_format_write_string_cstr(file, furi_string_get_cstr(key), profile->name);
            }
        }
    } while(false);
    furi_string_free(key);
    flipper_format_free(file);
}

/* Whether the start has yet to apply what is saved: it shares the Generation it applied */
static bool spoof_restart_pending(void) {
    if(!furi_record_exists(RECORD_NAMECHANGER_DONE)) return false;
    const uint8_t* applied = furi_record_open(RECORD_NAMECHANGER_DONE);
    const bool pending = *applied != (uint8_t)spoof->generation;
    furi_record_close(RECORD_NAMECHANGER_DONE);
    return pending;
}

/* ---- the page ---- */

static void spoof_send(void* context, uint32_t event) {
    HadalSettings* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, event);
}

// a change asks for the page again, built once the list is done with its input
static void identity_changed(VariableItem* item) {
    spoof->active = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, spoof->identities[spoof->active]);
    spoof_send(variable_item_get_context(item), EventIdentity);
}

static void mac_mode_changed(VariableItem* item) {
    const uint8_t index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, mac_modes[index]);
    spoof_current()->mac_mode = index;
    spoof_send(variable_item_get_context(item), EventMacMode);
}

static void serial_mode_changed(VariableItem* item) {
    const uint8_t index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, serial_modes[index]);
    SpoofProfile* profile = spoof_current();
    if(index && !profile->has_serial) memcpy(profile->serial, furi_hal_version_uid(), 8);
    profile->has_serial = index;
    spoof_send(variable_item_get_context(item), EventSerialMode);
}

static void row_entered(void* context, uint32_t index) {
    spoof_send(context, EventRow + index);
}

static VariableItem* spoof_row(
    HadalSettings* app,
    SpoofRow row,
    const char* label,
    uint8_t values,
    VariableItemChangeCallback changed) {
    const uint8_t place = spoof->row_count++;
    if(place < COUNT_OF(spoof->rows)) spoof->rows[place] = row;
    return variable_item_list_add(app->variable_list, label, values, changed, app);
}

static void spoof_show(HadalSettings* app, uint8_t selected) {
    spoof->over = SpoofOverList;
    memset(spoof->rows, RowNone, sizeof(spoof->rows));
    spoof->row_count = 0;
    spoof->identities[0] = "Own";
    for(uint8_t i = 0; i < spoof->count; i++) {
        if(spoof->profiles[i].name[0]) {
            strlcpy(spoof->labels[i], spoof->profiles[i].name, SPOOF_LABEL_SIZE);
        } else {
            snprintf(spoof->labels[i], SPOOF_LABEL_SIZE, "Profile %u", i + 1);
        }
        spoof->identities[i + 1] = spoof->labels[i];
    }

    VariableItemList* list = app->variable_list;
    variable_item_list_reset(list);
    variable_item_list_set_header(list, "Spoofing");

    VariableItem* item = spoof_row(app, RowNone, "Identity", spoof->count + 1, identity_changed);
    variable_item_set_current_value_index(item, spoof->active);
    variable_item_set_current_value_text(item, spoof->identities[spoof->active]);
    variable_item_set_values_text(item, spoof->identities);

    SpoofProfile* profile = spoof_current();
    if(profile) {
        item = spoof_row(app, RowNone, "MAC address", SpoofMacModes, mac_mode_changed);
        variable_item_set_indent(item, 1);
        variable_item_set_current_value_index(item, profile->mac_mode);
        variable_item_set_current_value_text(item, mac_modes[profile->mac_mode]);
        variable_item_set_values_text(item, mac_modes);
        if(profile->mac_mode == SpoofMacFixed) {
            snprintf(
                spoof->mac_text,
                sizeof(spoof->mac_text),
                "%02X:%02X:%02X:%02X:%02X:%02X",
                profile->mac[0],
                profile->mac[1],
                profile->mac[2],
                profile->mac[3],
                profile->mac[4],
                profile->mac[5]);
            item = spoof_row(app, RowMacValue, spoof->mac_text, 1, NULL);
            variable_item_set_indent(item, 2);
            variable_item_set_opens(item, true);
        }

        item = spoof_row(app, RowNone, "Serial", COUNT_OF(serial_modes), serial_mode_changed);
        variable_item_set_indent(item, 1);
        variable_item_set_current_value_index(item, profile->has_serial);
        variable_item_set_current_value_text(item, serial_modes[profile->has_serial]);
        variable_item_set_values_text(item, serial_modes);
        if(profile->has_serial) {
            for(size_t i = 0; i < 8; i++) {
                snprintf(spoof->serial_text + i * 2, 3, "%02X", profile->serial[i]);
            }
            item = spoof_row(app, RowSerialValue, spoof->serial_text, 1, NULL);
            variable_item_set_indent(item, 2);
            variable_item_set_opens(item, true);
        }

        item = spoof_row(app, RowName, "Flipper name", 1, NULL);
        variable_item_set_indent(item, 1);
        variable_item_set_opens(item, true);
        variable_item_set_current_value_text(item, profile->name[0] ? profile->name : "Own");

        item = spoof_row(app, RowDelete, "Delete profile", 0, NULL);
        variable_item_set_indent(item, 1);
    }

    if(spoof->count < SPOOF_PROFILES_MAX) spoof_row(app, RowNew, "New profile", 0, NULL);
    // offered only while what is saved has yet to be applied
    if(spoof_restart_pending()) spoof_row(app, RowRestart, "Restart to apply", 0, NULL);

    variable_item_list_set_enter_callback(list, row_entered, app);
    variable_item_list_set_selected_item(list, MIN(selected, spoof->row_count - 1));
    view_dispatcher_switch_to_view(app->view_dispatcher, HadalViewVariableList);
}

static uint8_t spoof_selected(HadalSettings* app) {
    return variable_item_list_get_selected_item_index(app->variable_list);
}

static void byte_input_done(void* context) {
    HadalSettings* app = context;
    spoof_send(
        app, scene_manager_get_scene_state(app->scene_manager, HadalSettingsSceneSpoof));
}

static void open_byte_input(HadalSettings* app, const char* header, size_t size, uint32_t event) {
    scene_manager_set_scene_state(app->scene_manager, HadalSettingsSceneSpoof, event);
    byte_input_set_header_text(app->byte_input, header);
    byte_input_set_result_callback(app->byte_input, byte_input_done, NULL, app, app->bytes, size);
    spoof->over = SpoofOverByteInput;
    view_dispatcher_switch_to_view(app->view_dispatcher, HadalViewByteInput);
}

static void name_done(void* context) {
    spoof_send(context, EventNameEntered);
}

// empty (Desktop's name), or 2 to 8 letters and digits, as the start takes it
static bool name_valid(const char* text, FuriString* error, void* context) {
    UNUSED(context);
    const size_t size = strlen(text);
    bool valid = size == 0 || (size >= 2 && size <= 8);
    for(const char* c = text; valid && *c; c++) {
        valid = (*c >= '0' && *c <= '9') || (*c >= 'A' && *c <= 'Z') || (*c >= 'a' && *c <= 'z');
    }
    if(!valid) furi_string_set_str(error, "2 to 8 letters\nand digits,\nor empty");
    return valid;
}

static void open_name_input(HadalSettings* app) {
    strlcpy(app->text, spoof_current()->name, SPOOF_NAME_SIZE);
    text_input_reset(app->text_input);
    text_input_set_header_text(app->text_input, "Name (empty: Desktop's)");
    text_input_set_validator(app->text_input, name_valid, app);
    text_input_set_result_callback(
        app->text_input, name_done, app, app->text, SPOOF_NAME_SIZE, false);
    spoof->over = SpoofOverTextInput;
    view_dispatcher_switch_to_view(app->view_dispatcher, HadalViewTextInput);
}

static void delete_result(DialogExResult result, void* context) {
    spoof_send(context, result == DialogExResultRight ? EventDeleteYes : EventDeleteNo);
}

static void open_delete_dialog(HadalSettings* app) {
    dialog_ex_reset(app->dialog);
    dialog_ex_set_header(app->dialog, "Delete this profile?", 64, 10, AlignCenter, AlignCenter);
    dialog_ex_set_text(
        app->dialog,
        spoof->identities[spoof->active],
        64,
        30,
        AlignCenter,
        AlignCenter);
    dialog_ex_set_left_button_text(app->dialog, "Keep");
    dialog_ex_set_right_button_text(app->dialog, "Delete");
    dialog_ex_set_result_callback(app->dialog, delete_result);
    dialog_ex_set_context(app->dialog, app);
    spoof->over = SpoofOverDialog;
    view_dispatcher_switch_to_view(app->view_dispatcher, HadalViewDialog);
}

void hadal_settings_scene_spoof_on_enter(void* context) {
    HadalSettings* app = context;
    spoof = malloc(sizeof(Spoof));
    spoof_load(app->storage);
    spoof_show(app, 0);
}

bool hadal_settings_scene_spoof_on_event(void* context, SceneManagerEvent event) {
    HadalSettings* app = context;
    if(event.type == SceneManagerEventTypeBack && spoof->over != SpoofOverList) {
        // Back from a keyboard or the question: the page, unchanged
        spoof_show(app, spoof_selected(app));
        return true;
    }
    if(event.type != SceneManagerEventTypeCustom) return false;

    SpoofProfile* profile = spoof_current();
    uint8_t selected = spoof_selected(app);
    switch(event.event) {
    case EventIdentity:
        selected = 0;
        break;
    case EventMacMode:
        // a fixed address starts from a new random one, to edit or keep
        if(profile->mac_mode == SpoofMacFixed) spoof_random_mac(profile->mac);
        break;
    case EventSerialMode:
        break;
    case EventMacEntered:
        memcpy(profile->mac, app->bytes, 6);
        break;
    case EventSerialEntered:
        memcpy(profile->serial, app->bytes, 8);
        break;
    case EventNameEntered:
        strlcpy(profile->name, app->text, SPOOF_NAME_SIZE);
        break;
    case EventDeleteYes:
        // the profiles after it move up; the Flipper is its own again
        memmove(
            &spoof->profiles[spoof->active - 1],
            &spoof->profiles[spoof->active],
            (spoof->count - spoof->active) * sizeof(SpoofProfile));
        spoof->count--;
        spoof->active = 0;
        selected = 0;
        break;
    case EventDeleteNo:
        spoof_show(app, selected);
        return true;
    default: {
        if(event.event < EventRow || event.event - EventRow >= COUNT_OF(spoof->rows)) return true;
        switch(spoof->rows[event.event - EventRow]) {
        case RowMacValue:
            memcpy(app->bytes, profile->mac, 6);
            open_byte_input(app, "Bluetooth MAC", 6, EventMacEntered);
            return true;
        case RowSerialValue:
            memcpy(app->bytes, profile->serial, 8);
            open_byte_input(app, "Serial (unique id)", 8, EventSerialEntered);
            return true;
        case RowName:
            open_name_input(app);
            return true;
        case RowDelete:
            open_delete_dialog(app);
            return true;
        case RowNew: {
            // a new identity, in use: a fixed random address, the Flipper's serial and name
            SpoofProfile* added = &spoof->profiles[spoof->count];
            memset(added, 0, sizeof(*added));
            added->mac_mode = SpoofMacFixed;
            spoof_random_mac(added->mac);
            spoof->count++;
            spoof->active = spoof->count;
            selected = 0;
            break;
        }
        case RowRestart: {
            Power* power = furi_record_open(RECORD_POWER);
            power_reboot(power, PowerBootModeNormal);
            furi_record_close(RECORD_POWER);
            return true;
        }
        default:
            return true;
        }
    }
    }
    spoof_save(app->storage);
    spoof_show(app, selected);
    return true;
}

void hadal_settings_scene_spoof_on_exit(void* context) {
    HadalSettings* app = context;
    variable_item_list_reset(app->variable_list);
    free(spoof);
    spoof = NULL;
}
