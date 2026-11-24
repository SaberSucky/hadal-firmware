#include "update_task_resources.h"
#include "update_task_i.h"
#include "update_log.h"
#include "update_resource_index.h"

#include <furi.h>
#include <storage/storage.h>
#include <toolbox/path.h>
#include <toolbox/md5_calc.h>
#include <toolbox/tar/tar_archive.h>
#include <toolbox/dir_walk.h>
#include <update_util/resources/manifest.h>

#define TAG "UpdResources"

#define ARCHIVE_MANIFEST "Manifest"
#define STAGED_MANIFEST  "resources.manifest" /* in the update folder */
#define CARD_MANIFEST    EXT_PATH("Manifest")

/* What the last install from this update folder unpacked, kept in the folder:
 *
 *     M <MD5 of /ext/Manifest as that install left it>
 *     <extra archive> <its CRC, from update.fuf>
 *     ...
 *
 * An extra archive the record lists with the CRC update.fuf gives it now is already on the card,
 * and is not unpacked again: most of an update's time went on reading every archive and every
 * file it holds, to find the few that changed. Only while the card's Manifest is still the one
 * that install left (nothing else has installed resources since: another firmware, qFlipper's
 * own resource install); otherwise, with no record, or with an older package, everything is
 * checked as before. Deleting the update folder makes the next update check everything too. */
#define INSTALLED_RECORD     ".installed"
#define INSTALLED_RECORD_MAX (8U * 1024U)

typedef struct {
    UpdateTask* update_task;
    TarArchive* archive;
    const UpdateResourceIndex* index;
    File* file;
    FuriString* path;
    uint32_t written;
    uint32_t unchanged;
    // One progress bar over every archive: sizes of the archives done, of all, of this one
    uint64_t bytes_done;
    uint64_t bytes_all;
    uint64_t bytes_current;
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

bool update_task_resources_is_kept(const char* name) {
    // Asset packs the previous firmware installed (Momentum's, say) are the user's to keep:
    // removing them took the animations out of packs people went on using
    return strncmp(name, "asset_packs/", strlen("asset_packs/")) == 0;
}

#define resources_is_kept update_task_resources_is_kept

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
            if(resources_is_kept(furi_string_get_cstr(entry->name))) continue;
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
            if(resources_is_kept(furi_string_get_cstr(entry->name))) continue;
            path_concat(STORAGE_EXT_PATH_PREFIX, furi_string_get_cstr(entry->name), path);
            // a folder that still holds the user's own files stays
            storage_common_remove(update_task->storage, furi_string_get_cstr(path));
        }
    } while(false);
    furi_string_free(path);
    resource_manifest_reader_free(reader);
    return removed;
}

/* Hadal's own pack is the firmware's, unlike the other packs on the card: what of it the new
 * resources do not hold goes. Fonts an older Hadal pack had stayed on the card through every update
 * since (they were not in the Manifest any more for the stale check to find), and changed the
 * look. Found first, then removed: a folder is not changed while it is read. The list is kept
 * small and walked again while a pass filled it (an old pack held well over a hundred files) */
#define OWN_PACK            "asset_packs/Hadal"
#define OWN_PACK_LIST_MAX   (2U * 1024U)
#define OWN_PACK_PASSES_MAX 32U

static uint32_t resources_prune_own_pack(Storage* storage, const UpdateResourceIndex* index) {
    // resources without the pack (a build that left it out) leave it as it is
    if(!update_resource_index_find(index, OWN_PACK)) return 0;
    const size_t prefix = strlen(STORAGE_EXT_PATH_PREFIX) + 1; // "/ext/"
    FuriString* found = furi_string_alloc();
    FuriString* path = furi_string_alloc();
    FileInfo info;
    uint32_t removed = 0;
    bool full = true;
    for(uint32_t pass = 0; full && pass < OWN_PACK_PASSES_MAX; pass++) {
        full = false;
        DirWalk* walk = dir_walk_alloc(storage);
        dir_walk_set_recursive(walk, true);
        if(dir_walk_open(walk, STORAGE_EXT_PATH_PREFIX "/" OWN_PACK)) {
            while(dir_walk_read(walk, path, &info) == DirWalkOK) {
                if(furi_string_size(path) <= prefix) continue;
                if(update_resource_index_find(index, furi_string_get_cstr(path) + prefix))
                    continue;
                if(furi_string_size(found) + furi_string_size(path) + 1 > OWN_PACK_LIST_MAX) {
                    full = true;
                    break;
                }
                furi_string_cat(found, path);
                furi_string_push_back(found, '|');
            }
        }
        dir_walk_close(walk);
        dir_walk_free(walk);

        // last found first: a folder is listed before what is in it, so it is empty by its turn
        const uint32_t before = removed;
        while(furi_string_size(found)) {
            furi_string_left(found, furi_string_size(found) - 1); // its '|'
            const size_t cut = furi_string_search_rchar(found, '|');
            const size_t from = cut == FURI_STRING_FAILURE ? 0 : cut + 1;
            furi_string_set_n(path, found, from, furi_string_size(found) - from);
            if(storage_common_remove(storage, furi_string_get_cstr(path)) == FSE_OK) removed++;
            furi_string_left(found, from);
        }
        if(removed == before) break; // nothing would go: walking again finds the same
    }
    furi_string_free(path);
    furi_string_free(found);
    return removed;
}

