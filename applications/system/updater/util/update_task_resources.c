#include "update_task_resources.h"
#include "update_task_i.h"
#include "update_log.h"
#include "update_resource_index.h"

#include <furi.h>
#include <storage/storage.h>
#include <toolbox/path.h>
#include <toolbox/md5_calc.h>
#include <toolbox/tar/tar_archive.h>
#include <update_util/resources/manifest.h>

#define TAG "UpdResources"

#define ARCHIVE_MANIFEST "Manifest"
#define STAGED_MANIFEST  "resources.manifest" /* in the update folder */
#define CARD_MANIFEST    EXT_PATH("Manifest")

typedef struct {
    UpdateTask* update_task;
    TarArchive* archive;
    const UpdateResourceIndex* index;
    File* file;
    FuriString* path;
    uint32_t written;
    uint32_t unchanged;
} ResourceUnpack;

static bool
    resources_load_index(Storage* storage, const char* manifest, UpdateResourceIndex* index) {
    ResourceManifestReader* reader = resource_manifest_reader_alloc(storage);
    bool ok = resource_manifest_reader_open(reader, manifest);
    ResourceManifestEntry* entry;
    while(ok && (entry = resource_manifest_reader_next(reader))) {
        if(entry->type == ResourceManifestEntryTypeFile) {
            ok = update_resource_index_add(
                index, furi_string_get_cstr(entry->name), entry->size, entry->hash);
        } else if(entry->type == ResourceManifestEntryTypeDirectory) {
            ok = update_resource_index_add(
                index, furi_string_get_cstr(entry->name), UPDATE_RESOURCE_DIRECTORY, NULL);
        }
    }
    resource_manifest_reader_free(reader);
    if(ok) update_resource_index_finish(index);
    return ok && index->count > 0;
}

// Files and folders the old resources had and the new ones do not
static uint32_t resources_remove_stale(UpdateTask* update_task, const UpdateResourceIndex* index) {
    uint32_t removed = 0;
    ResourceManifestReader* reader = resource_manifest_reader_alloc(update_task->storage);
    FuriString* path = furi_string_alloc();
    do {
        if(!resource_manifest_reader_open(reader, CARD_MANIFEST)) break;

        uint32_t files = 1, dirs = 1, done = 0;
        ResourceManifestEntry* entry;
        while((entry = resource_manifest_reader_next(reader))) {
            if(entry->type == ResourceManifestEntryTypeFile) files++;
            if(entry->type == ResourceManifestEntryTypeDirectory) dirs++;
        }
        resource_manifest_rewind(reader);

        update_task_set_progress(update_task, UpdateTaskStageResourcesFileCleanup, 0);
        while((entry = resource_manifest_reader_next(reader))) {
            if(entry->type != ResourceManifestEntryTypeFile) continue;
            update_task_set_progress(update_task, UpdateTaskStageProgress, done++ * 100 / files);
            if(update_resource_index_find(index, furi_string_get_cstr(entry->name))) continue;
            path_concat(STORAGE_EXT_PATH_PREFIX, furi_string_get_cstr(entry->name), path);
            if(storage_common_remove(update_task->storage, furi_string_get_cstr(path)) == FSE_OK) {
                removed++;
            }
        }

        update_task_set_progress(update_task, UpdateTaskStageResourcesDirCleanup, 0);
        done = 0;
        while((entry = resource_manifest_reader_previous(reader))) {
            if(entry->type != ResourceManifestEntryTypeDirectory) continue;
            update_task_set_progress(update_task, UpdateTaskStageProgress, done++ * 100 / dirs);
            if(update_resource_index_find(index, furi_string_get_cstr(entry->name))) continue;
            path_concat(STORAGE_EXT_PATH_PREFIX, furi_string_get_cstr(entry->name), path);
            // a folder that still holds the user's own files stays
            storage_common_remove(update_task->storage, furi_string_get_cstr(path));
        }
    } while(false);
    furi_string_free(path);
    resource_manifest_reader_free(reader);
    return removed;
}

