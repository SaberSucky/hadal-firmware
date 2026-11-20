/* Host driver for the Sub-GHz signal hash (applications/main/subghz/helpers/subghz_signal_hash.c)
 * and the user frequency file edits (applications/settings/hadal_settings/subghz_freqs.c).
 *
 * argv: hash [CHUNK]           stdin: a record; prints its hash, fed CHUNK bytes at a time
 *       list                   stdin: setting_user; prints "<hz>..." then "standard <0|1>"
 *       with HZ | without HZ | standard 0|1    stdin: setting_user; prints the new text
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "subghz_signal_hash.h"
#include "subghz_freqs.h"

static char* read_all(void) {
    size_t size = 0, cap = 1024;
    char* text = malloc(cap);
    size_t n;
    while((n = fread(text + size, 1, cap - size - 1, stdin)) > 0) {
        size += n;
        if(cap - size < 2) text = realloc(text, cap *= 2);
    }
    text[size] = '\0';
    return text;
}

int main(int argc, char** argv) {
    if(argc < 2) return 2;
    char* text = read_all();
    char* out = NULL;
    if(!strcmp(argv[1], "hash")) {
        const size_t chunk = argc > 2 ? (size_t)atoi(argv[2]) : strlen(text);
        SubGhzSignalHash state;
        subghz_signal_hash_init(&state);
        for(size_t at = 0; at < strlen(text); at += chunk) {
            const size_t left = strlen(text) - at;
            subghz_signal_hash_feed(&state, text + at, left < chunk ? left : chunk);
        }
        printf("%08X\n", (unsigned)subghz_signal_hash_finish(&state));
    } else if(!strcmp(argv[1], "list")) {
        uint32_t freqs[64];
        const size_t count = subghz_freqs_list(text, freqs, 64);
        for(size_t i = 0; i < count; i++) printf("%u\n", (unsigned)freqs[i]);
        printf("standard %d\n", subghz_freqs_standard(text));
    } else if(!strcmp(argv[1], "with") && argc > 2) {
        out = subghz_freqs_with(text, (uint32_t)strtoul(argv[2], NULL, 10));
    } else if(!strcmp(argv[1], "without") && argc > 2) {
        out = subghz_freqs_without(text, (uint32_t)strtoul(argv[2], NULL, 10));
    } else if(!strcmp(argv[1], "standard") && argc > 2) {
        out = subghz_freqs_set_standard(text, atoi(argv[2]));
    } else {
        return 2;
    }
    if(out) {
        fputs(out, stdout);
        free(out);
    }
    free(text);
    return 0;
}
