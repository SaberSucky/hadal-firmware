#include "infrared_signal_key.h"

// Keys of what signals send on the air, for leaving out codes a universal remote already sent.
// Only the decoders as dependency, so that the host tests build it as it is.

#define KEY_FNV64_OFFSET (0xcbf29ce484222325ULL)
#define KEY_FNV64_PRIME  (0x100000001b3ULL)

#define KEY_KIND_NEC32 (1U)
#define KEY_KIND_NEC42 (2U)
#define KEY_KIND_RAW   (3U)
#define KEY_KIND_OTHER (16U)

// A space longer than this separates frames
#define KEY_RAW_FRAME_GAP_US (10000U)
// Raw captures that are not a known protocol: timings rounded to this before comparing
#define KEY_RAW_STEP_US      (100U)

static uint64_t key_add(uint64_t hash, uint32_t value) {
    for(size_t i = 0; i < sizeof(value); i++) {
        hash ^= (value >> (8 * i)) & 0xFF;
        hash *= KEY_FNV64_PRIME;
    }
    return hash;
}

static uint32_t key_mask(uint8_t bits) {
    return bits >= 32 ? 0xFFFFFFFFU : ((1U << bits) - 1U);
}

uint64_t infrared_signal_key_message(const InfraredMessage* message) {
    uint64_t key = KEY_FNV64_OFFSET;
    const uint32_t address = message->address;
    const uint32_t command = message->command;

    // The NEC family is keyed by the bits its encoder sends, so that the two ways of writing
    // one code (NEC / NECext, NEC42 / NEC42ext) get one key
    switch(message->protocol) {
    case InfraredProtocolNEC: {
        const uint32_t a = address & 0xFF, c = command & 0xFF;
        key = key_add(key, KEY_KIND_NEC32);
        key = key_add(key, a | ((~a & 0xFF) << 8) | (c << 16) | ((~c & 0xFF) << 24));
        break;
    }
    case InfraredProtocolNECext:
        key = key_add(key, KEY_KIND_NEC32);
        key = key_add(key, (address & 0xFFFF) | ((command & 0xFFFF) << 16));
        break;
    case InfraredProtocolNEC42:
        key = key_add(key, KEY_KIND_NEC42);
        key = key_add(
            key, (address & 0x1FFF) | ((~address & 0x1FFF) << 13) | ((command & 0x3F) << 26));
        key = key_add(key, ((command & 0xC0) >> 6) | ((~command & 0xFF) << 2));
        break;
    case InfraredProtocolNEC42ext:
        key = key_add(key, KEY_KIND_NEC42);
        key = key_add(key, (address & 0x3FFFFFF) | ((command & 0x3F) << 26));
        key = key_add(key, (command & 0xFFC0) >> 6);
        break;
    case InfraredProtocolRC5:
    case InfraredProtocolRC5X:
        // RC5X is RC5 with the field bit cleared: the encoders send 6 command bits either way
        key = key_add(key, KEY_KIND_OTHER + (uint32_t)message->protocol);
        key = key_add(key, address & 0x1F);
        key = key_add(key, command & 0x3F);
        break;
    default:
        // Only the bits the protocol has go on the air
        key = key_add(key, KEY_KIND_OTHER + (uint32_t)message->protocol);
        key = key_add(
            key, address & key_mask(infrared_get_protocol_address_length(message->protocol)));
        key = key_add(
            key, command & key_mask(infrared_get_protocol_command_length(message->protocol)));
        break;
    }
    return key;
}

static bool key_messages_equal(const InfraredMessage* a, const InfraredMessage* b) {
    return a->protocol == b->protocol && a->address == b->address && a->command == b->command;
}

// A frame this short after a decoded one is a held-button repeat frame (NEC: 3 timings,
// Samsung: 5), not a code of its own
#define KEY_REPEAT_FRAME_MAX_TIMINGS (8U)

// The code one frame decodes to, with a fresh decoder: every message it gives must be that one
// code, and at least one must be a new press (not a repeat)
static bool key_decode_frame(
    const uint32_t* frame,
    size_t size,
    InfraredDecoderHandler* decoder,
    InfraredMessage* message) {
    bool have = false, consistent = true;

    infrared_reset_decoder(decoder);
    bool level = true;
    for(size_t i = 0; i <= size; i++) {
        // After the last timing, take what the decoders still hold (as on a receive timeout)
        const InfraredMessage* got = (i < size) ? infrared_decode(decoder, level, frame[i]) :
                                                  infrared_check_decoder_ready(decoder);
        level = !level;
        if(!got) continue;
        if(!have) {
            if(got->repeat) {
                consistent = false;
            } else {
                *message = *got;
                have = true;
            }
        } else if(!key_messages_equal(message, got)) {
            consistent = false;
        }
    }
    infrared_reset_decoder(decoder);
    return have && consistent;
}

