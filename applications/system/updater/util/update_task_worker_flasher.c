#include "update_task.h"
#include "update_task_i.h"

#include <furi.h>
#include <furi_hal.h>
#include <storage/storage.h>
#include <toolbox/path.h>
#include <update_util/dfu_file.h>
#include <update_util/update_operation.h>
#include <toolbox/tar/tar_archive.h>
#include <toolbox/crc32_calc.h>

#include "update_log.h"
#include "update_trace.h"
#include "update_task_preflight.h"
#include <update_util/boot_guard.h>

#define TAG "UpdWorkerRam"

#define STM_DFU_VENDOR_ID            0x0483
#define STM_DFU_PRODUCT_ID           0xDF11
/* Written into DFU file by build pipeline */
#define FLIPPER_ZERO_DFU_DEVICE_CODE 0xFFFF
/* Time, in ms, to wait for system restart by C2 before crashing */
#define C2_MODE_SWITCH_TIMEOUT       10000

static const DfuValidationParams flipper_dfu_params = {
    .device = FLIPPER_ZERO_DFU_DEVICE_CODE,
    .product = STM_DFU_PRODUCT_ID,
    .vendor = STM_DFU_VENDOR_ID,
};

static void update_task_file_progress(const uint8_t progress, void* context) {
    UpdateTask* update_task = context;
    update_task_set_progress(update_task, UpdateTaskStageProgress, progress);
}

static bool page_task_compare_flash(
    const uint8_t i_page,
    const uint8_t* update_block,
    uint16_t update_block_len) {
    const size_t page_addr = furi_hal_flash_get_base() + furi_hal_flash_get_page_size() * i_page;
    return memcmp(update_block, (void*)page_addr, update_block_len) == 0;
}

/* Verifies a flash operation address for fitting into writable memory
 */
static bool check_address_boundaries(const size_t address) {
    const size_t min_allowed_address = furi_hal_flash_get_base();
    const size_t max_allowed_address = (size_t)furi_hal_flash_get_free_end_address();
    return (address >= min_allowed_address) && (address < max_allowed_address);
}

// Whether the page already holds exactly what programming it would leave: the block, then erased
static bool update_task_page_is_written(
    const uint8_t i_page,
    const uint8_t* update_block,
    uint16_t update_block_len) {
    const size_t page_size = furi_hal_flash_get_page_size();
    const uint8_t* page = (const uint8_t*)(furi_hal_flash_get_base() + page_size * i_page);
    if(memcmp(update_block, page, update_block_len) != 0) return false;
    for(size_t i = update_block_len; i < page_size; i++) {
        if(page[i] != 0xFF) return false;
    }
    return true;
}

static bool update_task_flash_program_page(
    const uint8_t i_page,
    const uint8_t* update_block,
    uint16_t update_block_len) {
    // When the firmware goes in before the radio, the radio's restarts run this again: pages
    // that are already right are not erased and written a second time
    if(update_task_page_is_written(i_page, update_block, update_block_len)) return true;
    furi_hal_flash_program_page(i_page, update_block, update_block_len);
    return true;
}

static bool update_task_write_dfu(UpdateTask* update_task) {
    DfuUpdateTask page_task = {
        .address_cb = &check_address_boundaries,
        .progress_cb = &update_task_file_progress,
        .task_cb = &update_task_flash_program_page,
        .context = update_task,
    };

    bool success = false;
    do {
        update_task_set_progress(update_task, UpdateTaskStageValidateDFUImage, 0);
        CHECK_RESULT(
            update_task_open_file(update_task, update_task->manifest->firmware_dfu_image));
        CHECK_RESULT(
            dfu_file_validate_crc(update_task->file, &update_task_file_progress, update_task));

        const uint8_t valid_targets =
            dfu_file_validate_headers(update_task->file, &flipper_dfu_params);
        if(valid_targets == 0) {
            break;
        }

        update_task_set_progress(update_task, UpdateTaskStageFlashWrite, 0);
        update_trace_step(UpdateTraceStepFirmwareWrite, 0);
        CHECK_RESULT(dfu_file_process_targets(&page_task, update_task->file, valid_targets));

        page_task.task_cb = &page_task_compare_flash;

        update_task_set_progress(update_task, UpdateTaskStageFlashValidate, 0);
        CHECK_RESULT(dfu_file_process_targets(&page_task, update_task->file, valid_targets));
        update_trace_step(UpdateTraceStepFirmwareDone, 0);
        success = true;
    } while(false);

    return success;
}

