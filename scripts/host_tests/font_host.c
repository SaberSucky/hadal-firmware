/*
 * Host test of asset_pack_font_check() against the real u8g2 text renderer, driven by
 * scripts/asset_packer_test.py.
 *
 * u8g2 trusts font data completely and reads wherever the data tells it to, so the only way to
 * know a validated font is safe is to render with it. Fonts are copied into exactly-sized heap
 * blocks and drawn with the firmware's u8g2 sources under AddressSanitizer: any read past the
 * end of the font is an error.
 *
 *   check FILE SLOT        status of the font in FILE as a replacement for stock slot SLOT
 *   batch SLOT FILE        status of each font in FILE (<u32 LE size> <bytes>, repeated), one per line
 *   draw  FILE             render every character with FILE; prints how many pixels were lit
 *   fuzz  SLOT ROUNDS SEED mutate the stock font, and render every mutation that is accepted
 *
 * The five stock slot fonts come from stock_fonts.c, generated from lib/u8g2/u8g2_fonts.c.
 */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <u8g2.h>
#include "asset_pack_format.h"

extern const uint8_t* const stock_fonts[ASSET_PACK_FONT_SLOTS];
extern const size_t stock_font_sizes[ASSET_PACK_FONT_SLOTS];

/* ---- a real 128x64 display target, so u8g2 decodes glyphs instead of skipping them ---- */

static const u8x8_display_info_t display_info = {
    .tile_width = 16,
    .tile_height = 8,
    .pixel_width = 128,
    .pixel_height = 64,
};

static uint8_t display_cb(u8x8_t* u8x8, uint8_t msg, uint8_t arg_int, void* arg_ptr) {
    (void)arg_int;
    (void)arg_ptr;
    if(msg == U8X8_MSG_DISPLAY_SETUP_MEMORY) {
        u8x8_d_helper_display_setup_memory(u8x8, &display_info);
    } else if(msg == U8X8_MSG_DISPLAY_INIT) {
        u8x8_d_helper_display_init(u8x8);
    }
    return 1;
}

static u8g2_t u8g2;
static uint8_t frame[128 * 8];

static void setup_display(void) {
    u8g2_SetupDisplay(&u8g2, display_cb, u8x8_cad_empty, u8x8_byte_empty, u8x8_dummy_cb);
    u8g2_SetupBuffer(&u8g2, frame, 8, u8g2_ll_hvline_vertical_top_lsb, U8G2_R0);
}

static unsigned lit_pixels(void) {
    unsigned lit = 0;
    for(size_t i = 0; i < sizeof(frame); i++) lit += (unsigned)__builtin_popcount(frame[i]);
    return lit;
}

/** Render every character the way the firmware does, with the font in an exactly-sized block. */
static unsigned render_with(const uint8_t* font_bytes, size_t size) {
    uint8_t* font = malloc(size);
    memcpy(font, font_bytes, size);
    unsigned lit = 0;

    u8g2_SetFontMode(&u8g2, 1);
    u8g2_SetFont(&u8g2, font);
    for(unsigned encoding = 0; encoding < 256; encoding++) {
        memset(frame, 0, sizeof(frame));
        u8g2_DrawGlyph(&u8g2, 20, 40, encoding);
        lit += lit_pixels();
    }
    // The unicode path too: even with no unicode glyphs the lookup must stay inside the font
    static const uint16_t wide[] = {0x100, 0x20AC, 0x0416, 0x7FFF, 0xFFFE, 0xFFFF};
    for(size_t i = 0; i < sizeof(wide) / sizeof(wide[0]); i++) {
        u8g2_DrawGlyph(&u8g2, 20, 40, wide[i]);
    }
    memset(frame, 0, sizeof(frame));
    u8g2_DrawStr(&u8g2, 2, 30, "The quick brown fox jumps over 0123456789 \xC3\xA9\xC2\xB0\xE2\x82\xAC");
    lit += lit_pixels();
    (void)u8g2_GetStrWidth(&u8g2, "Hadal");

    u8g2_SetFont(&u8g2, stock_fonts[0]); // do not leave a pointer to the freed block
    free(font);
    return lit;
}

static AssetPackFontMetrics stock_metrics(unsigned slot) {
    AssetPackFontMetrics metrics;
    asset_pack_font_metrics(stock_fonts[slot], stock_font_sizes[slot], &metrics);
    return metrics;
}