// Captures from other receivers often sit just outside the decoders' tolerances (a 9.25 ms
// leader, 680 us marks). Any receiver that takes such a capture also takes the clean code, so
// the two most common pulse-distance shapes are read here with a looser eye. The frame must be
// exactly one code long, so that a longer code with the same leader is never read as one.
typedef struct {
    uint32_t lead_mark;
    uint32_t lead_space;
} KeyPulseShape;

static bool key_within_percent(uint32_t value, uint32_t target, uint32_t percent) {
    const uint32_t tolerance = target * percent / 100;
    return value + tolerance >= target && value <= target + tolerance;
}

static bool
    key_pulse_distance_32(const uint32_t* frame, size_t size, KeyPulseShape shape, uint32_t* bits) {
    if(size != 2 + 2 * 32 + 1) return false;
    if(!key_within_percent(frame[0], shape.lead_mark, 20)) return false;
    if(!key_within_percent(frame[1], shape.lead_space, 25)) return false;
    uint32_t value = 0;
    for(size_t i = 0; i < 32; i++) {
        const uint32_t mark = frame[2 + 2 * i], space = frame[3 + 2 * i];
        if(mark < 300 || mark > 900 || space < 300 || space > 2000) return false;
        if(space > 1000) value |= 1UL << i;
    }
    if(frame[size - 1] < 300 || frame[size - 1] > 900) return false;
    *bits = value;
    return true;
}

static bool key_decode_frame_loose(const uint32_t* frame, size_t size, InfraredMessage* message) {
    uint32_t bits;
    if(key_pulse_distance_32(frame, size, (KeyPulseShape){9000, 4500}, &bits)) {
        *message = (InfraredMessage){
            .protocol = InfraredProtocolNECext,
            .address = bits & 0xFFFF,
            .command = bits >> 16,
        };
        return true;
    }
    if(key_pulse_distance_32(frame, size, (KeyPulseShape){4500, 4500}, &bits)) {
        const uint32_t a = bits & 0xFF, a2 = (bits >> 8) & 0xFF;
        const uint32_t c = (bits >> 16) & 0xFF, c_inverse = bits >> 24;
        if(a == a2 && c == (~c_inverse & 0xFF)) {
            *message = (InfraredMessage){
                .protocol = InfraredProtocolSamsung32, .address = a, .command = c};
            return true;
        }
    }
    return false;
}

// Whether the capture is one code: its frames (split at spaces over 10 ms) each decode to that
// code, apart from short repeat frames after it
static bool key_decode(
    const uint32_t* timings,
    size_t size,
    InfraredDecoderHandler* decoder,
    InfraredMessage* message) {
    bool have = false;
    size_t start = 0;

    for(size_t i = 0; i < size; i++) {
        const bool gap = (i % 2 == 1) && (timings[i] > KEY_RAW_FRAME_GAP_US);
        if(!gap && i + 1 < size) continue;
        // The frame is [start, end): up to the gap, or to the end of the capture
        const size_t end = gap ? i : size;
        if(end > start) {
            InfraredMessage frame_message;
            if(key_decode_frame(timings + start, end - start, decoder, &frame_message) ||
               key_decode_frame_loose(timings + start, end - start, &frame_message)) {
                if(!have) {
                    *message = frame_message;
                    have = true;
                } else if(!key_messages_equal(message, &frame_message)) {
                    return false;
                }
            } else if(!have || end - start > KEY_REPEAT_FRAME_MAX_TIMINGS) {
                return false;
            }
        }
        start = i + 1;
    }
    return have;
}

uint64_t infrared_signal_key_raw(
    const uint32_t* timings,
    size_t size,
    uint32_t frequency,
    InfraredDecoderHandler* decoder) {
    // A capture of a known protocol is that code
    InfraredMessage message;
    if(decoder && size && key_decode(timings, size, decoder, &message)) {
        return infrared_signal_key_message(&message);
    }

    uint64_t key = key_add(KEY_FNV64_OFFSET, KEY_KIND_RAW);
    key = key_add(key, (frequency + 500) / 1000);
    for(size_t i = 0; i < size; i++) {
        key = key_add(key, (timings[i] + KEY_RAW_STEP_US / 2) / KEY_RAW_STEP_US);
    }
    return key;
}
