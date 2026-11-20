#include "../hadal_settings_i.h"
#include "../../../services/namechanger/namechanger.h"

#include <flipper_format/flipper_format.h>
#include <furi_hal_random.h>
#include <furi_hal_version.h>
#include <power/power_service/power.h>

/* What the Flipper says it is: its Bluetooth address and its serial (unique id). The name is
 * Unleashed's (Settings > Desktop). Applied at start: a restart is asked for. */

enum {
    SpoofMac,
    SpoofMacRandom,
    SpoofMacReal,
    SpoofSerial,
    SpoofSerialReal,
    SpoofRestart,
    EventMacEntered = 100,
    EventSerialEntered,
};

typedef struct {
    bool has_mac;
    uint8_t mac[6];
    bool has_serial;
    uint8_t serial[8];
} Spoof;

// the byte input is open, over the list
static bool in_byte_input;

static void spoof_load(Storage* storage, Spoof* spoof) {
    memset(spoof, 0, sizeof(*spoof));
    FlipperFormat* file = flipper_format_file_alloc(storage);
    FuriString* header = furi_string_alloc();
    uint32_t version;
    if(flipper_format_file_open_existing(file, HADAL_SPOOF_PATH) &&
       flipper_format_read_header(file, header, &version) &&
       !furi_string_cmp_str(header, HADAL_SPOOF_HEADER) && version == HADAL_SPOOF_VERSION) {
        spoof->has_mac = flipper_format_read_hex(file, HADAL_SPOOF_MAC, spoof->mac, 6);
        flipper_format_rewind(file);
        spoof->has_serial = flipper_format_read_hex(file, HADAL_SPOOF_SERIAL, spoof->serial, 8);
    }
    furi_string_free(header);
    flipper_format_free(file);
}

static void spoof_save(Storage* storage, const Spoof* spoof) {
    storage_simply_mkdir(storage, EXT_PATH("apps_data"));
    storage_simply_mkdir(storage, EXT_PATH("apps_data/spoof"));
    FlipperFormat* file = flipper_format_file_alloc(storage);
    if(flipper_format_file_open_always(file, HADAL_SPOOF_PATH) &&
       flipper_format_write_header_cstr(file, HADAL_SPOOF_HEADER, HADAL_SPOOF_VERSION)) {
        flipper_format_write_comment_cstr(file, "Set in Settings > Hadal > Misc > Spoofing");
        if(spoof->has_mac) flipper_format_write_hex(file, HADAL_SPOOF_MAC, spoof->mac, 6);
        if(spoof->has_serial) flipper_format_write_hex(file, HADAL_SPOOF_SERIAL, spoof->serial, 8);
    }
    flipper_format_free(file);
}

static void byte_input_done(void* context) {
    HadalSettings* app = context;
    view_dispatcher_send_custom_event(
        app->view_dispatcher,
        scene_manager_get_scene_state(app->scene_manager, HadalSettingsSceneSpoof));
}

