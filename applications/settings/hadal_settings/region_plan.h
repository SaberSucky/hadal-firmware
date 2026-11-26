/**
 * @file region_plan.h
 * @brief The Frequency Atlas region's band plan, as frequencies for the Sub-GHz list.
 *
 * Read with the atlas's own parser (Remote Analyzer's ra_atlas_plan.c), so Settings and the atlas
 * agree on what a plan file holds. Each band's listen_hz is where the atlas parks the receiver to
 * check the band, which is what the hopper should visit there too.
 */
#pragma once

#include <storage/storage.h>

/** The longest region code, with its NUL: a file name in /ext/frequency_atlas without .txt */
#define HADAL_REGION_CODE_SIZE 16
/** As many bands as the atlas reads */
#define HADAL_REGION_BANDS_MAX 32

typedef struct {
    char name[HADAL_REGION_CODE_SIZE]; /**< "UK": the plan's Region, else the code in capitals */
    uint32_t hz[HADAL_REGION_BANDS_MAX]; /**< listen frequencies the radio can tune, no repeats */
    size_t count;
    size_t out_of_range; /**< bands left out: the radio cannot tune them (Extended Sub-GHz off) */
} HadalRegionPlan;

/**
 * Read /ext/frequency_atlas/<code>.txt into @p plan.
 * @return false for "off", a code that is no file name, or a file that is missing, too big or
 *         not a plan with a band in it: the same files the atlas refuses
 */
bool hadal_region_plan_load(Storage* storage, const char* code, HadalRegionPlan* plan);
