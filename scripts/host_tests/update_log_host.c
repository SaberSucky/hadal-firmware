/*
 * Host test of applications/system/updater/util/update_log.c, driven by scripts/asset_packer_test.py:
 * the real code that leaves a trail of the update on the SD card, on host storage.
 *
 *   update_log_host ROOT lines N    write N short lines
 *   update_log_host ROOT long       write lines far longer than the buffer, and format-heavy ones
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <furi.h>

#include "../../applications/system/updater/util/update_log.c"

int main(int argc, char** argv) {
    if(argc < 3) return 2;
    host_storage_root = argv[1];
    if(!strcmp(argv[2], "lines") && argc == 4) {
        for(int i = 0, n = atoi(argv[3]); i < n; i++) update_log("line %d", i);
    } else if(!strcmp(argv[2], "long")) {
        char big[1000];
        memset(big, 'x', sizeof(big) - 1);
        big[sizeof(big) - 1] = '\0';
        update_log("%s", big);
        update_log("%s%s%s", big, big, big);
        update_log("percent signs %% and numbers %d %08lX and a string %s", -5, 0xDEADBEEFul, "ok");
        update_log("%s", "");
    } else {
        return 2;
    }
    return 0;
}