static void spoof_show(HadalSettings* app) {
    in_byte_input = false;
    Spoof spoof;
    spoof_load(app->storage, &spoof);
    FuriString* label = furi_string_alloc();
    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, "Spoofing (restart to apply)");

    if(spoof.has_mac) {
        furi_string_printf(
            label,
            "MAC %02X:%02X:%02X:%02X:%02X:%02X",
            spoof.mac[0],
            spoof.mac[1],
            spoof.mac[2],
            spoof.mac[3],
            spoof.mac[4],
            spoof.mac[5]);
    } else {
        furi_string_set_str(label, "MAC: the Flipper's own");
    }
    submenu_add_item(app->submenu, furi_string_get_cstr(label), SpoofMac, hadal_settings_submenu_callback, app);
    submenu_add_item(app->submenu, "Random MAC", SpoofMacRandom, hadal_settings_submenu_callback, app);
    if(spoof.has_mac) {
        submenu_add_item(app->submenu, "Own MAC back", SpoofMacReal, hadal_settings_submenu_callback, app);
    }

    if(spoof.has_serial) {
        furi_string_set_str(label, "Serial ");
        for(size_t i = 0; i < 8; i++) furi_string_cat_printf(label, "%02X", spoof.serial[i]);
    } else {
        furi_string_set_str(label, "Serial: the Flipper's own");
    }
    submenu_add_item(app->submenu, furi_string_get_cstr(label), SpoofSerial, hadal_settings_submenu_callback, app);
    if(spoof.has_serial) {
        submenu_add_item(
            app->submenu, "Own serial back", SpoofSerialReal, hadal_settings_submenu_callback, app);
    }
    submenu_add_item(app->submenu, "Restart to apply", SpoofRestart, hadal_settings_submenu_callback, app);
    furi_string_free(label);
    view_dispatcher_switch_to_view(app->view_dispatcher, HadalViewSubmenu);
}

void hadal_settings_scene_spoof_on_enter(void* context) {
    spoof_show(context);
}

static void open_byte_input(HadalSettings* app, const char* header, size_t size, uint32_t event) {
    scene_manager_set_scene_state(app->scene_manager, HadalSettingsSceneSpoof, event);
    byte_input_set_header_text(app->byte_input, header);
    byte_input_set_result_callback(app->byte_input, byte_input_done, NULL, app, app->bytes, size);
    in_byte_input = true;
    view_dispatcher_switch_to_view(app->view_dispatcher, HadalViewByteInput);
}

bool hadal_settings_scene_spoof_on_event(void* context, SceneManagerEvent event) {
    HadalSettings* app = context;
    if(event.type == SceneManagerEventTypeBack && in_byte_input) {
        spoof_show(app);
        return true;
    }
    if(event.type != SceneManagerEventTypeCustom) return false;

    Spoof spoof;
    spoof_load(app->storage, &spoof);
    switch(event.event) {
    case SpoofMac:
        if(spoof.has_mac) {
            memcpy(app->bytes, spoof.mac, 6);
        } else {
            memcpy(app->bytes, furi_hal_version_get_ble_mac(), 6);
            // shown most significant first
            for(size_t i = 0; i < 3; i++) {
                const uint8_t t = app->bytes[i];
                app->bytes[i] = app->bytes[5 - i];
                app->bytes[5 - i] = t;
            }
        }
        open_byte_input(app, "Bluetooth MAC", 6, EventMacEntered);
        return true;
    case SpoofSerial:
        memcpy(app->bytes, spoof.has_serial ? spoof.serial : furi_hal_version_uid(), 8);
        open_byte_input(app, "Serial (unique id)", 8, EventSerialEntered);
        return true;
    case EventMacEntered:
        spoof.has_mac = true;
        memcpy(spoof.mac, app->bytes, 6);
        break;
    case EventSerialEntered:
        spoof.has_serial = true;
        memcpy(spoof.serial, app->bytes, 8);
        break;
    case SpoofMacRandom:
        spoof.has_mac = true;
        furi_hal_random_fill_buf(spoof.mac, 6);
        // one device, not a group, and marked as made up: what a random address should say
        spoof.mac[0] = (spoof.mac[0] & 0xFC) | 0x02;
        break;
    case SpoofMacReal:
        spoof.has_mac = false;
        break;
    case SpoofSerialReal:
        spoof.has_serial = false;
        break;
    case SpoofRestart: {
        Power* power = furi_record_open(RECORD_POWER);
        power_reboot(power, PowerBootModeNormal);
        furi_record_close(RECORD_POWER);
        return true;
    }
    default:
        return true;
    }
    spoof_save(app->storage, &spoof);
    spoof_show(app);
    return true;
}

void hadal_settings_scene_spoof_on_exit(void* context) {
    HadalSettings* app = context;
    submenu_reset(app->submenu);
}
