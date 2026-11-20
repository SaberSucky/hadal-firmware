/**
 * @file archive_search.h
 * @brief Finding files by name under a folder, its subfolders included.
 */
#pragma once

#include <furi.h>
#include <storage/storage.h>

#define ARCHIVE_SEARCH_MAX_RESULTS (64U)

/**
 * Every file and folder under root whose name holds `needle` (case not minding), folder by
 * folder, up to max results. Dot entries are passed over when skip_hidden. Stops early, with
 * what it found, after looking at limit entries.
 *
 * @return the number of paths written to results (each a new FuriString the caller frees)
 */
size_t archive_search(
    Storage* storage,
    const char* root,
    const char* needle,
    bool skip_hidden,
    FuriString** results,
    size_t max,
    uint32_t limit);

/** Whether `name` holds `needle`, case not minding (ASCII) */
bool archive_search_matches(const char* name, const char* needle);