/* Installing a radio stack lower in flash (BLE Light -> Full moves it from 0x080D7000 down to
 * 0x080CE000) writes it over whatever is there, which can be the end of the firmware that is
 * still installed: one built for the Light stack may be up to 860 KB, and its last part holds
 * the startup data. The radio steps restart the Flipper several times, and every restart runs
 * the firmware in flash to load this updater again; with its end overwritten it cannot start,
 * and the screen stays dark until a DFU repair.
 *
 * So, before the radio is changed, the new firmware is written first if it fits below both the
 * new stack and what is writable now; every restart then runs the new firmware, which the new
 * stack does not touch. If it does not fit (the stack moves up, to make room for a larger
 * firmware), the radio goes first as before, which is then the safe order. */
static bool update_task_firmware_fits_first(UpdateTask* update_task) {
    if(!(update_task->state.groups & UpdateTaskStageGroupFirmware)) return false;

    uint32_t start = 0, end = 0;
    bool fits = false;
    do {
        if(!update_task_open_file(update_task, update_task->manifest->firmware_dfu_image)) break;
        const uint8_t valid_targets =
            dfu_file_validate_headers(update_task->file, &flipper_dfu_params);
        if(valid_targets == 0) break;
        if(!dfu_file_get_address_range(update_task->file, valid_targets, &start, &end)) break;
        fits = (start >= furi_hal_flash_get_base()) &&
               (end <= update_task->manifest->radio_address) &&
               (end <= (size_t)furi_hal_flash_get_free_end_address());
    } while(false);

    FURI_LOG_I(
        TAG,
        "Firmware 0x%08lX..0x%08lX, radio at 0x%08lX, writable to 0x%08lX: %s",
        start,
        end,
        update_task->manifest->radio_address,
        (uint32_t)furi_hal_flash_get_free_end_address(),
        fits ? "firmware first" : "radio first");
    return fits;
}

// Before a radio step that restarts the Flipper or rewrites flash: the new firmware goes in first
// when it can (see above). Runs again after each restart, writing nothing the second time.
static bool update_task_prepare_radio_change(UpdateTask* update_task) {
    if(update_task->firmware_written) return true;
    if(!update_task_firmware_fits_first(update_task)) return true;

    update_trace_step(UpdateTraceStepFirmwareFirst, 0);
    if(!update_task_write_dfu(update_task)) return false;
    update_task->firmware_written = true;
    update_trace_step(UpdateTraceStepFirmwareFirstDone, 0);
    return true;
}

