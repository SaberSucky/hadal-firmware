/* Host driver for applications/services/gui/modules/file_browser_sort.c and the name match of
 * applications/main/archive/helpers/archive_search.c.
 *
 * argv: sort SORT_BY FOLDERS_MIXED   stdin: "<name> <0|1 folder> <key>" lines; prints the names
 *                                    in order
 *       match NAME NEEDLE            prints 1 or 0
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "file_browser_sort.h"

bool archive_search_matches(const char* name, const char* needle);

typedef struct {
    char name[128];
    int folder;
    unsigned key;
} Entry;

static int compare(const void* a, const void* b) {
    const Entry* x = a;
    const Entry* y = b;
    return file_browser_sort_compare(x->name, x->folder, x->key, y->name, y->folder, y->key);
}

int main(int argc, char** argv) {
    if(argc == 4 && !strcmp(argv[1], "match")) {
        printf("%d\n", archive_search_matches(argv[2], argv[3]) ? 1 : 0);
        return 0;
    }
    if(argc != 4 || strcmp(argv[1], "sort")) return 2;
    file_browser_sort_prefs.sort_by = (uint8_t)atoi(argv[2]);
    file_browser_sort_prefs.folders_mixed = (uint8_t)atoi(argv[3]);
    Entry entries[64];
    size_t count = 0;
    while(count < 64 && scanf("%127s %d %u", entries[count].name, &entries[count].folder, &entries[count].key) == 3) {
        count++;
    }
    qsort(entries, count, sizeof(Entry), compare);
    for(size_t i = 0; i < count; i++) printf("%s\n", entries[i].name);
    return 0;
}
