#include "pack_info.h"

#include <furi.h>
#include <string.h>
#include <strings.h>

/* The packs' own layout, see documentation/AssetPacks.md */
#define MAX_DEPTH    5
#define PATH_SIZE    128
#define ENTRY_SIZE   96
#define MANIFEST_MAX 16384

static bool name_ok(const char* name) {
    size_t length = strlen(name);
    return length > 0 && length < HADAL_PACK_NAME_SIZE && name[0] != '.';
}

static bool join(char* out, size_t size, const char* base, const char* name) {
    int n = snprintf(out, size, "%s/%s", base, name);
    return n > 0 && (size_t)n < size;
}

static bool ends_with(const char* text, const char* suffix) {
    size_t text_length = strlen(text), suffix_length = strlen(suffix);
    return text_length >= suffix_length && strcmp(text + text_length - suffix_length, suffix) == 0;
}

size_t hadal_pack_list(Storage* storage, char (*names)[HADAL_PACK_NAME_SIZE], size_t max) {
    size_t count = 0;
    File* dir = storage_file_alloc(storage);
    if(storage_dir_open(dir, HADAL_PACKS_PATH)) {
        FileInfo info;
        char name[HADAL_PACK_NAME_SIZE + 1];
        while(count < max && storage_dir_read(dir, &info, name, sizeof(name))) {
            if(!file_info_is_dir(&info) || !name_ok(name)) continue;
            // insertion sort: there are few packs, and the list is shown in this order
            size_t at = count;
            while(at > 0 && strcasecmp(names[at - 1], name) > 0) {
                memcpy(names[at], names[at - 1], HADAL_PACK_NAME_SIZE);
                at--;
            }
            strlcpy(names[at], name, HADAL_PACK_NAME_SIZE);
            count++;
        }
        storage_dir_close(dir);
    }
    storage_file_free(dir);
    return count;
}

static uint16_t count_manifest_entries(Storage* storage, const char* manifest_path) {
    uint16_t count = 0;
    File* file = storage_file_alloc(storage);
    if(storage_file_open(file, manifest_path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        uint64_t size = storage_file_size(file);
        if(size > MANIFEST_MAX) size = MANIFEST_MAX;
        char chunk[64];
        char line[48];
        size_t line_length = 0;
        size_t left = (size_t)size;
        while(left > 0) {
            size_t want = left < sizeof(chunk) ? left : sizeof(chunk);
            size_t got = storage_file_read(file, chunk, want);
            if(got == 0) break;
            left -= got;
            for(size_t i = 0; i < got; i++) {
                if(chunk[i] == '\n') {
                    line[line_length] = '\0';
                    if(strncmp(line, "Name:", 5) == 0) count++;
                    line_length = 0;
                } else if(line_length + 1 < sizeof(line)) {
                    line[line_length++] = chunk[i];
                }
            }
        }
        line[line_length] = '\0';
        if(strncmp(line, "Name:", 5) == 0) count++; // a last line with no newline
    }
    storage_file_close(file);
    storage_file_free(file);
    return count;
}

/** Icons below dir: .bmx files, and folders holding a `meta` file (animated icons). */
static void
    count_icons(Storage* storage, const char* dir_path, unsigned depth, HadalPackInfo* info) {
    if(depth > MAX_DEPTH) return;

    char meta_path[PATH_SIZE];
    if(depth > 0 && join(meta_path, sizeof(meta_path), dir_path, "meta") &&
       storage_common_exists(storage, meta_path)) {
        if(info->icons < UINT16_MAX) info->icons++;
        return;
    }

    File* dir = storage_file_alloc(storage);
    if(storage_dir_open(dir, dir_path)) {
        FileInfo entry;
        char name[ENTRY_SIZE];
        while(storage_dir_read(dir, &entry, name, sizeof(name))) {
            if(strlen(name) + 1 >= sizeof(name)) continue;
            char child[PATH_SIZE];
            if(!join(child, sizeof(child), dir_path, name)) continue;
            if(file_info_is_dir(&entry)) {
                count_icons(storage, child, depth + 1, info);
            } else if(ends_with(name, ".bmx") && info->icons < UINT16_MAX) {
                info->icons++;
            }
        }
        storage_dir_close(dir);
    }
    storage_file_free(dir);
}

static uint8_t count_fonts(Storage* storage, const char* fonts_path) {
    uint8_t count = 0;
    File* dir = storage_file_alloc(storage);
    if(storage_dir_open(dir, fonts_path)) {
        FileInfo entry;
        char name[ENTRY_SIZE];
        while(storage_dir_read(dir, &entry, name, sizeof(name))) {
            if(!file_info_is_dir(&entry) && ends_with(name, ".u8f") && count < UINT8_MAX) count++;
        }
        storage_dir_close(dir);
    }
    storage_file_free(dir);
    return count;
}

void hadal_pack_info_scan(Storage* storage, const char* name, HadalPackInfo* info) {
    memset(info, 0, sizeof(*info));
    if(!name_ok(name)) return;

    char pack[PATH_SIZE], path[PATH_SIZE];
    if(!join(pack, sizeof(pack), HADAL_PACKS_PATH, name)) return;

    if(join(path, sizeof(path), pack, "Anims/manifest.txt")) {
        info->animations = count_manifest_entries(storage, path);
    }
    if(join(path, sizeof(path), pack, "Icons")) {
        count_icons(storage, path, 0, info);
    }
    if(join(path, sizeof(path), pack, "Fonts")) {
        info->fonts = count_fonts(storage, path);
    }
}
