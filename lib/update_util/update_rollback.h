/**
 * @file update_rollback.h
 * @brief The package to put back when a new firmware keeps failing to start.
 *
 * After each update, a slim copy of the package just installed (updater and firmware only, no
 * radio, resources, option bytes or splash: ~1 MB) is staged in UPDATE_ROLLBACK_NEXT_DIR. When
 * that firmware has run steadily, it becomes the rollback package (UPDATE_ROLLBACK_DIR). So the
 * rollback package is always the last firmware that started properly.
 */
#pragma once

#include <storage/storage.h>
#include "update_manifest.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UPDATE_ROLLBACK_DIR      "/ext/update/.hadal_rollback"
#define UPDATE_ROLLBACK_NEXT_DIR "/ext/update/.hadal_rollback_next"
#define UPDATE_ROLLBACK_MANIFEST UPDATE_ROLLBACK_DIR "/" UPDATE_MANIFEST_DEFAULT_NAME

/**
 * @brief Stage the package just installed as the next rollback package.
 * @param package_dir the folder of the installed package
 * @param manifest its manifest
 * @return false if there was nothing to stage (no firmware in it) or the copy failed
 */
bool update_rollback_stage(
    Storage* storage,
    const char* package_dir,
    const UpdateManifest* manifest);

/**
 * @brief The firmware has run steadily: the staged package becomes the rollback package.
 * @return whether a rollback package is in place afterwards
 */
bool update_rollback_promote(Storage* storage);

/** @brief Whether a rollback package is in place */
bool update_rollback_available(Storage* storage);

#ifdef __cplusplus
}
#endif
