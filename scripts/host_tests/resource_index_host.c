/* Host driver for applications/system/updater/util/update_resource_index.c.
 *
 * stdin lines:  F <name> <size> <md5 hex>   add a file
 *               D <name>                    add a folder
 *               .                           finish (sort)
 *               ? <name> <size> <md5 hex>   prints "match", "differs", "folder" or "absent"
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "update_resource_index.h"

static void parse_md5(const char* hex, unsigned char out[16]) {
    for(int i = 0; i < 16; i++) {
        unsigned value = 0;
        sscanf(hex + 2 * i, "%2x", &value);
        out[i] = (unsigned char)value;
    }
}

int main(void) {
    UpdateResourceIndex index;
    update_resource_index_init(&index);
    char line[1024], kind, name[512], hex[64];
    unsigned long size;
    unsigned char md5[16];
    while(fgets(line, sizeof(line), stdin)) {
        kind = line[0];
        if(kind == '.') {
            update_resource_index_finish(&index);
        } else if(kind == 'F' && sscanf(line + 2, "%511s %lu %63s", name, &size, hex) == 3) {
            parse_md5(hex, md5);
            if(!update_resource_index_add(&index, name, (unsigned)size, md5)) return 3;
        } else if(kind == 'D' && sscanf(line + 2, "%511s", name) == 1) {
            if(!update_resource_index_add(&index, name, UPDATE_RESOURCE_DIRECTORY, NULL)) return 3;
        } else if(kind == '?' && sscanf(line + 2, "%511s %lu %63s", name, &size, hex) == 3) {
            parse_md5(hex, md5);
            const UpdateResourceIndexEntry* entry = update_resource_index_find(&index, name);
            if(!entry) {
                puts("absent");
            } else if(entry->size == UPDATE_RESOURCE_DIRECTORY) {
                puts("folder");
            } else {
                puts(update_resource_index_matches(entry, (unsigned)size, md5) ? "match" : "differs");
            }
        }
    }
    update_resource_index_free(&index);
    return 0;
}
