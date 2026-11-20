#include "update_resource_index.h"

#include <stdlib.h>
#include <string.h>

uint32_t update_resource_name_hash(const char* name) {
    while(name[0] == '.' && name[1] == '/')
        name += 2;
    while(name[0] == '/')
        name++;
    size_t length = strlen(name);
    while(length && name[length - 1] == '/')
        length--;

    uint32_t hash = 2166136261U; // FNV-1a
    for(size_t i = 0; i < length; i++) {
        hash ^= (uint8_t)name[i];
        hash *= 16777619U;
    }
    return hash;
}

void update_resource_index_init(UpdateResourceIndex* index) {
    index->entries = NULL;
    index->count = 0;
    index->capacity = 0;
}

bool update_resource_index_add(
    UpdateResourceIndex* index,
    const char* name,
    uint32_t size,
    const uint8_t md5[16]) {
    if(index->count == index->capacity) {
        const size_t capacity = index->capacity ? index->capacity * 2 : 256;
        UpdateResourceIndexEntry* grown =
            realloc(index->entries, capacity * sizeof(UpdateResourceIndexEntry));
        if(!grown) return false;
        index->entries = grown;
        index->capacity = capacity;
    }
    UpdateResourceIndexEntry* entry = &index->entries[index->count++];
    entry->name_hash = update_resource_name_hash(name);
    entry->size = size;
    if(md5) {
        memcpy(entry->md5, md5, UPDATE_RESOURCE_MD5_KEPT);
    } else {
        memset(entry->md5, 0, UPDATE_RESOURCE_MD5_KEPT);
    }
    return true;
}

static int index_compare(const void* a, const void* b) {
    const uint32_t ha = ((const UpdateResourceIndexEntry*)a)->name_hash;
    const uint32_t hb = ((const UpdateResourceIndexEntry*)b)->name_hash;
    return (ha > hb) - (ha < hb);
}

void update_resource_index_finish(UpdateResourceIndex* index) {
    if(index->count)
        qsort(index->entries, index->count, sizeof(UpdateResourceIndexEntry), index_compare);
}

const UpdateResourceIndexEntry*
    update_resource_index_find(const UpdateResourceIndex* index, const char* name) {
    const uint32_t hash = update_resource_name_hash(name);
    size_t low = 0, high = index->count;
    while(low < high) {
        const size_t middle = low + (high - low) / 2;
        const uint32_t value = index->entries[middle].name_hash;
        if(value == hash) return &index->entries[middle];
        if(value < hash) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    return NULL;
}

void update_resource_index_free(UpdateResourceIndex* index) {
    free(index->entries);
    update_resource_index_init(index);
}

bool update_resource_index_matches(
    const UpdateResourceIndexEntry* entry,
    uint32_t size,
    const uint8_t md5[16]) {
    return entry && entry->size != UPDATE_RESOURCE_DIRECTORY && entry->size == size &&
           memcmp(entry->md5, md5, UPDATE_RESOURCE_MD5_KEPT) == 0;
}
