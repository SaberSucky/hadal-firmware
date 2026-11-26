/**
 * @file ra_atlas_save.h
 * @brief A decoded frame as a .sub file: whether it is one the Sub-GHz app opens, and its name.
 *
 * The frame's text comes from subghz_protocol_decoder_base_serialize(), the same call the
 * Sub-GHz app saves a received signal with; the plugin writes it into /ext/subghz, where Sub-GHz
 * > Saved lists it and sends it. Nothing here touches the firmware, so the host tests build it
 * as it is.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RA_ATLAS_SAVE_FILETYPE "Flipper SubGhz Key File"
/** File names the atlas makes: "<protocol>_<MHz>" and "_<n>" for another of the same */
#define RA_ATLAS_SAVE_NAME_SIZE 48
/** Copies of one name before saving gives up, as a folder full of them means something is off */
#define RA_ATLAS_SAVE_COPIES_MAX 99

/**
 * True for a key file the Sub-GHz app opens: the Filetype line first, then a Version, a
 * Frequency above 0 and a Protocol with a name. Anything less (an empty capture, a protocol
 * without a file format) is nothing to save.
 */
bool ra_atlas_capture_is_key_file(const char* text);

/** The Frequency line's value in Hz, 0 when there is none */
uint32_t ra_atlas_capture_frequency(const char* text);

/**
 * "<protocol>_<MHz>" for @p copy 1, "<protocol>_<MHz>_<copy>" after: "Princeton_433.92",
 * "Princeton_433.92_2". Anything a file name should not hold (space, / and the like) becomes _,
 * and a protocol with no name is "Signal". Always NUL terminates, cut short to fit.
 *
 * @return the length written, without the NUL
 */
size_t ra_atlas_save_name(
    const char* protocol,
    uint32_t hz,
    unsigned copy,
    char* out,
    size_t out_size);
