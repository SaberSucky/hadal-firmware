/**
 * @file ra_atlas_plan.h
 * @brief A region's band plan, parsed from its text file.
 *
 * The file is plain text, one setting per line in the style of subghz/assets/setting_user:
 *
 *     Filetype: Flipper Frequency Atlas
 *     Version: 1
 *     Region: UK
 *     Name: UK - Ofcom IR 2030
 *     Source: Ofcom IR 2030, published 23 Oct 2026
 *     Note: Reference only, not legal advice.
 *     # start_hz | end_hz | listen_hz | label | typical use | limit
 *     Band: 433050000|434790000|433920000|SRD 433|Remotes, sensors|10 mW e.r.p.
 *
 * Lines starting with # are comments; unknown keys are skipped, so a later version can add
 * some. listen_hz may be empty for the middle of the band. A Band line that does not parse is
 * counted and skipped, never fatal; a file without the Filetype and Version lines, or with no
 * band that parses, is not a plan at all (ra_atlas_plan_is_valid()).
 *
 * The text is parsed in place: every string the plan hands out points into the caller's
 * buffer, so it must outlive the plan. Nothing here allocates, and nothing here depends on the
 * firmware, so the host tests build it as it is.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RA_ATLAS_FILETYPE  "Flipper Frequency Atlas"
#define RA_ATLAS_VERSION   1
/** More than any regulator's sub-GHz table within the radio's reach. */
#define RA_ATLAS_BANDS_MAX 32
/** A plan is a few KB; a file far past that is not one. */
#define RA_ATLAS_FILE_MAX  (16 * 1024)

typedef struct {
    uint32_t start; /**< Hz */
    uint32_t end; /**< Hz, above start */
    uint32_t listen; /**< Hz, where the receiver parks: within the band */
    const char* label; /**< never empty */
    const char* use; /**< may be empty */
    const char* limit; /**< may be empty */
} RaAtlasBand;

typedef struct {
    bool filetype_ok;
    bool version_ok;
    const char* region; /**< short code for the header, "" when the file has none */
    const char* name;
    const char* source;
    const char* note;
    RaAtlasBand bands[RA_ATLAS_BANDS_MAX];
    size_t count;
    size_t bad_lines; /**< Band lines skipped, and lines that are not a setting at all */
} RaAtlasPlan;

/**
 * Parse @p text, @p size bytes, which need not be NUL terminated but must be writable: line
 * ends and field separators are overwritten with NULs, and the plan points into it.
 *
 * @param text one byte past @p size must be writable too, for the last line's terminator
 */
void ra_atlas_plan_parse(RaAtlasPlan* plan, char* text, size_t size);

/** True when the file is a plan with at least one band to show. */
bool ra_atlas_plan_is_valid(const RaAtlasPlan* plan);

/**
 * True for a region code that is safe to use as a file name: 1 to 15 characters out of
 * a-z, 0-9, _ and -. Upper case is refused rather than folded, so one region has one file.
 */
bool ra_atlas_region_code_is_valid(const char* code);

/**
 * @p hz as MHz to the kHz, trailing zeros dropped: 433920000 is "433.92", 446000000 "446".
 * Always NUL terminates. @return the length written, without the NUL
 */
size_t ra_atlas_format_mhz(uint32_t hz, char* out, size_t out_size);

/**
 * Copy @p text into @p out (@p out_size bytes), breaking lines at spaces so none is longer
 * than @p columns characters; a word longer than that is cut. Existing line breaks are kept.
 * Always NUL terminates, cutting the text short when @p out is too small.
 *
 * @return the length written, without the NUL
 */
size_t ra_atlas_wrap(const char* text, size_t columns, char* out, size_t out_size);
