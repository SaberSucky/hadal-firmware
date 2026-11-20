#include "update_task_preflight.h"
#include "update_task_i.h"
#include "update_log.h"

#include <furi.h>
#include <furi_hal.h>
#include <toolbox/crc32_calc.h>
#include <update_util/dfu_file.h>

#define TAG "UpdPreflight"

// Below this, unplugged, a long flash write may not finish
#define PREFLIGHT_MIN_BATTERY_PCT (15U)
// The STM32WB55RG in the Flipper has 1 MB of flash
#define PREFLIGHT_FLASH_SIZE      (1024U * 1024U)
// The readout protection byte of the option bytes; 0xAA is "no protection"
#define PREFLIGHT_RDP_NONE        (0xAAU)

/* Written into DFU file by build pipeline (the same as the flasher's) */
static const DfuValidationParams preflight_dfu_params = {
    .device = 0xFFFF,
    .product = 0xDF11,
    .vendor = 0x0483,
};

static bool preflight_battery(UpdateTask* update_task) {
    const uint8_t pct = furi_hal_power_get_pct();
    if(pct >= PREFLIGHT_MIN_BATTERY_PCT || furi_hal_power_is_charging() ||
       furi_hal_power_is_charging_done()) {
        return true;
    }
    update_task_fail_with_reason(update_task, "Battery too low:\ncharge it first");
    return false;
}

static bool preflight_radio(UpdateTask* update_task, uint32_t* radio_address) {
    UpdateManifest* manifest = update_task->manifest;
    *radio_address = manifest->radio_address;
    if(!update_task_open_file(update_task, manifest->radio_image) ||
       crc32_calc_file(update_task->file, NULL, NULL) != manifest->radio_crc) {
        update_task_fail_with_reason(update_task, "Radio image is\ndamaged");
        return false;
    }
    const size_t page = furi_hal_flash_get_page_size();
    const size_t flash_end = furi_hal_flash_get_base() + PREFLIGHT_FLASH_SIZE;
    const size_t size = storage_file_size(update_task->file);
    if(manifest->radio_address % page != 0 ||
       manifest->radio_address < furi_hal_flash_get_base() ||
       manifest->radio_address + size > flash_end) {
        update_task_fail_with_reason(update_task, "Bad radio address\nin the package");
        return false;
    }
    return true;
}

static bool
    preflight_firmware(UpdateTask* update_task, bool radio_changes, uint32_t radio_address) {
    UpdateManifest* manifest = update_task->manifest;
    uint32_t start = 0, end = 0;
    bool readable = update_task_open_file(update_task, manifest->firmware_dfu_image) &&
                    dfu_file_validate_crc(update_task->file, NULL, NULL);
    const uint8_t targets =
        readable ? dfu_file_validate_headers(update_task->file, &preflight_dfu_params) : 0;
    if(!targets || !dfu_file_get_address_range(update_task->file, targets, &start, &end)) {
        update_task_fail_with_reason(update_task, "Firmware image is\ndamaged");
        return false;
    }

    // With the radio staying, an image beyond the writable flash would be written only in part
    // (the writer skips what does not fit). With the radio changing, it must end below the new
    // stack, or the stack would overwrite its end.
    const uint32_t limit = radio_changes ? radio_address :
                                           (uint32_t)furi_hal_flash_get_free_end_address();
    update_log(
        "preflight: firmware 0x%08lX..0x%08lX, must end by 0x%08lX",
        (unsigned long)start,
        (unsigned long)end,
        (unsigned long)limit);
    if(start < furi_hal_flash_get_base() || end > limit) {
        update_task_fail_with_reason(update_task, "Firmware won't fit\nwith this radio");
        return false;
    }
    return true;
}

static bool preflight_option_bytes(UpdateTask* update_task) {
    const UpdateManifest* manifest = update_task->manifest;
    // Writing the readout protection byte to anything but "none" locks the chip for good
    const uint8_t rdp_mask = manifest->ob_write_mask.obs[0].values.base & 0xFF;
    const uint8_t rdp_value = manifest->ob_reference.obs[0].values.base & 0xFF;
    if(rdp_mask && rdp_value != PREFLIGHT_RDP_NONE) {
        update_task_fail_with_reason(update_task, "Package would lock\nthe chip");
        return false;
    }
    return true;
}

bool update_task_preflight(UpdateTask* update_task) {
    const UpdateTaskStageGroup groups = update_task->state.groups;
    const bool radio_changes = groups & UpdateTaskStageGroupRadio;
    uint32_t radio_address = 0;

    if(!preflight_battery(update_task)) return false;
    if(radio_changes && !preflight_radio(update_task, &radio_address)) return false;
    if((groups & UpdateTaskStageGroupFirmware) &&
       !preflight_firmware(update_task, radio_changes, radio_address)) {
        return false;
    }
    if((groups & UpdateTaskStageGroupOptionBytes) && !preflight_option_bytes(update_task)) {
        return false;
    }
    FURI_LOG_I(TAG, "Package checked");
    return true;
}
