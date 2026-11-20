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

uint32_t file_browser_sort_key_of(uint64_t size, uint32_t modified) {
    // (storage_common_timestamp() gave the card's last write, the same for every file)
    if(file_browser_sort_prefs.sort_by == FileBrowserSortNewest) return modified;
    return size > UINT32_MAX ? UINT32_MAX : (uint32_t)size;
}
