/* Host driver for lib/infrared/signal/infrared_learned.c.
 *
 * argv: bump <library> <button> <key hex> <1|3>   stdin: the text; stdout: the new text
 *       find <library> <button> <max>              stdin: the text; stdout: "<key> <score>" lines
 *       fuzz N SEED
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "infrared_learned.h"

static char* read_all(void) {
    size_t size = 0, cap = 4096;
    char* text = malloc(cap);
    size_t n;
    while((n = fread(text + size, 1, cap - size - 1, stdin)) > 0) {
        size += n;
        if(cap - size < 2) text = realloc(text, cap *= 2);
    }
    text[size] = '\0';
    return text;
}

static unsigned rng_state;
static unsigned rng(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

int main(int argc, char** argv) {
    if(argc == 4 && strcmp(argv[1], "fuzz") == 0) {
        rng_state = (unsigned)atoi(argv[3]) | 1;
        static const char* const names[] = {"tv.ir", "Power", "Vol_up", "audio.ir", "x", "Mute"};
        char* text = strdup("");
        for(int n = 0; n < atoi(argv[2]); n++) {
            if(rng() % 50 == 0) {
                // damage the text: a cut line, junk bytes
                size_t length = strlen(text);
                if(length) text[rng() % length] = (char)(rng() % 256 ? rng() % 256 : 'x');
                text[strlen(text) / 2] = '\0';
            }
            char* next = infrared_learned_bump(
                text, names[rng() % 6], names[rng() % 6], rng() % 300, rng() % 2 ? 1 : 3);
            if(!next) abort();
            free(text);
            text = next;
            size_t lines = 0;
            for(const char* p = text; *p; p++) lines += *p == '\n';
            if(lines > INFRARED_LEARNED_MAX_LINES) abort();
            InfraredLearnedCode codes[8];
            const size_t found = infrared_learned_find(text, names[rng() % 6], names[rng() % 6], codes, 8);
            for(size_t i = 1; i < found; i++) {
                if(codes[i].score > codes[i - 1].score) abort();
            }
        }
        free(text);
        printf("ok\n");
        return 0;
    }
    char* text = read_all();
    if(argc == 6 && strcmp(argv[1], "bump") == 0) {
        char* out = infrared_learned_bump(
            text, argv[2], argv[3], (uint32_t)strtoul(argv[4], NULL, 16), (InfraredLearnedWeight)atoi(argv[5]));
        if(!out) {
            printf("NULL\n");
        } else {
            fputs(out, stdout);
            free(out);
        }
    } else if(argc == 5 && strcmp(argv[1], "find") == 0) {
        InfraredLearnedCode codes[64];
        const size_t count = infrared_learned_find(text, argv[2], argv[3], codes, (size_t)atoi(argv[4]));
        for(size_t i = 0; i < count; i++) {
            printf("%08X %u\n", (unsigned)codes[i].key, (unsigned)codes[i].score);
        }
    } else {
        return 2;
    }
    free(text);
    return 0;
}
