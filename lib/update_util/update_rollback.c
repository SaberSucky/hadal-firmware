#include "update_rollback.h"

#include <furi.h>
#include <toolbox/path.h>
#include <flipper_format/flipper_format.h>

#define TAG "UpdRollback"

#define ROLLBACK_NEXT_MANIFEST UPDATE_ROLLBACK_NEXT_DIR "/" UPDATE_MANIFEST_DEFAULT_NAME

static bool rollback_copy(Storage* storage, const char* package_dir, const FuriString* name) {
    FuriString* from = furi_string_alloc();
    FuriString* to = furi_string_alloc();
    path_concat(package_dir, furi_string_get_cstr(name), from);
    path_concat(UPDATE_ROLLBACK_NEXT_DIR, furi_string_get_cstr(name), to);
    const bool copied =
        storage_common_copy(storage, furi_string_get_cstr(from), furi_string_get_cstr(to)) ==
        FSE_OK;
    furi_string_free(from);
    furi_string_free(to);
    return copied;
}

static bool rollback_write_manifest(Storage* storage, const UpdateManifest* manifest) {
    FlipperFormat* file = flipper_format_file_alloc(storage);
    const uint32_t crc = manifest->staged_loader_crc;
    bool written =
        flipper_format_file_open_always(file, ROLLBACK_NEXT_MANIFEST) &&
        flipper_format_write_header_cstr(
            file, "Flipper firmware upgrade configuration", manifest->manifest_version) &&
        flipper_format_write_comment_cstr(
            file, "Hadal rollback package: the last firmware that started steadily") &&
        flipper_format_write_string(file, "Info", manifest->version) &&
        flipper_format_write_uint32(file, "Target", &manifest->target, 1) &&
        flipper_format_write_string(file, "Loader", manifest->staged_loader_file) &&
        flipper_format_write_hex(file, "Loader CRC", (const uint8_t*)&crc, sizeof(crc)) &&
        flipper_format_write_string(file, "Firmware", manifest->firmware_dfu_image);
    flipper_format_free(file);
    return written;
}

bool update_rollback_stage(
    Storage* storage,
    const char* package_dir,
    const UpdateManifest* manifest) {
    if(furi_string_empty(manifest->firmware_dfu_image) ||
       furi_string_empty(manifest->staged_loader_file)) {
        return false;
    }
    storage_simply_remove_recursive(storage, UPDATE_ROLLBACK_NEXT_DIR);
    const bool staged = storage_simply_mkdir(storage, UPDATE_ROLLBACK_NEXT_DIR) &&
                        rollback_copy(storage, package_dir, manifest->staged_loader_file) &&
                        rollback_copy(storage, package_dir, manifest->firmware_dfu_image) &&
                        rollback_write_manifest(storage, manifest);
    if(!staged) {
        FURI_LOG_W(TAG, "Could not stage the rollback package");
        storage_simply_remove_recursive(storage, UPDATE_ROLLBACK_NEXT_DIR);
    }
    return staged;
}

bool update_rollback_promote(Storage* storage) {
    if(storage_file_exists(storage, ROLLBACK_NEXT_MANIFEST)) {
        storage_simply_remove_recursive(storage, UPDATE_ROLLBACK_DIR);
        if(storage_common_rename(storage, UPDATE_ROLLBACK_NEXT_DIR, UPDATE_ROLLBACK_DIR) !=
           FSE_OK) {
            FURI_LOG_W(TAG, "Could not promote the staged rollback package");
        }
    }
    return update_rollback_available(storage);
}

bool update_rollback_available(Storage* storage) {
    return storage_file_exists(storage, UPDATE_ROLLBACK_MANIFEST);
}
