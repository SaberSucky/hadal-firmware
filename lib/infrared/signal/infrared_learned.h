/**
 * @file infrared_learned.h
 * @brief Universal remote codes that worked, so they are sent first next time.
 *
 * Kept as text, one code a line: "<library> <button> <key> <score>", e.g. "tv.ir Power 1A2B3C4D 4".
 * The key is what the code sends on the air (see infrared_signal_key.h), so the same code is
 * recognised in any library and however it was recorded. No dependencies, for the host tests.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Lines kept: the lowest scores make room */
#define INFRARED_LEARNED_MAX_LINES (200U)
/** A score stops growing here, so that a code that stops working can be overtaken */
#define INFRARED_LEARNED_MAX_SCORE (50U)
/** The longest library or button name */
#define INFRARED_LEARNED_NAME_SIZE (32U)

/** How strongly a use says the code worked */
typedef enum {
    InfraredLearnedSentAgain = 1, /**< sent again by hand from the pause screen */
    InfraredLearnedSaved = 3, /**< saved as a remote */
} InfraredLearnedWeight;

typedef struct {
    uint32_t key;
    uint32_t score;
} InfraredLearnedCode;

/**
 * The codes that worked for a library's button, best first
 *
 * @return the number written to codes (at most max)
 */
size_t infrared_learned_find(
    const char* text,
    const char* library,
    const char* button,
    InfraredLearnedCode* codes,
    size_t max);

/**
 * The text with a code's score raised (added if new). Lines that cannot be read are dropped.
 *
 * @return a new string (free() it), NULL if library or button cannot be stored
 */
char* infrared_learned_bump(
    const char* text,
    const char* library,
    const char* button,
    uint32_t key,
    InfraredLearnedWeight weight);

#ifdef __cplusplus
}
#endif
