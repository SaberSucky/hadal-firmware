/*
 * Host test of applications/settings/hadal_settings/pack_info.c, driven by scripts/asset_packer_test.py:
 * the real code that lists and counts asset packs, on host storage (ROOT/ext/asset_packs/...).
 *
 *   pack_info_host ROOT list         the pack folder names, one per line, in the order the app shows
 *   pack_info_host ROOT scan NAME    "animations icons fonts" of pack NAME
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <furi.h>

#include "../../applications/settings/hadal_settings/pack_info.c"

int main(int argc, char** argv) {
    if(argc < 3) return 2;
    host_storage_root = argv[1];
    Storage* storage = furi_record_open(RECORD_STORAGE);
    if(!strcmp(argv[2], "list")) {
        char names[64][HADAL_PACK_NAME_SIZE];
        size_t count = hadal_pack_list(storage, names, 64);
        for(size_t i = 0; i < count; i++) printf("%s\n", names[i]);
    } else if(!strcmp(argv[2], "scan") && argc == 4) {
        HadalPackInfo info;
        hadal_pack_info_scan(storage, argv[3], &info);
        printf("%u %u %u\n", info.animations, info.icons, info.fonts);
    } else {
        return 2;
    }
    furi_record_close(RECORD_STORAGE);
    return 0;
}