static bool resources_unpack_cb(const char* name, bool is_directory, void* context) {
    ResourceUnpack* unpack = context;

    int32_t progress = 0, total = 0;
    tar_archive_get_read_progress(unpack->archive, &progress, &total);
    update_task_set_progress(
        unpack->update_task, UpdateTaskStageProgress, (progress * 100) / (total + 1));

    if(is_directory) return true;
    // installed after everything else
    if(strcmp(name, ARCHIVE_MANIFEST) == 0) return false;

    const UpdateResourceIndexEntry* entry = update_resource_index_find(unpack->index, name);
    if(!entry || entry->size == UPDATE_RESOURCE_DIRECTORY) {
        unpack->written++;
        return true;
    }

    // Only a file that is already exactly the new one is left alone: its real size and MD5 are
    // checked, so a file edited or damaged on the card is put back as before
    Storage* storage = unpack->update_task->storage;
    path_concat(STORAGE_EXT_PATH_PREFIX, name, unpack->path);
    FileInfo info;
    uint8_t md5[16];
    FS_Error error;
    if(storage_common_stat(storage, furi_string_get_cstr(unpack->path), &info) == FSE_OK &&
       !file_info_is_dir(&info) && info.size == entry->size &&
       md5_calc_file(unpack->file, furi_string_get_cstr(unpack->path), md5, &error) &&
       update_resource_index_matches(entry, (uint32_t)info.size, md5)) {
        unpack->unchanged++;
        return false;
    }
    unpack->written++;
    return true;
}

bool update_task_resources_install(UpdateTask* update_task, const char* archive_path) {
    Storage* storage = update_task->storage;
    FuriString* staged = furi_string_alloc();
    path_concat(furi_string_get_cstr(update_task->update_path), STAGED_MANIFEST, staged);

    UpdateResourceIndex index;
    update_resource_index_init(&index);
    bool installed = false;

    do {
        // The new Manifest is the archive's first file, so this reads very little
        TarArchive* archive = tar_archive_alloc(storage);
        bool staged_ok =
            tar_archive_open(archive, archive_path, TarOpenModeReadHeatshrink) &&
            tar_archive_unpack_file(archive, ARCHIVE_MANIFEST, furi_string_get_cstr(staged));
        tar_archive_free(archive);
        if(!staged_ok || !resources_load_index(storage, furi_string_get_cstr(staged), &index)) {
            update_log("resources: no usable Manifest in the archive, replacing everything");
            break;
        }

        const uint32_t removed = resources_remove_stale(update_task, &index);

        ResourceUnpack unpack = {
            .update_task = update_task,
            .index = &index,
            .file = storage_file_alloc(storage),
            .path = furi_string_alloc(),
        };
        unpack.archive = tar_archive_alloc(storage);
        bool unpacked = tar_archive_open(unpack.archive, archive_path, TarOpenModeReadHeatshrink);
        if(unpacked) {
            update_task_set_progress(update_task, UpdateTaskStageResourcesFileUnpack, 0);
            tar_archive_set_file_callback(unpack.archive, resources_unpack_cb, &unpack);
            unpacked = tar_archive_unpack_to(unpack.archive, STORAGE_EXT_PATH_PREFIX, NULL);
        }
        tar_archive_free(unpack.archive);
        storage_file_free(unpack.file);
        furi_string_free(unpack.path);

        update_log(
            "resources: %lu written, %lu unchanged, %lu removed%s",
            (unsigned long)unpack.written,
            (unsigned long)unpack.unchanged,
            (unsigned long)removed,
            unpacked ? "" : ", stopped part way");
        if(!unpacked) break;

        // Last: the Manifest that describes what is now on the card
        storage_common_remove(storage, CARD_MANIFEST);
        installed = storage_common_copy(storage, furi_string_get_cstr(staged), CARD_MANIFEST) ==
                    FSE_OK;
    } while(false);

    storage_common_remove(storage, furi_string_get_cstr(staged));
    update_resource_index_free(&index);
    furi_string_free(staged);
    return installed;
}
