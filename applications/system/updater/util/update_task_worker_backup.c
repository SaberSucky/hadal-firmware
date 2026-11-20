#include "update_task.h"
#include "update_task_i.h"
#include "update_trace.h"
#include "update_task_resources.h"
#include "update_log.h"
#include <update_util/boot_guard.h>
#include <update_util/update_rollback.h>

#include <furi.h>
#include <furi_hal.h>
#include <storage/storage.h>
#include <desktop/helpers/slideshow_filename.h>
#include <toolbox/path.h>
#include <update_util/dfu_file.h>
#include <update_util/int_backup.h>
#include <update_util/update_operation.h>
#include <update_util/resources/manifest.h>
#include <toolbox/tar/tar_archive.h>
#include <toolbox/crc32_calc.h>

#define TAG "UpdWorkerBackup"

static bool update_task_pre_update(UpdateTask* update_task) {
    bool success = false;
    FuriString* backup_file_path;
    backup_file_path = furi_string_alloc();
    path_concat(
        furi_string_get_cstr(update_task->update_path),
        INT_BACKUP_DEFAULT_FILENAME,
        backup_file_path);

    update_task_set_progress(update_task, UpdateTaskStageIntBackup, 0);
    /* to avoid bootloops */
    furi_hal_rtc_set_boot_mode(FuriHalRtcBootModeNormal);
    if((success =
            int_backup_create(update_task->storage, furi_string_get_cstr(backup_file_path)))) {
        furi_hal_rtc_set_boot_mode(FuriHalRtcBootModeUpdate);
    }

    furi_string_free(backup_file_path);
    return success;
}
typedef struct {
    UpdateTask* update_task;
    TarArchive* archive;
} TarUnpackProgress;

static bool update_task_resource_unpack_cb(const char* name, bool is_directory, void* context) {
    UNUSED(name);
    UNUSED(is_directory);
    TarUnpackProgress* unpack_progress = context;
    int32_t progress = 0, total = 0;
    tar_archive_get_read_progress(unpack_progress->archive, &progress, &total);
    update_task_set_progress(
        unpack_progress->update_task, UpdateTaskStageProgress, (progress * 100) / (total + 1));
    return true;
}

static void update_task_cleanup_resources(UpdateTask* update_task) {
    ResourceManifestReader* manifest_reader = resource_manifest_reader_alloc(update_task->storage);
    do {
        FURI_LOG_D(TAG, "Cleaning up old manifest");
        if(!resource_manifest_reader_open(manifest_reader, EXT_PATH("Manifest"))) {
            FURI_LOG_W(TAG, "No existing manifest");
            break;
        }

        ResourceManifestEntry* entry_ptr = NULL;
        /* Iterate over manifest and calculate entries count */
        uint32_t n_file_entries = 1, n_dir_entries = 1;
        while((entry_ptr = resource_manifest_reader_next(manifest_reader))) {
            if(entry_ptr->type == ResourceManifestEntryTypeFile) {
                n_file_entries++;
            } else if(entry_ptr->type == ResourceManifestEntryTypeDirectory) {
                n_dir_entries++;
            }
        }
        resource_manifest_rewind(manifest_reader);

        update_task_set_progress(update_task, UpdateTaskStageResourcesFileCleanup, 0);
        uint32_t n_processed_file_entries = 0;
        while((entry_ptr = resource_manifest_reader_next(manifest_reader))) {
            if(entry_ptr->type == ResourceManifestEntryTypeFile) {
                update_task_set_progress(
                    update_task,
                    UpdateTaskStageProgress,
                    (n_processed_file_entries++ * 100) / n_file_entries);
                if(update_task_resources_is_kept(furi_string_get_cstr(entry_ptr->name))) continue;

                FuriString* file_path = furi_string_alloc();
                path_concat(
                    STORAGE_EXT_PATH_PREFIX, furi_string_get_cstr(entry_ptr->name), file_path);
                FURI_LOG_D(TAG, "Removing %s", furi_string_get_cstr(file_path));

                FS_Error result =
                    storage_common_remove(update_task->storage, furi_string_get_cstr(file_path));
                if(result != FSE_OK && result != FSE_EXIST) {
                    FURI_LOG_E(
                        TAG,
                        "%s remove failed, cause %s",
                        furi_string_get_cstr(file_path),
                        storage_error_get_desc(result));
                }
                furi_string_free(file_path);
            }
        }

        update_task_set_progress(update_task, UpdateTaskStageResourcesDirCleanup, 0);
        uint32_t n_processed_dir_entries = 0;
        while((entry_ptr = resource_manifest_reader_previous(manifest_reader))) {
            if(entry_ptr->type == ResourceManifestEntryTypeDirectory) {
                update_task_set_progress(
                    update_task,
                    UpdateTaskStageProgress,
                    (n_processed_dir_entries++ * 100) / n_dir_entries);

                FuriString* folder_path = furi_string_alloc();

                do {
                    path_concat(
                        STORAGE_EXT_PATH_PREFIX,
                        furi_string_get_cstr(entry_ptr->name),
                        folder_path);

                    FURI_LOG_D(TAG, "Removing folder %s", furi_string_get_cstr(folder_path));
                    FS_Error result = storage_common_remove(
                        update_task->storage, furi_string_get_cstr(folder_path));
                    if(result != FSE_OK && result != FSE_EXIST) {
                        FURI_LOG_E(
                            TAG,
                            "%s remove failed, cause %s",
                            furi_string_get_cstr(folder_path),
                            storage_error_get_desc(result));
                    }
                } while(false);

                furi_string_free(folder_path);
            }
        }
    } while(false);
    resource_manifest_reader_free(manifest_reader);
}