static bool resources_unpack_cb(const char* name, bool is_directory, void* context) {
    ResourceUnpack* unpack = context;

    int32_t progress = 0, total = 0;
    tar_archive_get_read_progress(unpack->archive, &progress, &total);
    const uint64_t here = unpack->bytes_current * (uint64_t)progress / (uint64_t)(total + 1);
    update_task_set_progress(
        unpack->update_task,
        UpdateTaskStageProgress,
        (unpack->bytes_done + here) * 100 / (unpack->bytes_all + 1));

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

// The next space-separated word of a list
static bool resources_next_word(const FuriString* list_string, size_t* offset, FuriString* word) {
    const char* list = furi_string_get_cstr(list_string);
    size_t start = *offset;
    while(list[start] == ' ') start++;
    if(!list[start]) return false;
    size_t end = start;
    while(list[end] && list[end] != ' ') end++;
    furi_string_set_strn(word, list + start, end - start);
    *offset = end;
    return true;
}

bool update_task_resources_next_extra(UpdateTask* update_task, size_t* offset, FuriString* path) {
    FuriString* name = furi_string_alloc();
    const bool found = resources_next_word(update_task->manifest->resource_extras, offset, name);
    if(found) {
        path_concat(
            furi_string_get_cstr(update_task->update_path), furi_string_get_cstr(name), path);
    }
    furi_string_free(name);
    return found;
}

static void resources_record_path(UpdateTask* update_task, FuriString* path) {
    path_concat(furi_string_get_cstr(update_task->update_path), INSTALLED_RECORD, path);
}

/** The record's text, when the card is still as it says; otherwise empty. Then it is removed:
 * an install that stops part way leaves none, and the next one checks everything */
static void resources_record_take(UpdateTask* update_task, File* file, FuriString* record) {
    Storage* storage = update_task->storage;
    FuriString* path = furi_string_alloc();
    FuriString* md5 = furi_string_alloc();
    FuriString* expected = furi_string_alloc();
    resources_record_path(update_task, path);
    furi_string_reset(record);
    do {
        FileInfo info;
        if(storage_common_stat(storage, furi_string_get_cstr(path), &info) != FSE_OK) break;
        if(info.size == 0 || info.size > INSTALLED_RECORD_MAX) break;
        if(!md5_string_calc_file(file, CARD_MANIFEST, md5, NULL)) break;
        furi_string_printf(expected, "M %s\n", furi_string_get_cstr(md5));

        char* text = malloc(info.size + 1);
        size_t read = 0;
        if(storage_file_open(file, furi_string_get_cstr(path), FSAM_READ, FSOM_OPEN_EXISTING)) {
            read = storage_file_read(file, text, info.size);
        }
        storage_file_close(file);
        text[read] = '\0';
        if(read == info.size &&
           strncmp(text, furi_string_get_cstr(expected), furi_string_size(expected)) == 0) {
            furi_string_set_str(record, text);
        }
        free(text);
    } while(false);
    storage_common_remove(storage, furi_string_get_cstr(path));
    furi_string_free(expected);
    furi_string_free(md5);
    furi_string_free(path);
}

/** The record's line for an extra archive: "<name> <CRC>\n", or empty without a CRC */
static void resources_record_line(
    UpdateTask* update_task,
    size_t* name_offset,
    size_t* crc_offset,
    FuriString* line) {
    FuriString* name = furi_string_alloc();
    FuriString* crc = furi_string_alloc();
    furi_string_reset(line);
    if(resources_next_word(update_task->manifest->resource_extras, name_offset, name) &&
       resources_next_word(update_task->manifest->resource_extras_crc, crc_offset, crc) &&
       furi_string_size(crc) == 8) {
        furi_string_printf(
            line, "%s %s\n", furi_string_get_cstr(name), furi_string_get_cstr(crc));
    }
    furi_string_free(crc);
    furi_string_free(name);
}

/** Whether the record lists this line (every line it has but the first follows a newline) */
static bool resources_record_has(const FuriString* record, const FuriString* line) {
    if(furi_string_empty(record) || furi_string_empty(line)) return false;
    FuriString* needle = furi_string_alloc_printf("\n%s", furi_string_get_cstr(line));
    const bool found = furi_string_search(record, needle) != FURI_STRING_FAILURE;
    furi_string_free(needle);
    return found;
}

/** Write the record of what is now on the card: lines, after the Manifest's MD5 */
static void resources_record_save(UpdateTask* update_task, File* file, const FuriString* lines) {
    if(furi_string_empty(lines)) return;
    FuriString* path = furi_string_alloc();
    FuriString* md5 = furi_string_alloc();
    FuriString* head = furi_string_alloc();
    resources_record_path(update_task, path);
    if(md5_string_calc_file(file, CARD_MANIFEST, md5, NULL)) {
        furi_string_printf(head, "M %s\n", furi_string_get_cstr(md5));
        bool written = false;
        if(storage_file_open(file, furi_string_get_cstr(path), FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
            const size_t head_size = furi_string_size(head), body = furi_string_size(lines);
            written = storage_file_write(file, furi_string_get_cstr(head), head_size) ==
                          head_size &&
                      storage_file_write(file, furi_string_get_cstr(lines), body) == body;
        }
        storage_file_close(file);
        if(!written) storage_common_remove(update_task->storage, furi_string_get_cstr(path));
    }
    furi_string_free(head);
    furi_string_free(md5);
    furi_string_free(path);
}

uint64_t update_task_resources_file_size(UpdateTask* update_task, const char* path) {
    FileInfo info;
    return storage_common_stat(update_task->storage, path, &info) == FSE_OK ? info.size : 0;
}

uint64_t update_task_resources_total_size(UpdateTask* update_task, const char* archive_path) {
    uint64_t total = update_task_resources_file_size(update_task, archive_path);
    FuriString* extra = furi_string_alloc();
    for(size_t offset = 0; update_task_resources_next_extra(update_task, &offset, extra);) {
        total += update_task_resources_file_size(update_task, furi_string_get_cstr(extra));
    }
    furi_string_free(extra);
    return total;
}

static bool resources_unpack(ResourceUnpack* unpack, const char* archive_path) {
    unpack->bytes_current = update_task_resources_file_size(unpack->update_task, archive_path);
    unpack->archive = tar_archive_alloc(unpack->update_task->storage);
    bool unpacked = tar_archive_open(unpack->archive, archive_path, TarOpenModeReadHeatshrink);
    if(unpacked) {
        tar_archive_set_file_callback(unpack->archive, resources_unpack_cb, unpack);
        unpacked = tar_archive_unpack_to(unpack->archive, STORAGE_EXT_PATH_PREFIX, NULL);
    }
    tar_archive_free(unpack->archive);
    unpack->archive = NULL;
    unpack->bytes_done += unpack->bytes_current;
    return unpacked;
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

        ResourceUnpack unpack = {
            .update_task = update_task,
            .index = &index,
            .file = storage_file_alloc(storage),
            .path = furi_string_alloc(),
        };
        // before anything on the card changes: which archives it already holds
        FuriString* record = furi_string_alloc();
        resources_record_take(update_task, unpack.file, record);

        const uint32_t removed = resources_remove_stale(update_task, &index);

        FuriString* extra = furi_string_alloc();
        FuriString* line = furi_string_alloc();
        FuriString* lines = furi_string_alloc();
        // the bar runs once over the archives to unpack, by their sizes
        unpack.bytes_all = update_task_resources_file_size(update_task, archive_path);
        for(size_t offset = 0, names = 0, crcs = 0;
            update_task_resources_next_extra(update_task, &offset, extra);) {
            resources_record_line(update_task, &names, &crcs, line);
            if(!resources_record_has(record, line)) {
                unpack.bytes_all +=
                    update_task_resources_file_size(update_task, furi_string_get_cstr(extra));
            }
        }

        update_task_set_progress(update_task, UpdateTaskStageResourcesFileUnpack, 0);
        bool unpacked = resources_unpack(&unpack, archive_path);
        // then the big files, the app folders and the data folders, each in an archive of its own
        uint32_t skipped = 0;
        for(size_t offset = 0, names = 0, crcs = 0;
            unpacked && update_task_resources_next_extra(update_task, &offset, extra);) {
            resources_record_line(update_task, &names, &crcs, line);
            if(resources_record_has(record, line)) {
                skipped++;
            } else {
                unpacked = resources_unpack(&unpack, furi_string_get_cstr(extra));
            }
            furi_string_cat(lines, line);
        }

        update_log(
            "resources: %lu written, %lu unchanged, %lu removed, %lu archives already there%s",
            (unsigned long)unpack.written,
            (unsigned long)unpack.unchanged,
            (unsigned long)removed,
            (unsigned long)skipped,
            unpacked ? "" : ", stopped part way");

        if(unpacked) {
            const uint32_t pruned = resources_prune_own_pack(storage, &index);
            if(pruned) update_log("resources: %lu old files of Hadal's pack removed", (unsigned long)pruned);
            // Last: the Manifest that describes what is now on the card, then the record of it
            storage_common_remove(storage, CARD_MANIFEST);
            installed = storage_common_copy(
                            storage, furi_string_get_cstr(staged), CARD_MANIFEST) == FSE_OK;
            if(installed) resources_record_save(update_task, unpack.file, lines);
        }

        furi_string_free(lines);
        furi_string_free(line);
        furi_string_free(extra);
        furi_string_free(record);
        storage_file_free(unpack.file);
        furi_string_free(unpack.path);
    } while(false);

    storage_common_remove(storage, furi_string_get_cstr(staged));
    update_resource_index_free(&index);
    furi_string_free(staged);
    return installed;
}
