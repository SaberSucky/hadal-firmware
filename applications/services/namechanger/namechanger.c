#include "namechanger.h"
#include <furi_hal.h>
#include <furi_hal_version.h>
#include <cli/cli_vcp.h>
#include <bt/bt_service/bt.h>
#include <storage/storage.h>
#include <flipper_format/flipper_format.h>

#define TAG "NameChanger"

/* 2 to 8 letters and digits: what a Flipper name may be */
static bool namechanger_name_valid(const char* name) {
    const size_t size = strlen(name);
    if(size < 2 || size > 8) return false;
    for(; *name; ++name) {
        const char c = *name;
        if((c < '0' || c > '9') && (c < 'A' || c > 'Z') && (c < 'a' || c > 'z')) return false;
    }
    return true;
}

static void namechanger_set_name(const char* name) {
    version_set_custom_name(NULL, strdup(name));
    furi_hal_version_set_name(version_get_custom_name(NULL));
}

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
        if(!namechanger_name_valid(furi_string_get_cstr(str))) break;
        namechanger_set_name(furi_string_get_cstr(str));

        res = true;
    } while(false);

    flipper_format_free(file);
    furi_record_close(RECORD_STORAGE);
    furi_string_free(str);

    return res;
}

// The Generation of the spoofing applied: shared as RECORD_NAMECHANGER_DONE's byte
static uint8_t namechanger_generation;

// The identity set in Hadal > Spoofing: its address (fixed, or new at every start), serial and
// name. True if the address or the name changed
static bool namechanger_spoof_init(void) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    FlipperFormat* file = flipper_format_file_alloc(storage);
    FuriString* header = furi_string_alloc();
    bool mac_set = false;
    do {
        uint32_t version;
        if(!flipper_format_file_open_existing(file, HADAL_SPOOF_PATH)) break;
        if(!flipper_format_read_header(file, header, &version)) break;
        if(furi_string_cmp_str(header, HADAL_SPOOF_HEADER) || version < 1 ||
           version > HADAL_SPOOF_VERSION)
            break;

        uint32_t generation = 0;
        if(flipper_format_read_uint32(file, HADAL_SPOOF_GENERATION, &generation, 1)) {
            namechanger_generation = (uint8_t)generation;
        }
        flipper_format_rewind(file);
        bool random = false;
        flipper_format_read_bool(file, HADAL_SPOOF_RANDOM, &random, 1);
        flipper_format_rewind(file);
        uint8_t mac[6];
        if(random) {
            // one device, not a group, and marked as made up: a random address says so
            furi_hal_random_fill_buf(mac, sizeof(mac));
            mac[0] = (mac[0] & 0xFC) | 0x02;
        }
        if(random || flipper_format_read_hex(file, HADAL_SPOOF_MAC, mac, sizeof(mac))) {
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
        flipper_format_rewind(file);
        if(flipper_format_read_string(file, HADAL_SPOOF_NAME, header) &&
           namechanger_name_valid(furi_string_get_cstr(header))) {
            namechanger_set_name(furi_string_get_cstr(header));
            mac_set = true; // Bluetooth starts again with the name
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
    // after the name: a spoofed address goes over the one the name gives, a profile's name over
    // Desktop's
    const bool spoofed = namechanger_spoof_init();
    // the name is final: the USB port may come up
    done_marker = namechanger_generation;
    furi_record_create(RECORD_NAMECHANGER_DONE, &done_marker);
    // The desktop normally brings the USB port up after this, with the new name already in place.
    // Only if it gave up waiting is the port up with the old name: reconnect it once.
    if(renamed || spoofed) {
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
