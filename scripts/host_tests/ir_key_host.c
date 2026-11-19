/* Host driver for lib/infrared/signal/infrared_signal_key.c, with the real IR encoders and
 * decoders.
 *
 *   ir_key_host selftest N SEED   NEC/NECext and NEC42/NEC42ext aliasing; every protocol's
 *                                 encoded signal keyed as raw equals its message key
 *   ir_key_host < signals         each input line "P <protocol> <address> <command>" or
 *                                 "R <frequency> <timings...>"; prints one key (hex) per line
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "infrared.h"
#include "infrared_signal_key.h"

#define MAX_TIMINGS 4096

static uint32_t rng_state;
static uint32_t rng(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

static uint32_t mask(uint8_t bits) {
    return bits >= 32 ? 0xFFFFFFFFU : ((1U << bits) - 1);
}

/* One transmission of the message as raw timings starting with a mark (the encoder's leading
 * silence dropped, runs of one level merged). Returns the number of timings. */
static size_t encode(const InfraredMessage* message, size_t transmissions, uint32_t* out) {
    InfraredEncoderHandler* encoder = infrared_alloc_encoder();
    infrared_reset_encoder(encoder, message);
    size_t size = 0;
    bool last_level = false;
    size_t done = 0;
    while(done < transmissions && size < MAX_TIMINGS) {
        uint32_t duration;
        bool level;
        InfraredStatus status = infrared_encode(encoder, &duration, &level);
        if(status == InfraredStatusError) abort();
        /* Done comes with the transmission's last timing, as for the transmitter */
        if(status == InfraredStatusDone) done++;
        if(size == 0 && !level) continue; /* leading silence */
        if(size && level == last_level) {
            out[size - 1] += duration;
        } else {
            out[size++] = duration;
        }
        last_level = level;
    }
    infrared_free_encoder(encoder);
    /* end on a mark */
    if(size && !last_level) size--;
    return size;
}