static bool update_task_write_stack_data(UpdateTask* update_task) {
    furi_check(storage_file_is_open(update_task->file));
    const size_t FLASH_PAGE_SIZE = furi_hal_flash_get_page_size();

    uint32_t stack_size = storage_file_size(update_task->file);
    storage_file_seek(update_task->file, 0, true);

    if(!check_address_boundaries(update_task->manifest->radio_address) ||
       !check_address_boundaries(update_task->manifest->radio_address + stack_size)) {
        update_log(
            "stack: 0x%08lX..0x%08lX is outside the writable flash (ends at 0x%08lX)",
            (unsigned long)update_task->manifest->radio_address,
            (unsigned long)(update_task->manifest->radio_address + stack_size),
            (unsigned long)furi_hal_flash_get_free_end_address());
        return false;
    }

    update_log(
        "stack: writing %lu bytes at 0x%08lX, writable flash ends at 0x%08lX",
        (unsigned long)stack_size,
        (unsigned long)update_task->manifest->radio_address,
        (unsigned long)furi_hal_flash_get_free_end_address());
    update_task_set_progress(update_task, UpdateTaskStageRadioWrite, 0);
    uint8_t* fw_block = malloc(FLASH_PAGE_SIZE);
    size_t bytes_read = 0;
    uint32_t element_offs = 0;

    while(element_offs < stack_size) {
        uint32_t n_bytes_to_read = FLASH_PAGE_SIZE;
        if((element_offs + n_bytes_to_read) > stack_size) {
            n_bytes_to_read = stack_size - element_offs;
        }

        bytes_read = storage_file_read(update_task->file, fw_block, n_bytes_to_read);
        CHECK_RESULT(bytes_read != 0);

        int16_t i_page =
            furi_hal_flash_get_page_number(update_task->manifest->radio_address + element_offs);
        CHECK_RESULT(i_page >= 0);

        if((element_offs / FLASH_PAGE_SIZE) % 8 == 0) {
            update_trace_step(
                UpdateTraceStepStackWrite, (uint8_t)(element_offs * 10 / stack_size));
            update_log(
                "stack: writing page %d (offset %lu of %lu)",
                i_page,
                (unsigned long)element_offs,
                (unsigned long)stack_size);
        }
        furi_hal_flash_program_page(i_page, fw_block, bytes_read);

        element_offs += bytes_read;
        update_task_set_progress(
            update_task, UpdateTaskStageProgress, element_offs * 100 / stack_size);
    }

    free(fw_block);
    return element_offs == stack_size;
}

static void update_task_wait_for_restart(UpdateTask* update_task) {
    update_task_set_progress(update_task, UpdateTaskStageRadioBusy, 70);
    update_trace_step(UpdateTraceStepWaitRestart, 0);
    update_log(
        "waiting up to %d ms for the radio chip to restart the system", C2_MODE_SWITCH_TIMEOUT);
    furi_delay_ms(C2_MODE_SWITCH_TIMEOUT);
    update_log("C2 timeout: the system was not restarted, crashing on purpose");
    furi_crash("C2 timeout");
}

static bool update_task_write_stack(UpdateTask* update_task) {
    UpdateManifest* manifest = update_task->manifest;
    do {
        UPDATE_LOG_W(TAG, "Writing stack");
        update_task_set_progress(update_task, UpdateTaskStageRadioImageValidate, 0);
        CHECK_RESULT(update_task_open_file(update_task, manifest->radio_image));
        CHECK_RESULT(
            crc32_calc_file(update_task->file, &update_task_file_progress, update_task) ==
            manifest->radio_crc);

        CHECK_RESULT(update_task_write_stack_data(update_task));
        update_task_set_progress(update_task, UpdateTaskStageRadioInstall, 10);
        update_log("fus: install stack at 0x%08lX", (unsigned long)manifest->radio_address);
        BleGlueCommandResult install_result =
            ble_glue_fus_stack_install(manifest->radio_address, 0);
        update_trace_step(UpdateTraceStepStackInstall, (uint8_t)install_result);
        update_log("fus: install returned %d", (int)install_result);
        CHECK_RESULT(install_result != BleGlueCommandResultError);
        update_task_set_progress(update_task, UpdateTaskStageProgress, 80);
        update_log("fus: waiting for the install to finish");
        BleGlueCommandResult install_wait = ble_glue_fus_wait_operation();
        update_trace_step(UpdateTraceStepStackInstalled, (uint8_t)install_wait);
        update_log("fus: install finished with %d", (int)install_wait);
        CHECK_RESULT(install_wait == BleGlueCommandResultOK);
        update_task_set_progress(update_task, UpdateTaskStageProgress, 100);
        /* ...system will restart here. */
        update_task_wait_for_restart(update_task);
    } while(false);
    return false; /* will return only in the case of failure */
}

