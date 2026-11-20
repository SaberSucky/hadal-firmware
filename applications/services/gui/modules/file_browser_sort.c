#include "file_browser_sort.h"

#include <string.h>

FileBrowserSortPrefs file_browser_sort_prefs = {0};

bool file_browser_sort_needs_key(void) {
    return file_browser_sort_prefs.sort_by == FileBrowserSortNewest ||
           file_browser_sort_prefs.sort_by == FileBrowserSortLargest;
}

static const char* base_name(const char* path) {
    const char* slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

// ASCII case-fold compare, the order the lists always had
static int compare_names(const char* a, const char* b) {
    unsigned char ca, cb;
    do {
        ca = (unsigned char)*a++;
        cb = (unsigned char)*b++;
        if((ca >= 'A') && (ca <= 'Z')) ca += 'a' - 'A';
        if((cb >= 'A') && (cb <= 'Z')) cb += 'a' - 'A';
    } while((ca == cb) && (ca != '\0'));
    return (int)ca - (int)cb;
}

// "" for a name with no extension (or one starting with its only dot)
static const char* extension(const char* name, bool folder) {
    if(folder) return "";
    const char* dot = strrchr(name, '.');
    return dot && dot != name ? dot + 1 : "";
}

int file_browser_sort_compare(
    const char* a,
    bool a_folder,
    uint32_t a_key,
    const char* b,
    bool b_folder,
    uint32_t b_key) {
    if(!file_browser_sort_prefs.folders_mixed && a_folder != b_folder) {
        return a_folder ? -1 : 1;
    }
    const char* name_a = base_name(a);
    const char* name_b = base_name(b);
    switch(file_browser_sort_prefs.sort_by) {
    case FileBrowserSortNameReverse:
        return compare_names(name_b, name_a);
    case FileBrowserSortNewest:
    case FileBrowserSortLargest:
        // biggest key first; equal keys by name
        if(a_key != b_key) return a_key > b_key ? -1 : 1;
        break;
    case FileBrowserSortType: {
        const int by_type =
            compare_names(extension(name_a, a_folder), extension(name_b, b_folder));
        if(by_type) return by_type;
        break;
    }
    default:
        break;
    }
    return compare_names(name_a, name_b);
}
