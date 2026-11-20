#include "file_browser_sort.h"

#include <furi.h>
#include <saved_struct.h>
#include <storage/storage.h>

void file_browser_sort_prefs_load(void) {
    FileBrowserSortPrefs prefs;
    if(saved_struct_load(
           FILE_BROWSER_SORT_PREFS_PATH,
           &prefs,
           sizeof(prefs),
           FILE_BROWSER_SORT_PREFS_MAGIC,
           FILE_BROWSER_SORT_PREFS_VERSION) &&
       prefs.sort_by < FileBrowserSortCount) {
        file_browser_sort_prefs = prefs;
    } else {
        memset(&file_browser_sort_prefs, 0, sizeof(file_browser_sort_prefs));
    }
}

uint32_t file_browser_sort_key(const char* path) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    uint32_t key = 0;
    if(file_browser_sort_prefs.sort_by == FileBrowserSortNewest) {
        if(storage_common_timestamp(storage, path, &key) != FSE_OK) key = 0;
    } else {
        FileInfo info;
        if(storage_common_stat(storage, path, &info) == FSE_OK) {
            key = info.size > UINT32_MAX ? UINT32_MAX : (uint32_t)info.size;
        }
    }
    furi_record_close(RECORD_STORAGE);
    return key;
}