static bool update_task_remove_stack(UpdateTask* update_task) {
    do {
        UPDATE_LOG_W(TAG, "Removing stack");
        update_task_set_progress(update_task, UpdateTaskStageRadioErase, 30);
        update_log("fus: delete stack");
        BleGlueCommandResult delete_result = ble_glue_fus_stack_delete();
        update_trace_step(UpdateTraceStepStackDelete, (uint8_t)delete_result);
        update_log("fus: delete returned %d", (int)delete_result);
        CHECK_RESULT(delete_result != BleGlueCommandResultError);
        update_task_set_progress(update_task, UpdateTaskStageProgress, 80);
        update_log("fus: waiting for the delete to finish");
        BleGlueCommandResult delete_wait = ble_glue_fus_wait_operation();
        update_log("fus: delete finished with %d", (int)delete_wait);
        CHECK_RESULT(delete_wait == BleGlueCommandResultOK);
        update_task_set_progress(update_task, UpdateTaskStageProgress, 100);
        /* ...system will restart here. */
        update_task_wait_for_restart(update_task);
    } while(false);
    return false; /* will return only in the case of failure */
}

static bool update_task_manage_radiostack(UpdateTask* update_task) {
    update_task_set_progress(update_task, UpdateTaskStageRadioBusy, 10);
    bool success = false;
    do {
        update_trace_step(UpdateTraceStepRadioWait, 0);
        update_log("radio: waiting for the radio chip to start");
        bool c2_started = ble_glue_wait_for_c2_start(FURI_HAL_BT_C2_START_TIMEOUT);
        update_log("radio: chip started: %d", (int)c2_started);
        CHECK_RESULT(c2_started);

        const BleGlueC2Info* c2_state = ble_glue_get_c2_info();
        update_trace_step(
            UpdateTraceStepRadioState,
            (uint8_t)((((uint8_t)c2_state->mode & 0x0F) << 4) | (c2_state->StackType & 0x0F)));
        update_log(
            "radio: mode %d, version %d.%d.%d.%d type %d release %d, writable flash ends at 0x%08lX",
            (int)c2_state->mode,
            c2_state->VersionMajor,
            c2_state->VersionMinor,
            c2_state->VersionSub,
            c2_state->VersionBranch,
            c2_state->StackType,
            c2_state->VersionReleaseType,
            (unsigned long)furi_hal_flash_get_free_end_address());
        {
            const FuriHalFlashRawOptionByteData* ob = furi_hal_flash_ob_get_raw_ptr();
            char dump[16 * 9 + 1];
            for(size_t i = 0; i < 16; i++) {
                snprintf(dump + i * 9, 10, "%08lX ", (unsigned long)ob->obs[i].values.base);
            }
            update_log("option bytes: %s", dump);
        }

        const UpdateManifestRadioVersion* radio_ver = &update_task->manifest->radio_version;
        bool stack_version_match = (c2_state->VersionMajor == radio_ver->version.major) &&
                                   (c2_state->VersionMinor == radio_ver->version.minor) &&
                                   (c2_state->VersionSub == radio_ver->version.sub) &&
                                   (c2_state->VersionBranch == radio_ver->version.branch) &&
                                   (c2_state->VersionReleaseType == radio_ver->version.release);
        bool stack_missing = (c2_state->VersionMajor == 0) && (c2_state->VersionMinor == 0);

        if(c2_state->mode == BleGlueC2ModeStack) {
            /* Stack type is not available when we have FUS running. */
            bool total_stack_match = stack_version_match &&
                                     (c2_state->StackType == radio_ver->version.type);
            if(total_stack_match) {
                /* Nothing to do. */
                UPDATE_LOG_W(TAG, "Stack version is up2date");
                update_trace_step(UpdateTraceStepRadioCurrent, 0);
                furi_hal_rtc_reset_flag(FuriHalRtcFlagC2Update);
                success = true;
                break;
            } else {
                /* Version or type mismatch. Let's boot to FUS and start updating. */
                CHECK_RESULT(update_task_prepare_radio_change(update_task));
                UPDATE_LOG_W(TAG, "Restarting to FUS");
                update_trace_step(UpdateTraceStepRestartToFus, 0);
                furi_hal_rtc_set_flag(FuriHalRtcFlagC2Update);
                update_task_set_progress(update_task, UpdateTaskStageProgress, 20);

                CHECK_RESULT(furi_hal_bt_ensure_c2_mode(BleGlueC2ModeFUS));
                /* ...system will restart here. */
                update_task_wait_for_restart(update_task);
            }
        } else if(c2_state->mode == BleGlueC2ModeFUS) {
            /* OK, we're in FUS mode. */
            UPDATE_LOG_W(TAG, "Waiting for FUS to settle");
            update_task_set_progress(update_task, UpdateTaskStageProgress, 30);
            BleGlueCommandResult settle_result = ble_glue_fus_wait_operation();
            update_trace_step(UpdateTraceStepFusSettled, (uint8_t)settle_result);
            update_log("fus: settled with %d", (int)settle_result);
            CHECK_RESULT(settle_result == BleGlueCommandResultOK);
            /* FUS is idle: before it changes the stack or restarts, the firmware goes in */
            CHECK_RESULT(update_task_prepare_radio_change(update_task));
            if(stack_version_match) {
                /* We can't check StackType with FUS, but partial version matches */
                if(furi_hal_rtc_is_flag_set(FuriHalRtcFlagC2Update)) {
                    /* This flag was set when full version was checked.
                     * And something in versions of the stack didn't match.
                     * So, clear the flag and drop the stack. */
                    furi_hal_rtc_reset_flag(FuriHalRtcFlagC2Update);
                    UPDATE_LOG_W(TAG, "Forcing stack removal (match)");
                    CHECK_RESULT(update_task_remove_stack(update_task));
                } else {
                    /* We might just had the stack installed.
                     * Let's start it up to check its version */
                    UPDATE_LOG_W(TAG, "Starting stack to check full version");
                    update_trace_step(UpdateTraceStepStartStack, 0);
                    update_task_set_progress(update_task, UpdateTaskStageProgress, 50);
                    CHECK_RESULT(furi_hal_bt_ensure_c2_mode(BleGlueC2ModeStack));
                    /* ...system will restart here. */
                    update_task_wait_for_restart(update_task);
                }
            } else {
                if(stack_missing) {
                    /* Install stack. */
                    CHECK_RESULT(update_task_write_stack(update_task));
                } else {
                    CHECK_RESULT(update_task_remove_stack(update_task));
                }
            }
        }
    } while(false);

    return success;
}