static uint8_t* read_file(const char* path, size_t* size) {
    FILE* f = fopen(path, "rb");
    if(!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t* data = malloc(n > 0 ? (size_t)n : 1);
    if(fread(data, 1, (size_t)n, f) != (size_t)n) {
        free(data);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *size = (size_t)n;
    return data;
}

static uint64_t rng_state;
static uint32_t rng(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return (uint32_t)(rng_state >> 11);
}

static void on_timeout(int sig) {
    (void)sig;
    printf("TIMEOUT: rendering did not finish (a font drove the decoder into a long loop)\n");
    fflush(stdout);
    _exit(3);
}

int main(int argc, char** argv) {
    if(argc < 3) return 99;
    signal(SIGALRM, on_timeout);
    alarm(60);
    setup_display();

    if(!strcmp(argv[1], "check") && argc == 4) {
        size_t size = 0;
        uint8_t* data = read_file(argv[2], &size);
        if(!data) return 98;
        AssetPackFontMetrics expected = stock_metrics((unsigned)atoi(argv[3]));
        uint8_t* exact = malloc(size ? size : 1); // exact-sized: the checker must not read past it
        memcpy(exact, data, size);
        printf("status %d\n", asset_pack_font_check(exact, size, &expected));
        free(exact);
        free(data);
    } else if(!strcmp(argv[1], "batch") && argc == 4) {
        size_t total = 0;
        uint8_t* data = read_file(argv[3], &total);
        if(!data) return 98;
        AssetPackFontMetrics expected = stock_metrics((unsigned)atoi(argv[2]));
        size_t at = 0;
        while(at + 4 <= total) {
            const size_t size = (size_t)data[at] | ((size_t)data[at + 1] << 8) |
                                ((size_t)data[at + 2] << 16) | ((size_t)data[at + 3] << 24);
            at += 4;
            if(size > total - at) return 97;
            uint8_t* exact = malloc(size ? size : 1); // exact-sized: the checker must not read past it
            memcpy(exact, data + at, size);
            printf("%d\n", asset_pack_font_check(exact, size, &expected));
            free(exact);
            at += size;
        }
        free(data);
    } else if(!strcmp(argv[1], "draw")) {
        size_t size = 0;
        uint8_t* data = read_file(argv[2], &size);
        if(!data) return 98;
        printf("lit %u\n", render_with(data, size));
        free(data);
    } else if(!strcmp(argv[1], "fuzz") && argc == 5) {
        const unsigned slot = (unsigned)atoi(argv[2]);
        const unsigned rounds = (unsigned)atoi(argv[3]);
        rng_state = (uint64_t)strtoull(argv[4], 0, 10) * 2654435761u + 88172645463325252ull;
        const AssetPackFontMetrics expected = stock_metrics(slot);
        const size_t stock_size = stock_font_sizes[slot];
        unsigned accepted = 0, rejected = 0, accepted_changed = 0;

        for(unsigned r = 0; r < rounds; r++) {
            uint8_t* font = malloc(stock_size + 64);
            memcpy(font, stock_fonts[slot], stock_size);
            size_t size = stock_size;
            const unsigned mutations = 1 + rng() % 6;
            for(unsigned m = 0; m < mutations; m++) {
                switch(rng() % 6) {
                case 0: // flip a bit anywhere
                    font[rng() % size] ^= (uint8_t)(1u << (rng() % 8));
                    break;
                case 1: // overwrite a byte
                    font[rng() % size] = (uint8_t)rng();
                    break;
                case 2: // flip a bit in the header, where the layout is decided
                    font[rng() % 23] ^= (uint8_t)(1u << (rng() % 8));
                    break;
                case 3: // damage a glyph size byte or two
                    if(size > 23) font[23 + rng() % (size - 23)] = (uint8_t)(rng() % 12);
                    break;
                case 4: // truncate
                    size = 1 + rng() % size;
                    break;
                default: // extend with junk
                    for(unsigned i = 0; i < 1 + rng() % 40 && size < stock_size + 64; i++) {
                        font[size++] = (uint8_t)rng();
                    }
                    break;
                }
            }
            uint8_t* exact = malloc(size);
            memcpy(exact, font, size);
            free(font);

            if(asset_pack_font_check(exact, size, &expected) == AssetPackOk) {
                accepted++;
                if(size != stock_size || memcmp(exact, stock_fonts[slot], size) != 0) {
                    accepted_changed++;
                }
                render_with(exact, size); // any read outside `exact` is an AddressSanitizer error
            } else {
                rejected++;
            }
            free(exact);
        }
        printf("rounds %u accepted %u (changed %u) rejected %u\n", rounds, accepted, accepted_changed, rejected);
    } else {
        return 2;
    }
    return 0;
}
