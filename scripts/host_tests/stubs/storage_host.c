#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <storage/storage.h>

const char* host_storage_root = ".";
int host_log_enabled = 0;

struct File {
    FILE* file;
    DIR* dir;
};

static void host_path(char* out, size_t size, const char* path) {
    snprintf(out, size, "%s%s", host_storage_root, path);
}

File* storage_file_alloc(Storage* storage) {
    (void)storage;
    return calloc(1, sizeof(File));
}

void storage_file_free(File* file) {
    if(file->file) fclose(file->file);
    if(file->dir) closedir(file->dir);
    free(file);
}

bool storage_file_open(File* file, const char* path, FS_AccessMode access, FS_OpenMode mode) {
    char p[512];
    host_path(p, sizeof(p), path);
    const char* m = "rb";
    if(access & FSAM_WRITE) {
        m = (mode == FSOM_CREATE_ALWAYS) ? "wb" : (mode == FSOM_OPEN_APPEND) ? "ab" : "r+b";
    }
    file->file = fopen(p, m);
    return file->file != NULL;
}

bool storage_file_close(File* file) {
    if(file->file) fclose(file->file);
    file->file = NULL;
    return true;
}

uint16_t storage_file_read(File* file, void* buff, uint16_t bytes_to_read) {
    return (uint16_t)fread(buff, 1, bytes_to_read, file->file);
}

uint16_t storage_file_write(File* file, const void* buff, uint16_t bytes_to_write) {
    return (uint16_t)fwrite(buff, 1, bytes_to_write, file->file);
}

const char* storage_file_get_error_desc(File* file) {
    (void)file;
    return "host storage error";
}

uint64_t storage_file_size(File* file) {
    long pos = ftell(file->file);
    fseek(file->file, 0, SEEK_END);
    long size = ftell(file->file);
    fseek(file->file, pos, SEEK_SET);
    return (uint64_t)size;
}

bool storage_file_seek(File* file, uint32_t offset, bool from_start) {
    if(!file->file) return false;
    return fseek(file->file, (long)offset, from_start ? SEEK_SET : SEEK_CUR) == 0;
}

uint64_t storage_file_tell(File* file) {
    return file->file ? (uint64_t)ftell(file->file) : 0;
}

bool storage_file_is_open(File* file) {
    return file->file != NULL;
}

bool storage_dir_open(File* file, const char* path) {
    char p[512];
    host_path(p, sizeof(p), path);
    file->dir = opendir(p);
    return file->dir != NULL;
}

bool storage_dir_close(File* file) {
    if(file->dir) closedir(file->dir);
    file->dir = NULL;
    return true;
}

bool file_info_is_dir(const FileInfo* info) {
    return info->flags & FSF_DIRECTORY;
}

bool storage_dir_read(File* file, FileInfo* info, char* name, uint16_t name_length) {
    struct dirent* entry;
    while((entry = readdir(file->dir))) {
        if(!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        // FAT-style truncation: the real API copies at most name_length - 1 characters
        snprintf(name, name_length, "%s", entry->d_name);
        info->flags = entry->d_type == DT_DIR ? FSF_DIRECTORY : 0;
        info->size = 0;
        return true;
    }
    return false;
}

bool storage_common_exists(Storage* storage, const char* path) {
    (void)storage;
    char p[512];
    host_path(p, sizeof(p), path);
    struct stat st;
    return stat(p, &st) == 0;
}

bool storage_simply_remove(Storage* storage, const char* path) {
    (void)storage;
    char p[512];
    host_path(p, sizeof(p), path);
    return remove(p) == 0;
}