bool update_task_validate_optionbytes(UpdateTask* update_task) {
    update_task_set_progress(update_task, UpdateTaskStageOBValidation, 0);
    update_trace_step(UpdateTraceStepOptionBytes, 0);

    bool match = true;
    bool ob_dirty = false;
    const UpdateManifest* manifest = update_task->manifest;
    const FuriHalFlashRawOptionByteData* device_data = furi_hal_flash_ob_get_raw_ptr();
    for(size_t idx = 0; idx < FURI_HAL_FLASH_OB_TOTAL_VALUES; ++idx) {
        update_task_set_progress(
            update_task, UpdateTaskStageProgress, idx * 100 / FURI_HAL_FLASH_OB_TOTAL_VALUES);
        const uint32_t ref_value = manifest->ob_reference.obs[idx].values.base;
        const uint32_t device_ob_value = device_data->obs[idx].values.base;
        const uint32_t device_ob_value_masked = device_ob_value &
                                                manifest->ob_compare_mask.obs[idx].values.base;
        if(ref_value != device_ob_value_masked) {
            match = false;
            UPDATE_LOG_E(
                TAG,
                "OB MISMATCH: #%d: real %08lX != %08lX (exp.), full %08lX",
                idx,
                device_ob_value_masked,
                ref_value,
                device_ob_value);

            /* any bits we are allowed to write?.. */
            bool can_patch = ((device_ob_value_masked ^ ref_value) &
                              manifest->ob_write_mask.obs[idx].values.base) != 0;

            if(can_patch) {
                const uint32_t patched_value =
                    /* take all non-writable bits from real value */
                    (device_ob_value & ~(manifest->ob_write_mask.obs[idx].values.base)) |
                    /* take all writable bits from reference value */
                    (manifest->ob_reference.obs[idx].values.base &
                     manifest->ob_write_mask.obs[idx].values.base);

                UPDATE_LOG_W(TAG, "Fixing up OB byte #%d to %08lX", idx, patched_value);
                ob_dirty = true;

                bool is_fixed = furi_hal_flash_ob_set_word(idx, patched_value) &&
                                ((device_data->obs[idx].values.base &
                                  manifest->ob_compare_mask.obs[idx].values.base) == ref_value);

                if(!is_fixed) {
                    /* Things are so bad that fixing what we are allowed to still doesn't match
                     * reference value */
                    UPDATE_LOG_W(
                        TAG,
                        "OB #%d is FUBAR (fixed&masked %08lX, not %08lX)",
                        idx,
                        patched_value,
                        ref_value);
                }
            }
        } else {
            FURI_LOG_D(
                TAG,
                "OB MATCH: #%d: real %08lX == %08lX (exp.)",
                idx,
                device_ob_value_masked,
                ref_value);
        }
    }
    if(!match) {
        update_task_set_progress(update_task, UpdateTaskStageOBError, 0);
    }

    if(ob_dirty) {
        UPDATE_LOG_W(TAG, "OBs were changed, applying");
        furi_hal_flash_ob_apply();
    }
    return match;
}

