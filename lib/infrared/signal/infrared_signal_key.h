/**
 * @file infrared_signal_key.h
 * @brief A key of what a signal sends on the air: equal keys, same code.
 *
 * Internal to the infrared library (not part of the SDK).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "infrared.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Key of a parsed signal.
 *
 * Messages that send the same bits get the same key: NEC and NECext (and NEC42 and NEC42ext)
 * written for one code, and bits beyond what the protocol sends (RC5X's seventh command bit,
 * for one) are ignored.
 */
uint64_t infrared_signal_key_message(const InfraredMessage* message);

/**
 * @brief Key of a raw signal.
 *
 * A capture of a known protocol, where every frame (frames are separated by spaces over 10 ms)
 * decodes on its own to one and the same message (short repeat frames after it allowed), gets
 * that message's key, so it matches the parsed signal it was recorded from. NEC and Samsung32
 * frames are also read with wider tolerances than the decoders', as captures often have them.
 * Anything else is keyed by its carrier (to 1 kHz) and its timings (to 0.1 ms).
 *
 * @param[in] timings raw timings, starting with a mark
 * @param[in] size number of timings
 * @param[in] frequency carrier frequency in Hz
 * @param[in,out] decoder a decoder to use (reset before and after), or NULL to key by timings only
 */
uint64_t infrared_signal_key_raw(
    const uint32_t* timings,
    size_t size,
    uint32_t frequency,
    InfraredDecoderHandler* decoder);

#ifdef __cplusplus
}
#endif
