#include "subghz_ignore.h"

#include <stdlib.h>
#include <string.h>

#define IGNORE_FILE_MAX (4096U)

void subghz_ignore_remember(uint32_t* hashes, size_t* count, size_t max, uint32_t hash) {
    for(size_t i = 0; i < *count; i++) {
        if(hashes[i] == hash) return;
    }
    if(!max) return;
    if(*count == max) {
        // full: the oldest makes room
        memmove(hashes, hashes + 1, (max - 1) * sizeof(uint32_t));
        (*count)--;
    }
    hashes[(*count)++] = hash;
}

size_t subghz_ignore_load(Storage* storage, uint32_t* hashes, size_t max) {
    File* file = storage_file_alloc(storage);
    size_t count = 0;
    if(storage_file_open(file, SUBGHZ_IGNORE_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        // the end of the file: the newest lines (the file only grows, by appending)
        const uint64_t size = storage_file_size(file);
        const bool tail = size > IGNORE_FILE_MAX;
        if(tail) storage_file_seek(file, (uint32_t)(size - IGNORE_FILE_MAX), true);
        char* text = malloc(IGNORE_FILE_MAX + 1);
        const size_t read = storage_file_read(file, text, IGNORE_FILE_MAX);
        text[read] = '\0';
        // started mid-line: that part line is skipped
        char* line = tail ? strchr(text, '\n') : text;
        if(line && tail) line++;
        while(line && *line) {
            char* end = strchr(line, '\n');
            if(end) *end = '\0';
            if(line[0] != '#') {
                char* rest;
                const unsigned long hash = strtoul(line, &rest, 16);
                if(rest != line && (*rest == ' ' || *rest == '\0' || *rest == '\r')) {
                    subghz_ignore_remember(hashes, &count, max, (uint32_t)hash);
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
