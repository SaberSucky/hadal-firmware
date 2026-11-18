#pragma once

#include <furi_hal_infrared.h>
#include <infrared.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Send message over INFRARED.
 *
 * \param[in]   message     - message to send.
 * \param[in]   times       - number of times message should be sent.
 */
void infrared_send(const InfraredMessage* message, int times);

/**
 * Send raw data through infrared port.
 *
 * \param[in]   timings - array of timings to send.
 * \param[in]   timings_cnt - timings array size.
 * \param[in]   start_from_mark - true if timings starts from mark,
 *              otherwise from space
 */
void infrared_send_raw(const uint32_t timings[], uint32_t timings_cnt, bool start_from_mark);

/**
 * Send raw data through infrared port, with additional settings.
 *
 * \param[in]   timings - array of timings to send.
 * \param[in]   timings_cnt - timings array size.
 * \param[in]   start_from_mark - true if timings starts from mark,
 *              otherwise from space
 * \param[in]   duty_cycle - duty cycle to generate on PWM
 * \param[in]   frequency - frequency to generate on PWM
 */
void infrared_send_raw_ext(
    const uint32_t timings[],
    uint32_t timings_cnt,
    bool start_from_mark,
    uint32_t frequency,
    float duty_cycle);

/** Keep the protocol's own leading silence (same behaviour as infrared_send()) */
#define INFRARED_TX_LEADING_SILENCE_DEFAULT (UINT32_MAX)

/**
 * Send message like infrared_send(), with a custom leading silence.
 *
 * Every transmission starts with a silence (the protocol's repeat period, up to
 * ~150 ms). When sending many different codes in a row, a shorter gap is enough.
 *
 * \param[in]   message - message to send
 * \param[in]   times - number of times to send (at least the protocol's minimum)
 * \param[in]   leading_silence_us - max silence before the first frame, in us, or
 *              INFRARED_TX_LEADING_SILENCE_DEFAULT
 */
void infrared_send_ex(const InfraredMessage* message, int times, uint32_t leading_silence_us);

/**
 * Send raw data like infrared_send_raw_ext(), with a custom leading silence.
 *
 * \param[in]   leading_silence_us - silence before the first mark (if start_from_mark),
 *              in us, or INFRARED_TX_LEADING_SILENCE_DEFAULT
 */
void infrared_send_raw_ex(
    const uint32_t timings[],
    uint32_t timings_cnt,
    bool start_from_mark,
    uint32_t frequency,
    float duty_cycle,
    uint32_t leading_silence_us);

#ifdef __cplusplus
}
#endif
