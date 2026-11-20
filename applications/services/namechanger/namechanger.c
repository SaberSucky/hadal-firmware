#include "namechanger.h"
#include <furi_hal.h>
#include <furi_hal_version.h>
#include <cli/cli_vcp.h>
#include <bt/bt_service/bt.h>
#include <storage/storage.h>
#include <flipper_format/flipper_format.h>

#define TAG "NameChanger"

static bool namechanger_init() {
    Storage* storage = furi_record_open(RECORD_STORAGE);

    // Kostil + velosiped = top ficha
    uint8_t timeout = 0;
    while(timeout < 11) {
        if(storage_sd_status(storage) == FSE_OK) break;
        furi_delay_ms(250);
        timeout++;
        /*if(timeout == 10) {
            // Failed to init namechanger, SD card not ready
            furi_record_close(RECORD_STORAGE);
            return false;
        }*/
    }

    FuriString* str = furi_string_alloc();
    FlipperFormat* file = flipper_format_file_alloc(storage);

    bool res = false;

    do {
        uint32_t version;
        if(!flipper_format_file_open_existing(file, NAMECHANGER_PATH)) break;
        if(!flipper_format_read_header(file, str, &version)) break;
        if(furi_string_cmp_str(str, NAMECHANGER_HEADER)) break;
        if(version != NAMECHANGER_VERSION) break;

        if(!flipper_format_read_string(file, "Name", str)) break;
        // Check for size
        size_t temp_string_size = furi_string_size(str);
        if(temp_string_size > (size_t)8) break;
        if(temp_string_size < (size_t)2) break;

        // Check for forbidden characters
        const char* name_ptr = furi_string_get_cstr(str);
        bool chars_check_failed = false;

        for(; *name_ptr; ++name_ptr) {
            const char c = *name_ptr;
            if((c < '0' || c > '9') && (c < 'A' || c > 'Z') && (c < 'a' || c > 'z')) {
                chars_check_failed = true;
                break;
            }
        }

        if(chars_check_failed) break;

        // If all checks was good we can set the name
        version_set_custom_name(NULL, strdup(furi_string_get_cstr(str)));
        furi_hal_version_set_name(version_get_custom_name(NULL));

        res = true;
    } while(false);

    flipper_format_free(file);
    furi_record_close(RECORD_STORAGE);
    furi_string_free(str);

    return res;
}

// The spoofed Bluetooth address and serial, if the user set them; true if the address changed
static bool namechanger_spoof_init(void) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    FlipperFormat* file = flipper_format_file_alloc(storage);
    FuriString* header = furi_string_alloc();
    bool mac_set = false;
    do {
        uint32_t version;
        if(!flipper_format_file_open_existing(file, HADAL_SPOOF_PATH)) break;
        if(!flipper_format_read_header(file, header, &version)) break;
        if(furi_string_cmp_str(header, HADAL_SPOOF_HEADER) || version != HADAL_SPOOF_VERSION) break;

        uint8_t mac[6];
        if(flipper_format_read_hex(file, HADAL_SPOOF_MAC, mac, sizeof(mac))) {
            // written most significant first; the radio wants it the other way round
            uint8_t reversed[6];
            for(size_t i = 0; i < sizeof(mac); i++) {
                reversed[i] = mac[sizeof(mac) - 1 - i];
            }
            furi_hal_version_set_ble_mac(reversed);
            mac_set = true;
        }
        flipper_format_rewind(file);
        uint8_t serial[8];
        if(flipper_format_read_hex(file, HADAL_SPOOF_SERIAL, serial, sizeof(serial))) {
            furi_hal_version_set_uid(serial);
        }
    } while(false);
    furi_string_free(header);
    flipper_format_free(file);
    furi_record_close(RECORD_STORAGE);
    return mac_set;
}

static void namechanger_apply(void) {

    // The name and the spoofing come from the SD card: storage and the USB port's service are
    // all that is needed to put them in place. Bluetooth is waited for only to restart with them,
    // below: the desktop, which waits for this to bring the USB port up, no longer waits for
    // Bluetooth's own restart as well.
    static uint8_t done_marker;
    uint8_t timeout = 0;
    while(!furi_record_exists(RECORD_CLI_VCP) || !furi_record_exists(RECORD_STORAGE)) {
        timeout++;
        if(timeout > 250) {
            furi_record_create(RECORD_NAMECHANGER_DONE, &done_marker);
            return;
        }
        furi_delay_ms(5);
    }

    // Hehe bad code now here, bad bad bad, very bad, bad example, dont take it, make it better

    const bool renamed = namechanger_init();
    // after the name: a spoofed address goes over the one the name gives
    const bool spoofed = namechanger_spoof_init();
    // the name is final: the USB port may come up
    furi_record_create(RECORD_NAMECHANGER_DONE, &done_marker);
    // The desktop normally brings the USB port up after this, with the new name already in place.
    // Only if it gave up waiting is the port up with the old name: reconnect it once.
    if(renamed) {
        CliVcp* cli = furi_record_open(RECORD_CLI_VCP);
        if(cli_vcp_is_enabled(cli)) {
            cli_vcp_disable(cli);
            furi_delay_ms(2);
            cli_vcp_enable(cli);
        }
        furi_record_close(RECORD_CLI_VCP);
    }
    if(renamed || spoofed) {
        for(timeout = 0; !furi_record_exists(RECORD_BT) && timeout < 250; timeout++) {
            furi_delay_ms(5);
        }
        if(!furi_record_exists(RECORD_BT)) return;
        furi_delay_ms(3);
        Bt* bt = furi_record_open(RECORD_BT);
        if(!bt_profile_restore_default(bt)) {
            //FURI_LOG_D(TAG, "Failed to touch bluetooth to name change");
        }
        furi_record_close(RECORD_BT);
        bt = NULL;
        furi_delay_ms(3);
    }
}

int32_t namechanger_on_system_start(void* p) {
    UNUSED(p);
    if(furi_hal_rtc_get_boot_mode() != FuriHalRtcBootModeNormal) {
        return 0;
    }

    // creates RECORD_NAMECHANGER_DONE as soon as the name is in place
    namechanger_apply();
    return 0;
}
