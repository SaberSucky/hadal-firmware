#include "boot_timeline_save.h"

#include <furi.h>
#include <furi_hal_version.h>
#include <toolbox/boot_profile.h>

bool boot_timeline_save(Storage* storage) {
    if(!boot_profile_count() || storage_sd_status(storage) != FSE_OK) return false;
    storage_simply_mkdir(storage, EXT_PATH("apps_data"));
    storage_simply_mkdir(storage, BOOT_TIMELINE_DIR);

    FuriString* text = furi_string_alloc();
    const Version* version = furi_hal_version_get_firmware_version();
    furi_string_printf(
        text,
        "Start-up timeline, firmware %s (%s)\n"
        "when each step ended (ms since the system started), and how long after the one before\n\n",
        version ? version_get_version(version) : "?",
        version ? version_get_githash(version) : "?");
    uint32_t tick, previous = 0;
    const char* name;
    for(size_t i = 0; boot_profile_get(i, &tick, &name); i++) {
        furi_string_cat_printf(
            text, "%6lu ms  %+6ld  %s\n", (unsigned long)tick, (long)(tick - previous), name);
        previous = tick;
    }

    File* file = storage_file_alloc(storage);
    const size_t size = furi_string_size(text);
    const bool success =
        storage_file_open(file, BOOT_TIMELINE_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS) &&
        storage_file_write(file, furi_string_get_cstr(text), size) == size;
    storage_file_free(file);
    furi_string_free(text);
    return success;
}