int32_t update_task_worker_flash_writer(void* context) {
    furi_assert(context);
    UpdateTask* update_task = context;
    bool success = false;

    update_trace_begin();
    do {
        CHECK_RESULT(update_task_parse_manifest(update_task));
        /* Nothing changes until the package is known to be safe to install */
        CHECK_RESULT(update_task_preflight(update_task));

        /* Changing the radio may put the new firmware in first (update_task_prepare_radio_change) */
        if(update_task->state.groups & UpdateTaskStageGroupRadio) {
            CHECK_RESULT(update_task_manage_radiostack(update_task));
        }

        if(update_task->state.groups & UpdateTaskStageGroupOptionBytes) {
            CHECK_RESULT(update_task_validate_optionbytes(update_task));
        }

        if((update_task->state.groups & UpdateTaskStageGroupFirmware) &&
           !update_task->firmware_written) {
            CHECK_RESULT(update_task_write_dfu(update_task));
        }

        update_trace_step(UpdateTraceStepDone, 0);
        furi_hal_rtc_set_boot_mode(FuriHalRtcBootModePostUpdate);
        // Clean up /int before restoring backup on next boot. A rollback has no backup (it
        // starts here, without the pre-update step) and leaves /int as it is.
        if(!boot_guard_rollback_requested()) {
            furi_hal_rtc_set_flag(FuriHalRtcFlagStorageFormatInternal);
        }
#ifdef FURI_NDEBUG
        // Production. The log level, Debug and the sleep method are the user's System settings
        // and stay as they were set (resetting them was why they never survived an update). A
        // log level never chosen (Default, which is Info) goes quiet, as it always did: every
        // line costs milliseconds on the log port. Heap tracking is a developer's tool that
        // slows everything down, and goes.
        if(furi_hal_rtc_get_log_level() == FuriLogLevelDefault) {
            furi_hal_rtc_set_log_level(FuriLogLevelNone);
        }
        furi_hal_rtc_set_heap_track_mode(FuriHalRtcHeapTrackModeNone);
#endif
        update_task_set_progress(update_task, UpdateTaskStageCompleted, 100);
        success = true;
    } while(false);

    if(!success) {
        update_trace_step(UpdateTraceStepFailed, (uint8_t)update_task->state.stage);
        update_task_set_progress(update_task, UpdateTaskStageError, 0);
        return UPDATE_TASK_FAILED;
    }

    return UPDATE_TASK_NOERR;
}
