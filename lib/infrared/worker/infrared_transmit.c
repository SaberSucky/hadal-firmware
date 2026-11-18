#include "infrared.h"
#include "infrared_transmit.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <furi.h>
#include <furi_hal_infrared.h>

static uint32_t infrared_tx_number_of_transmissions = 0;
static uint32_t infrared_tx_raw_timings_index = 0;
static uint32_t infrared_tx_raw_timings_number = 0;
static uint32_t infrared_tx_raw_start_from_mark = 0;
static bool infrared_tx_raw_add_silence = false;
static uint32_t infrared_tx_raw_silence_us = INFRARED_RAW_TX_TIMING_DELAY_US;
static uint32_t infrared_tx_leading_silence_us = INFRARED_TX_LEADING_SILENCE_DEFAULT;
static bool infrared_tx_first_timing = false;

// Never hand a zero-length silence to the HAL
#define INFRARED_TX_MIN_LEADING_SILENCE_US (1000U)

FuriHalInfraredTxGetDataState
    infrared_get_raw_data_callback(void* context, uint32_t* duration, bool* level) {
    furi_assert(duration);
    furi_assert(level);
    furi_assert(context);

    FuriHalInfraredTxGetDataState state = FuriHalInfraredTxGetDataStateOk;
    const uint32_t* timings = context;

    if(infrared_tx_raw_add_silence && (infrared_tx_raw_timings_index == 0)) {
        infrared_tx_raw_add_silence = false;
        *level = false;
        *duration = infrared_tx_raw_silence_us;
    } else {
        *level = infrared_tx_raw_start_from_mark ^ (infrared_tx_raw_timings_index % 2);
        *duration = timings[infrared_tx_raw_timings_index++];
    }

    if(infrared_tx_raw_timings_number == infrared_tx_raw_timings_index) {
        state = FuriHalInfraredTxGetDataStateLastDone;
    }

    return state;
}

void infrared_send_raw_ex(
    const uint32_t timings[],
    uint32_t timings_cnt,
    bool start_from_mark,
    uint32_t frequency,
    float duty_cycle,
    uint32_t leading_silence_us) {
    furi_check(timings);

    infrared_tx_raw_silence_us = (leading_silence_us == INFRARED_TX_LEADING_SILENCE_DEFAULT) ?
                                     INFRARED_RAW_TX_TIMING_DELAY_US :
                                     MAX(leading_silence_us, INFRARED_TX_MIN_LEADING_SILENCE_US);
    infrared_tx_raw_start_from_mark = start_from_mark;
    infrared_tx_raw_timings_index = 0;
    infrared_tx_raw_timings_number = timings_cnt;
    infrared_tx_raw_add_silence = start_from_mark;
    furi_hal_infrared_async_tx_set_data_isr_callback(
        infrared_get_raw_data_callback, (void*)timings);
    furi_hal_infrared_async_tx_start(frequency, duty_cycle);
    furi_hal_infrared_async_tx_wait_termination();

    furi_check(!furi_hal_infrared_is_busy());
}

void infrared_send_raw_ext(
    const uint32_t timings[],
    uint32_t timings_cnt,
    bool start_from_mark,
    uint32_t frequency,
    float duty_cycle) {
    infrared_send_raw_ex(
        timings,
        timings_cnt,
        start_from_mark,
        frequency,
        duty_cycle,
        INFRARED_TX_LEADING_SILENCE_DEFAULT);
}

void infrared_send_raw(const uint32_t timings[], uint32_t timings_cnt, bool start_from_mark) {
    infrared_send_raw_ext(
        timings,
        timings_cnt,
        start_from_mark,
        INFRARED_COMMON_CARRIER_FREQUENCY,
        INFRARED_COMMON_DUTY_CYCLE);
}

FuriHalInfraredTxGetDataState
    infrared_get_data_callback(void* context, uint32_t* duration, bool* level) {
    FuriHalInfraredTxGetDataState state;
    InfraredEncoderHandler* handler = context;
    InfraredStatus status = InfraredStatusError;

    if(infrared_tx_number_of_transmissions > 0) {
        status = infrared_encode(handler, duration, level);
    }

    if(status == InfraredStatusError) {
        state = FuriHalInfraredTxGetDataStateLastDone;
        *duration = 0;
        *level = 0;
    } else if(status == InfraredStatusOk) {
        state = FuriHalInfraredTxGetDataStateOk;
        // Encoders start every message with the protocol's silence: shorten only that one
        if(infrared_tx_first_timing) {
            infrared_tx_first_timing = false;
            if(!*level && infrared_tx_leading_silence_us != INFRARED_TX_LEADING_SILENCE_DEFAULT) {
                *duration = MIN(*duration, infrared_tx_leading_silence_us);
            }
        }
    } else if(status == InfraredStatusDone) {
        if(--infrared_tx_number_of_transmissions == 0) {
            state = FuriHalInfraredTxGetDataStateLastDone;
        } else {
            state = FuriHalInfraredTxGetDataStateDone;
        }
    } else {
        furi_crash();
    }

    return state;
}

void infrared_send_ex(const InfraredMessage* message, int times, uint32_t leading_silence_us) {
    furi_check(message);
    furi_check(times);
    furi_check(infrared_is_protocol_valid(message->protocol));

    infrared_tx_leading_silence_us =
        (leading_silence_us == INFRARED_TX_LEADING_SILENCE_DEFAULT) ?
            INFRARED_TX_LEADING_SILENCE_DEFAULT :
            MAX(leading_silence_us, INFRARED_TX_MIN_LEADING_SILENCE_US);
    infrared_tx_first_timing = true;

    InfraredEncoderHandler* handler = infrared_alloc_encoder();
    infrared_reset_encoder(handler, message);
    infrared_tx_number_of_transmissions =
        MAX((int)infrared_get_protocol_min_repeat_count(message->protocol), times);

    uint32_t frequency = infrared_get_protocol_frequency(message->protocol);
    float duty_cycle = infrared_get_protocol_duty_cycle(message->protocol);

    furi_hal_infrared_async_tx_set_data_isr_callback(infrared_get_data_callback, handler);
    furi_hal_infrared_async_tx_start(frequency, duty_cycle);
    furi_hal_infrared_async_tx_wait_termination();

    infrared_free_encoder(handler);

    furi_check(!furi_hal_infrared_is_busy());
}

void infrared_send(const InfraredMessage* message, int times) {
    infrared_send_ex(message, times, INFRARED_TX_LEADING_SILENCE_DEFAULT);
}