static int selftest(unsigned count, unsigned seed) {
    rng_state = seed ? seed : 1;
    InfraredDecoderHandler* decoder = infrared_alloc_decoder();
    static uint32_t timings[MAX_TIMINGS];
    int failures = 0;

    for(unsigned n = 0; n < count; n++) {
        const uint32_t a = rng() & 0xFF, c = rng() & 0xFF;
        InfraredMessage nec = {.protocol = InfraredProtocolNEC, .address = a, .command = c};
        InfraredMessage ext = {
            .protocol = InfraredProtocolNECext,
            .address = a | ((~a & 0xFF) << 8),
            .command = c | ((~c & 0xFF) << 8)};
        if(infrared_signal_key_message(&nec) != infrared_signal_key_message(&ext)) {
            fprintf(stderr, "NEC %02x/%02x and its NECext differ\n", a, c);
            failures++;
        }
        ext.command ^= 0x100; /* no longer the inverse: another signal */
        if(infrared_signal_key_message(&nec) == infrared_signal_key_message(&ext)) {
            fprintf(stderr, "NEC %02x/%02x equals a different NECext\n", a, c);
            failures++;
        }
        const uint32_t a13 = rng() & 0x1FFF;
        InfraredMessage n42 = {.protocol = InfraredProtocolNEC42, .address = a13, .command = c};
        InfraredMessage n42e = {
            .protocol = InfraredProtocolNEC42ext,
            .address = a13 | ((~a13 & 0x1FFF) << 13),
            .command = c | ((~c & 0xFF) << 8)};
        if(infrared_signal_key_message(&n42) != infrared_signal_key_message(&n42e)) {
            fprintf(stderr, "NEC42 %04x/%02x and its NEC42ext differ\n", a13, c);
            failures++;
        }
    }

    /* every protocol: its own encoding, read back as a raw capture, is the same code */
    for(int p = 0; p < InfraredProtocolMAX; p++) {
        unsigned ok = 0, tried = 0;
        for(unsigned n = 0; n < count / 8 + 1; n++) {
            InfraredMessage m = {
                .protocol = p,
                .address = rng() & mask(infrared_get_protocol_address_length(p)),
                .command = rng() & mask(infrared_get_protocol_command_length(p)),
            };
            size_t reps = infrared_get_protocol_min_repeat_count(p);
            size_t size = encode(&m, reps, timings);
            uint64_t raw = infrared_signal_key_raw(
                timings, size, infrared_get_protocol_frequency(p), decoder);
            tried++;
            if(raw == infrared_signal_key_message(&m)) ok++;
            /* without a decoder a capture is keyed by its timings, never as a message */
            if(infrared_signal_key_raw(timings, size, infrared_get_protocol_frequency(p), NULL) ==
               infrared_signal_key_message(&m)) {
                fprintf(stderr, "%s: timing key equals message key\n", infrared_get_protocol_name(p));
                failures++;
            }
        }
        printf("roundtrip %s %u/%u\n", infrared_get_protocol_name(p), ok, tried);
    }
    /* held buttons (several transmissions, NEC repeat frames) are still the code; a capture with
     * a frame no decoder knows, or with two different codes, is not */
    for(unsigned n = 0; n < count / 8 + 1; n++) {
        const InfraredProtocol protocols[] = {
            InfraredProtocolNEC, InfraredProtocolSamsung32, InfraredProtocolRC6, InfraredProtocolSIRC};
        InfraredProtocol p = protocols[n % 4];
        InfraredMessage m = {
            .protocol = p,
            .address = rng() & mask(infrared_get_protocol_address_length(p)),
            .command = rng() & mask(infrared_get_protocol_command_length(p)),
        };
        const uint32_t frequency = infrared_get_protocol_frequency(p);
        const uint64_t want = infrared_signal_key_message(&m);
        size_t size = encode(&m, 3 + rng() % 3, timings);
        if(infrared_signal_key_raw(timings, size, frequency, decoder) != want) {
            fprintf(stderr, "%s held: not the code\n", infrared_get_protocol_name(p));
            failures++;
        }
        /* plus an unknown frame */
        size_t junk = size;
        timings[junk++] = 50000 + rng() % 50000;
        for(unsigned k = 0; k < 9; k++) timings[junk++] = 3000 + rng() % 400;
        if(infrared_signal_key_raw(timings, junk, frequency, decoder) == want) {
            fprintf(stderr, "%s with a junk frame: taken for the code\n", infrared_get_protocol_name(p));
            failures++;
        }
        /* plus another code */
        InfraredMessage other = m;
        other.command ^= 1;
        size_t both = size;
        timings[both++] = 60000;
        both += encode(&other, 1, timings + both);
        const uint64_t got = infrared_signal_key_raw(timings, both, frequency, decoder);
        if(got == want || got == infrared_signal_key_message(&other)) {
            fprintf(stderr, "%s followed by another code: taken for one code\n", infrared_get_protocol_name(p));
            failures++;
        }
    }

    /* empty and odd input */
    if(infrared_signal_key_raw(timings, 0, 38000, decoder) ==
       infrared_signal_key_raw(timings, 0, 36000, decoder)) {
        fprintf(stderr, "empty captures of two carriers equal\n");
        failures++;
    }

    infrared_free_decoder(decoder);
    printf("%s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}

int main(int argc, char** argv) {
    if(argc == 4 && strcmp(argv[1], "selftest") == 0) return selftest(atoi(argv[2]), atoi(argv[3]));

    InfraredDecoderHandler* decoder = infrared_alloc_decoder();
    static char line[1 << 16];
    static uint32_t timings[MAX_TIMINGS];
    while(fgets(line, sizeof(line), stdin)) {
        char* tok = strtok(line, " \n");
        if(!tok) continue;
        if(tok[0] == 'P') {
            InfraredMessage m = {0};
            m.protocol = infrared_get_protocol_by_name(strtok(NULL, " \n"));
            m.address = strtoul(strtok(NULL, " \n"), NULL, 0);
            m.command = strtoul(strtok(NULL, " \n"), NULL, 0);
            printf("%016llx\n", (unsigned long long)infrared_signal_key_message(&m));
        } else {
            uint32_t frequency = strtoul(strtok(NULL, " \n"), NULL, 10);
            size_t size = 0;
            for(tok = strtok(NULL, " \n"); tok && size < MAX_TIMINGS; tok = strtok(NULL, " \n")) {
                timings[size++] = strtoul(tok, NULL, 10);
            }
            printf(
                "%016llx\n",
                (unsigned long long)infrared_signal_key_raw(timings, size, frequency, decoder));
        }
    }
    infrared_free_decoder(decoder);
    return 0;
}
