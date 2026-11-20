/**
 * @file file_browser_sort.h
 * @brief How file lists are sorted (Archive and the file pickers apps open), and whether Archive
 * shows hidden files: set in Settings > Hadal > Interface > File Browser, kept on the internal
 * storage. Lists of up to BROWSER_SORT_THRESHOLD entries are sorted; longer ones come in the
 * order the SD card holds them, as before.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FILE_BROWSER_SORT_PREFS_PATH    "/int/.browser.settings"
#define FILE_BROWSER_SORT_PREFS_MAGIC   (0x42)
#define FILE_BROWSER_SORT_PREFS_VERSION (1)

typedef enum {
    FileBrowserSortName, /**< A to Z, ignoring case */
    FileBrowserSortNameReverse,
    FileBrowserSortNewest, /**< last changed first */
    FileBrowserSortLargest,
    FileBrowserSortType, /**< by extension, then name */
    FileBrowserSortCount,
} FileBrowserSortBy;

typedef struct {
    uint8_t sort_by; /**< FileBrowserSortBy */
    uint8_t folders_mixed; /**< 0: folders first, as before */
    uint8_t hide_hidden; /**< 0: Archive's Browser tab shows dot files, as before */
    uint8_t reserved;
} FileBrowserSortPrefs;

/** The preferences in effect: the defaults until loaded */
extern FileBrowserSortPrefs file_browser_sort_prefs;

/** Read the preferences from the internal storage (the defaults when there are none) */
void file_browser_sort_prefs_load(void);

/** Whether the order needs each file's size or time (file_browser_sort_key) */
bool file_browser_sort_needs_key(void);

/** A file's size or time, whichever the order needs, from what its folder listed (a stat per
 * file walked the folder again for each one) */
uint32_t file_browser_sort_key_of(uint64_t size, uint32_t modified);

/**
 * Two entries of one folder, in the order the preferences give: negative if a comes first.
 * a and b are names or paths (compared from their last '/'); key from file_browser_sort_key
 */
int file_browser_sort_compare(
    const char* a,
    bool a_folder,
    uint32_t a_key,
    const char* b,
    bool b_folder,
    uint32_t b_key);

#ifdef __cplusplus
}
#endif
