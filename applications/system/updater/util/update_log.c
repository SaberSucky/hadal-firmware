#include "update_log.h"

#include <furi.h>
#include <storage/storage.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define LINE_SIZE 192

void update_log(const char* format, ...) {
    char line[LINE_SIZE];
    // The tick says how long after the kernel started: restarts show as the time going back
    int prefix = snprintf(line, sizeof(line), "[%8lu] ", (unsigned long)furi_get_tick());
    if(prefix < 0) return;

    va_list args;
    va_start(args, format);
    int body = vsnprintf(line + prefix, sizeof(line) - (size_t)prefix - 2, format, args);
    va_end(args);
    if(body < 0) return;

    size_t length = strlen(line);
    line[length++] = '\n';

    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    // Mounted or not, never let the log hold up or break the update
    if(storage_file_open(file, UPDATE_LOG_PATH, FSAM_WRITE, FSOM_OPEN_APPEND)) {
        if(storage_file_size(file) > UPDATE_LOG_MAX_BYTES) {
            storage_file_close(file);
            storage_file_open(file, UPDATE_LOG_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS);
        }
        storage_file_write(file, line, length);
        storage_file_close(file);
    }
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
}