static bool update_task_post_update(UpdateTask* update_task) {
    bool success = false;

    FuriString* file_path;
    file_path = furi_string_alloc();

    TarArchive* archive = tar_archive_alloc(update_task->storage);
    do {
        path_concat(
            furi_string_get_cstr(update_task->update_path),
            INT_BACKUP_DEFAULT_FILENAME,
            file_path);

        // A rollback kept /int and has no backup of it
        if(!boot_guard_rollback_requested()) {
            update_task_set_progress(update_task, UpdateTaskStageIntRestore, 0);
            CHECK_RESULT(int_backup_unpack(update_task->storage, furi_string_get_cstr(file_path)));
        }

        if(update_task->state.groups & UpdateTaskStageGroupResources) {
            TarUnpackProgress progress = {
                .update_task = update_task,
                .archive = archive,
            };

            path_concat(
                furi_string_get_cstr(update_task->update_path),
                furi_string_get_cstr(update_task->manifest->resource_bundle),
                file_path);

            // Only what changed, when the archive says what it holds; otherwise, or if that
            // stops part way, everything is replaced as before
            if(!update_task_resources_install(update_task, furi_string_get_cstr(file_path))) {
                CHECK_RESULT(tar_archive_open(
                    archive, furi_string_get_cstr(file_path), TarOpenModeReadHeatshrink));

                update_task_cleanup_resources(update_task);

                update_task_set_progress(update_task, UpdateTaskStageResourcesFileUnpack, 0);
                tar_archive_set_file_callback(archive, update_task_resource_unpack_cb, &progress);
                CHECK_RESULT(tar_archive_unpack_to(archive, STORAGE_EXT_PATH_PREFIX, NULL));

                FuriString* extra = furi_string_alloc();
                bool extras_ok = true;
                for(size_t offset = 0; extras_ok && update_task_resources_next_extra(
                                                        update_task, &offset, extra);) {
                    TarArchive* extra_archive = tar_archive_alloc(update_task->storage);
                    progress.archive = extra_archive;
                    extras_ok = tar_archive_open(
                        extra_archive, furi_string_get_cstr(extra), TarOpenModeReadHeatshrink);
                    if(extras_ok) {
                        tar_archive_set_file_callback(
                            extra_archive, update_task_resource_unpack_cb, &progress);
                        extras_ok =
                            tar_archive_unpack_to(extra_archive, STORAGE_EXT_PATH_PREFIX, NULL);
                    }
                    tar_archive_free(extra_archive);
                }
                progress.archive = archive;
                furi_string_free(extra);
                CHECK_RESULT(extras_ok);
            }
        }

        if(update_task->state.groups & UpdateTaskStageGroupSplashscreen) {
            update_task_set_progress(update_task, UpdateTaskStageSplashscreenInstall, 0);
            FuriString* tmp_path;
            tmp_path = furi_string_alloc_set(update_task->update_path);
            path_append(tmp_path, furi_string_get_cstr(update_task->manifest->splash_file));
            if(storage_common_copy(
                   update_task->storage,
                   furi_string_get_cstr(tmp_path),
                   INT_PATH(SLIDESHOW_FILE_NAME)) != FSE_OK) {
                // actually, not critical
            }
            furi_string_free(tmp_path);
            update_task_set_progress(update_task, UpdateTaskStageSplashscreenInstall, 100);
        }
        success = true;
    } while(false);

    tar_archive_free(archive);
    furi_string_free(file_path);
    return success;
}

// After a successful install: a rollback is logged; a normal update stages itself as the next
// rollback package, and the boot guard watches its first starts
static void update_task_rollback_after_install(UpdateTask* update_task) {
    const bool was_rollback = boot_guard_rollback_requested();
    if(was_rollback) {
        update_log(
            "rollback: %s put back, the new firmware did not start",
            furi_string_get_cstr(update_task->manifest->version));
        // what was staged is the firmware that failed: it must never become the rollback
        storage_simply_remove_recursive(update_task->storage, UPDATE_ROLLBACK_NEXT_DIR);
    } else if(update_rollback_stage(
                  update_task->storage,
                  furi_string_get_cstr(update_task->update_path),
                  update_task->manifest)) {
        update_log(
            "rollback: this firmware is staged; it becomes the rollback once it runs steadily");
    }
    const bool available = update_rollback_available(update_task->storage);
    boot_guard_update_done(available, was_rollback);
    update_log(
        "rollback: %s", available ? "a rollback package is in place" : "no rollback package yet");
}

int32_t update_task_worker_backup_restore(void* context) {
    furi_assert(context);
    UpdateTask* update_task = context;

    FuriHalRtcBootMode boot_mode = update_task->boot_mode;
    if((boot_mode != FuriHalRtcBootModePreUpdate) && (boot_mode != FuriHalRtcBootModePostUpdate)) {
        /* no idea how we got here. Do nothing */
        return UPDATE_TASK_NOERR;
    }

    if(boot_mode == FuriHalRtcBootModePostUpdate) {
        /* What the part that ran from RAM (radio and firmware) left in the RTC registers */
        update_trace_report();
    }

    bool success = false;
    do {
        if(!update_task_parse_manifest(update_task)) {
            break;
        }

        if(boot_mode == FuriHalRtcBootModePreUpdate) {
            success = update_task_pre_update(update_task);
        } else if(boot_mode == FuriHalRtcBootModePostUpdate) { //-V547
            success = update_task_post_update(update_task);
            if(success) {
                update_task_rollback_after_install(update_task);
                update_operation_disarm();
            }
        }
    } while(false);

    if(!success) {
        update_task_set_progress(update_task, UpdateTaskStageError, 0);
        return UPDATE_TASK_FAILED;
    }

    update_task_set_progress(update_task, UpdateTaskStageCompleted, 100);
    return UPDATE_TASK_NOERR;
}
