#include "subghz_ignore.h"

#include <stdlib.h>

#define IGNORE_FILE_MAX (4096U)

size_t subghz_ignore_load(Storage* storage, uint32_t* hashes, size_t max) {
    File* file = storage_file_alloc(storage);
    size_t count = 0;
    if(storage_file_open(file, SUBGHZ_IGNORE_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        char* text = malloc(IGNORE_FILE_MAX + 1);
        const size_t size = storage_file_read(file, text, IGNORE_FILE_MAX);
        text[size] = '\0';
        for(char* line = text; line && *line && count < max;) {
            char* end = strchr(line, '\n');
            if(end) *end = '\0';
            if(line[0] != '#') {
                char* rest;
                const unsigned long hash = strtoul(line, &rest, 16);
                if(rest != line && (*rest == ' ' || *rest == '\0' || *rest == '\r')) {
                    hashes[count++] = (uint32_t)hash;
                }
            }
            line = end ? end + 1 : NULL;
        }
        free(text);
    }
    storage_file_free(file);
    return count;
}

bool subghz_ignore_add(Storage* storage, uint32_t hash, const char* label) {
    File* file = storage_file_alloc(storage);
    const bool existed = storage_file_exists(storage, SUBGHZ_IGNORE_PATH);
    bool saved = storage_file_open(file, SUBGHZ_IGNORE_PATH, FSAM_WRITE, FSOM_OPEN_APPEND);
    if(saved) {
        FuriString* line = furi_string_alloc();
        if(!existed) {
            furi_string_cat_str(
                line,
                "# Signals Sub-GHz Read does not show: a signal's hash, then what it was.\n"
                "# Added with Ignore on a signal; Read > Config > Forget ignored clears it.\n");
        }
        furi_string_cat_printf(line, "%08lX %s\n", (unsigned long)hash, label ? label : "");
        const size_t length = furi_string_size(line);
        saved = storage_file_write(file, furi_string_get_cstr(line), length) == length;
        furi_string_free(line);
    }
    storage_file_free(file);
    return saved;
}

bool subghz_ignore_clear(Storage* storage) {
    const FS_Error error = storage_common_remove(storage, SUBGHZ_IGNORE_PATH);
    return error == FSE_OK || error == FSE_NOT_EXIST;
}
