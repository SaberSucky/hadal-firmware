/**
 * @file ra_atlas_view.h
 * @brief The band list: one row per band, what it is and whether it is busy.
 *
 * Up/Down pick a band, OK opens it in full, Right listens to it alone, Left goes back to
 * going round them all, and holding OK changes the region.
 */
#pragma once

#include "ra_atlas_scan.h"

#include <gui/view.h>

typedef struct RaAtlasView RaAtlasView;

typedef enum {
    RaAtlasViewEventDetails, /**< OK on the selected band */
    RaAtlasViewEventHold, /**< Right: listen to the selected band alone */
    RaAtlasViewEventRelease, /**< Left: back to scanning */
    RaAtlasViewEventRegion, /**< OK held: pick another region */
} RaAtlasViewEvent;

typedef void (*RaAtlasViewCallback)(RaAtlasViewEvent event, size_t index, void* context);

/** What one row shows. The label is borrowed and must outlive the row. */
typedef struct {
    const char* label;
    uint32_t start;
    uint32_t end;
    uint8_t status; /**< RaAtlasStatus */
    uint32_t ago; /**< seconds, for RaAtlasStatusSeen */
    bool listening; /**< the receiver is on this band now */
    char protocol[RA_ATLAS_PROTOCOL_SIZE]; /**< the last decode on it, "" for none */
} RaAtlasRow;

RaAtlasView* ra_atlas_view_alloc(void);

void ra_atlas_view_free(RaAtlasView* instance);

View* ra_atlas_view_get_view(RaAtlasView* instance);

void ra_atlas_view_set_callback(RaAtlasView* instance, RaAtlasViewCallback callback, void* context);

/** New plan: @p count rows, selection back at the top. @p region is borrowed. */
void ra_atlas_view_reset(RaAtlasView* instance, const char* region, size_t count);

/** Refresh every row from the scan, and whether the receiver is held on one band. */
void ra_atlas_view_update(
    RaAtlasView* instance,
    const RaAtlasRow* rows,
    size_t count,
    bool held,
    bool signal);
