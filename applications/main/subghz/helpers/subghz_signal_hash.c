#include "subghz_signal_hash.h"

#include <string.h>

#define FNV_OFFSET (2166136261U)
#define FNV_PRIME  (16777619U)

// Lines that differ between receptions of the same signal
static const char* const skipped_keys[] = {
    "Frequency",
    "Preset",
    "Custom_preset_module",
    "Custom_preset_data",
    "TE",
    "Guard_time",
    "Repeat",
    "RSSI",
    "Lat",
    "Lon",
    "Latitute",
    "Latitude",
    "Longitude",
};

static void hash_byte(SubGhzSignalHash* state, char c) {
    state->hash = (state->hash ^ (uint8_t)c) * FNV_PRIME;
}

static void line_start(SubGhzSignalHash* state) {
    state->key_length = 0;
    state->in_key = true;
    state->skip = false;
    state->overflow = false;
}

void subghz_signal_hash_init(SubGhzSignalHash* state) {
    memset(state, 0, sizeof(*state));
    state->hash = FNV_OFFSET;
    line_start(state);
}

// The key is complete: skip the line, or hash what was held back
static void key_done(SubGhzSignalHash* state) {
    state->in_key = false;
    if(state->overflow) return;
    for(size_t i = 0; i < sizeof(skipped_keys) / sizeof(skipped_keys[0]); i++) {
        if(strlen(skipped_keys[i]) == state->key_length &&
           memcmp(skipped_keys[i], state->key, state->key_length) == 0) {
            state->skip = true;
            return;
        }
    }
    for(uint8_t i = 0; i < state->key_length; i++) {
        hash_byte(state, state->key[i]);
    }
}

void subghz_signal_hash_feed(SubGhzSignalHash* state, const char* data, size_t length) {
    for(size_t i = 0; i < length; i++) {
        const char c = data[i];
        if(c == '\r') continue;
        if(state->in_key) {
            if(c == ':' || c == '\n') {
                key_done(state);
            } else if(state->overflow) {
                hash_byte(state, c);
                continue;
            } else if(state->key_length < sizeof(state->key)) {
                state->key[state->key_length++] = c;
                continue;
            } else {
                // too long to be a key worth skipping: hash it all
                state->overflow = true;
                for(uint8_t k = 0; k < state->key_length; k++) {
                    hash_byte(state, state->key[k]);
                }
                hash_byte(state, c);
                continue;
            }
        }
        if(!state->skip) hash_byte(state, c);
        if(c == '\n') line_start(state);
    }
}

uint32_t subghz_signal_hash_finish(SubGhzSignalHash* state) {
    // a last line without its newline
    if(state->in_key && state->key_length) key_done(state);
    return state->hash;
}

uint32_t subghz_signal_hash(const char* text, size_t length) {
    SubGhzSignalHash state;
    subghz_signal_hash_init(&state);
    subghz_signal_hash_feed(&state, text, length);
    return subghz_signal_hash_finish(&state);
}
