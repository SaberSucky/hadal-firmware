#pragma once
/* declarations only: previews never touch storage (the code that does is never called) */
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef struct Storage Storage;
typedef struct File File;
typedef enum { FSAM_READ = (1 << 0), FSAM_WRITE = (1 << 1) } FS_AccessMode;
typedef enum {
    FSOM_OPEN_EXISTING = 1,
    FSOM_OPEN_ALWAYS = 2,
    FSOM_OPEN_APPEND = 4,
    FSOM_CREATE_NEW = 8,
    FSOM_CREATE_ALWAYS = 16,
} FS_OpenMode;
#define RECORD_STORAGE "storage"
File* storage_file_alloc(Storage* storage);
void storage_file_free(File* file);
bool storage_file_open(File* file, const char* path, FS_AccessMode access_mode, FS_OpenMode open_mode);
size_t storage_file_read(File* file, void* buff, size_t bytes_to_read);
bool storage_file_seek(File* file, uint32_t offset, bool from_start);
uint64_t storage_file_size(File* file);
