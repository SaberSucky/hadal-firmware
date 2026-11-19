/* Host storage backed by a directory: "/ext/..." and "/int/..." live under host_storage_root. */
#pragma once

#include <furi.h>
#include <storage/filesystem_api_defines.h>
#include <stdbool.h>
#include <stdint.h>

#define INT_PATH(path) "/int/" path

typedef struct Storage Storage;
typedef struct File File;

extern const char* host_storage_root;

File* storage_file_alloc(Storage* storage);
void storage_file_free(File* file);
bool storage_file_open(File* file, const char* path, FS_AccessMode access, FS_OpenMode mode);
bool storage_file_close(File* file);
uint16_t storage_file_read(File* file, void* buff, uint16_t bytes_to_read);
uint16_t storage_file_write(File* file, const void* buff, uint16_t bytes_to_write);
const char* storage_file_get_error_desc(File* file);
uint64_t storage_file_size(File* file);
bool storage_dir_open(File* file, const char* path);
bool storage_dir_close(File* file);
bool storage_dir_read(File* file, FileInfo* fileinfo, char* name, uint16_t name_length);
bool storage_common_exists(Storage* storage, const char* path);
bool storage_simply_remove(Storage* storage, const char* path);
