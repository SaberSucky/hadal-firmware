/* Host driver for lib/infrared/signal/infrared_raw_trim.c.
 *
 *   ir_trim_host < signals    each input line: timings separated by spaces
 *                             each output line: <fast length> <turbo length> <turbo gap us>
 *   ir_trim_host fuzz N SEED  N random signals; checks what must always hold, prints "ok N"
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "infrared_raw_trim.h"

#define MAX_TIMINGS 4096

static void check_cut(const uint32_t* t, size_t size, size_t cut, const char* what) {
    if(size == 0) {
        if(cut != 0) { fprintf(stderr, "%s: empty signal gave %zu\n", what, cut); abort(); }
        return;
    }
    if(cut < 1 || cut > size) { fprintf(stderr, "%s: cut %zu of %zu\n", what, cut, size); abort(); }
    if(cut < size) {
        /* a cut drops a long space and everything after it, and ends on a mark */
        if(cut % 2 != 1) { fprintf(stderr, "%s: cut %zu ends on a space\n", what, cut); abort(); }
        /* at a frame gap, so the first frame (which ends at the first gap) is sent whole */
        if(t[cut] <= 10000) { fprintf(stderr, "%s: cut %zu not at a frame gap (%u)\n", what, cut, t[cut]); abort(); }
    }
}

static uint32_t rng_state;
static uint32_t rng(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

static int fuzz(unsigned count, unsigned seed) {
    static uint32_t t[MAX_TIMINGS];
    rng_state = seed ? seed : 1;
    for(unsigned n = 0; n < count; n++) {
        size_t size = rng() % 400;
        /* frames of one of a few shapes, repeated, with gaps; sometimes noise */
        size_t frame = 1 + 2 * (rng() % 40);
        for(size_t i = 0; i < size; i++) {
            uint32_t v;
            switch(rng() % 8) {
            case 0: v = rng(); break; /* anything, up to UINT32_MAX */
            case 1: v = 9000 + rng() % 400; break;
            case 2: v = 2400 + rng() % 100; break;
            default: v = 300 + rng() % 1500; break;
            }
            if(i % (frame + 1) == frame) v = 10001 + rng() % 120000; /* frame gap */
            t[i] = v;
        }
        for(int turbo = 0; turbo < 2; turbo++) {
            size_t cut = infrared_raw_trim_length(t, size, turbo);
            check_cut(t, size, cut, turbo ? "turbo" : "fast");
            uint32_t gap = infrared_raw_trim_turbo_gap_us(t, cut);
            if(size && (gap < 20000 || gap > 50000)) { fprintf(stderr, "gap %u\n", gap); abort(); }
        }
        /* fast never sends less than turbo */
        if(infrared_raw_trim_length(t, size, false) < infrared_raw_trim_length(t, size, true)) {
            fprintf(stderr, "fast sent less than turbo\n");
            abort();
        }
    }
    if(infrared_raw_trim_length(NULL, 0, true) != 0) abort();
    printf("ok %u\n", count);
    return 0;
}

int main(int argc, char** argv) {
    if(argc == 4 && strcmp(argv[1], "fuzz") == 0) return fuzz(atoi(argv[2]), atoi(argv[3]));

    static char line[1 << 16];
    static uint32_t t[MAX_TIMINGS];
    while(fgets(line, sizeof(line), stdin)) {
        size_t size = 0;
        for(char* tok = strtok(line, " \n"); tok && size < MAX_TIMINGS; tok = strtok(NULL, " \n")) {
            t[size++] = (uint32_t)strtoul(tok, NULL, 10);
        }
        size_t fast = infrared_raw_trim_length(t, size, false);
        size_t turbo = infrared_raw_trim_length(t, size, true);
        check_cut(t, size, fast, "fast");
        check_cut(t, size, turbo, "turbo");
        printf("%zu %zu %u\n", fast, turbo, infrared_raw_trim_turbo_gap_us(t, turbo));
    }
    return 0;
}
