/**
 * @file update_resource_index.h
 * @brief What the new resources hold, by name: lets an update leave unchanged files alone.
 *
 * Built from the Manifest inside the new resources archive. A name is kept as a 32-bit hash,
 * with the file's size and the first 8 bytes of its MD5 (16 bytes an entry, so ~27 KB for the
 * ~1,700 entries a package has). A hash shared by two names is harmless: a file is only left
 * alone when the one on the card has the size and the MD5 its entry says, and a stale file that
 * a collision keeps is only clutter.
 *
 * No dependencies, for the host tests.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UPDATE_RESOURCE_DIRECTORY (UINT32_MAX) /**< the size an entry for a folder has */
#define UPDATE_RESOURCE_MD5_KEPT  (8U)

typedef struct {
    uint32_t name_hash;
    uint32_t size; /**< UPDATE_RESOURCE_DIRECTORY for a folder */
    uint8_t md5[UPDATE_RESOURCE_MD5_KEPT];
} UpdateResourceIndexEntry;

typedef struct {
    UpdateResourceIndexEntry* entries;
    size_t count;
    size_t capacity;
} UpdateResourceIndex;

/** Hash of a resource name; leading "./" or "/" and a trailing "/" do not count */
uint32_t update_resource_name_hash(const char* name);

void update_resource_index_init(UpdateResourceIndex* index);
/** @return false when out of memory (the index is then unusable: free it) */
bool update_resource_index_add(
    UpdateResourceIndex* index,
    const char* name,
    uint32_t size,
    const uint8_t md5[16]);
/** Sort it, after the last add and before the first find */
void update_resource_index_finish(UpdateResourceIndex* index);
const UpdateResourceIndexEntry*
    update_resource_index_find(const UpdateResourceIndex* index, const char* name);
void update_resource_index_free(UpdateResourceIndex* index);

/** Whether a file on the card (its size and full MD5) is the entry's file */
bool update_resource_index_matches(
    const UpdateResourceIndexEntry* entry,
    uint32_t size,
    const uint8_t md5[16]);

#ifdef __cplusplus
}
#endif
