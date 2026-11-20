/**
 * @file subghz_signal_hash.h
 * @brief What a received signal is, as a number: a hash of its saved form (the .sub file's lines)
 * without the lines that change from one reception of the same signal to the next (frequency,
 * preset, timing, position, RSSI). Two receptions of one button of one remote hash the same; a
 * rolling code hashes differently each press. No dependencies, for the host tests.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t hash;
    char key[24];
    uint8_t key_length;
    bool in_key; /**< still before the line's ':' */
    bool skip; /**< this line is not hashed */
    bool overflow; /**< the key was too long to hold: hashed as it came */
} SubGhzSignalHash;

void subghz_signal_hash_init(SubGhzSignalHash* state);
void subghz_signal_hash_feed(SubGhzSignalHash* state, const char* data, size_t length);
uint32_t subghz_signal_hash_finish(SubGhzSignalHash* state);

/** The whole text at once */
uint32_t subghz_signal_hash(const char* text, size_t length);

#ifdef __cplusplus
}
#endif
