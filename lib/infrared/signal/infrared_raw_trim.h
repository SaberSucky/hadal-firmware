/**
 * @file infrared_raw_trim.h
 * @brief How much of a raw signal a universal remote sends, and the silence before it.
 *
 * Internal to the infrared library (not part of the SDK).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Number of timings of a raw signal to send.
 *
 * Fast: at most two identical frames in a row. Turbo: one, and no NEC repeat frames. Both keep
 * three of a Sony-like code, which devices only accept repeated. Frames are separated by spaces
 * over 10 ms. The first frame is always sent whole, and the result always ends on a mark.
 *
 * @param[in] timings raw timings, starting with a mark
 * @param[in] size number of timings
 * @param[in] turbo true for turbo trimming, false for fast trimming
 * @returns the number of timings to send, between 1 and size (0 for an empty signal)
 */
size_t infrared_raw_trim_length(const uint32_t* timings, size_t size, bool turbo);

/**
 * @brief Silence before a raw signal in turbo mode, in microseconds.
 *
 * Five times the longest mark that is sent (an IR receiver needs about four times its longest
 * burst before it takes the next one at full gain), between 20 and 50 ms.
 *
 * @param[in] timings raw timings, starting with a mark
 * @param[in] length number of timings that are sent
 */
uint32_t infrared_raw_trim_turbo_gap_us(const uint32_t* timings, size_t length);

#ifdef __cplusplus
}
#endif
