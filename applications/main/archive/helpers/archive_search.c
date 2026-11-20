#include "archive_search.h"

#include <m-array.h>

ARRAY_DEF(SearchFolders, FuriString*, M_PTR_OPLIST) //-V658

// folders waiting to be looked into; beyond this many, the deepest are skipped
#define SEARCH_MAX_PENDING (256U)

static char lower(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c + ('a' - 'A')) : c;
}

bool archive_search_matches(const char* name, const char* needle) {
    if(!needle[0]) return false;
    for(; *name; name++) {
        size_t i = 0;
        while(needle[i] && name[i] && lower(name[i]) == lower(needle[i])) {
            i++;
        }
        if(!needle[i]) return true;
    }
    return false;
}

size_t archive_search(
    Storage* storage,
    const char* root,
    const char* needle,
    bool skip_hidden,
    FuriString** results,
    size_t max,
    uint32_t limit) {
    size_t found = 0;
    uint32_t seen = 0;
    SearchFolders_t pending;
    SearchFolders_init(pending);
    SearchFolders_push_back(pending, furi_string_alloc_set_str(root));

    File* dir = storage_file_alloc(storage);
    char name[256];
    FileInfo info;
    // breadth first: the folders near the top, where people look first, come first
    for(size_t next = 0; next < SearchFolders_size(pending) && found < max && seen < limit;
        next++) {
        FuriString* folder = *SearchFolders_get(pending, next);
        if(!storage_dir_open(dir, furi_string_get_cstr(folder))) {
            storage_dir_close(dir);
            continue;
        }
        while(found < max && seen < limit && storage_dir_read(dir, &info, name, sizeof(name))) {
            seen++;
            if(skip_hidden && name[0] == '.') continue;
            const bool is_dir = file_info_is_dir(&info);
            if(archive_search_matches(name, needle)) {
                results[found++] =
                    furi_string_alloc_printf("%s/%s", furi_string_get_cstr(folder), name);
            }
            if(is_dir && SearchFolders_size(pending) < SEARCH_MAX_PENDING) {
                SearchFolders_push_back(
                    pending, furi_string_alloc_printf("%s/%s", furi_string_get_cstr(folder), name));
            }
        }
        storage_dir_close(dir);
    }
    storage_file_free(dir);

    for(size_t i = 0; i < SearchFolders_size(pending); i++) {
        furi_string_free(*SearchFolders_get(pending, i));
    }
    SearchFolders_clear(pending);
    return found;
}
