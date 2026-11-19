/*
 * Host harness for lib/asset_packs/asset_pack_format.c, driven by scripts/asset_packer_test.py.
 *
 *   asset_pack_check blob  FILE W H     status of a frame blob at W x H
 *   asset_pack_check bmx   FILE         parse a .bmx, check its frame at its own size
 *   asset_pack_check meta  FILE         parse an animated icon meta, print the fields
 *   asset_pack_check hash  PATH         path hash in hex
 *   asset_pack_check cost  FRAMES BYTES RAM cost of an icon
 *   asset_pack_check font  FILE         layout metrics of a u8g2 font
 *   asset_pack_check slots X            font slot names, in order, then the font limits
 *
 * Prints the AssetPackStatus number (0 = ok). Compressed frames are decoded with the bundled
 * heatshrink using the firmware's parameters (window 8, lookahead 4).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "asset_pack_format.h"
#include "heatshrink_decoder.h"

static bool hs_decode(
    const uint8_t* blob,
    size_t blob_size,
    uint8_t* out,
    size_t cap,
    size_t* out_len,
    void* context) {
    (void)context;
    const uint8_t* in = blob + 4;
    const size_t in_len = blob_size - 4;
    heatshrink_decoder* d = heatshrink_decoder_alloc(256, 8, 4);
    size_t in_off = 0, out_off = 0;
    bool ok = true, input_done = false;

    while(ok) {
        if(in_off < in_len) {
            size_t sunk = 0;
            if(heatshrink_decoder_sink(d, (uint8_t*)in + in_off, in_len - in_off, &sunk) < 0) {
                ok = false;
                break;
            }
            in_off += sunk;
        }
        if(in_off == in_len && !input_done) {
            input_done = true;
        }
        HSD_poll_res pr;
        do {
            size_t polled = 0;
            pr = heatshrink_decoder_poll(d, out + out_off, cap - out_off, &polled);
            if(pr < 0) {
                ok = false;
                break;
            }
            out_off += polled;
            if(pr == HSDR_POLL_MORE && out_off == cap) {
                ok = false; /* would overflow the decode buffer */
                break;
            }
        } while(pr == HSDR_POLL_MORE);
        if(!ok) break;
        if(input_done) {
            if(heatshrink_decoder_finish(d) == HSDR_FINISH_DONE) break;
        }
    }
    heatshrink_decoder_free(d);
    *out_len = out_off;
    return ok;
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

int main(int argc, char** argv) {
    if(argc < 3) return 99;
    uint8_t scratch[ASSET_PACK_DECODE_BUFFER_SIZE];
    const char* cmd = argv[1];

    if(!strcmp(cmd, "hash")) {
        printf("%08x\nstatus 0\n", asset_pack_path_hash(argv[2]));
        return 0;
    }
    if(!strcmp(cmd, "cost") && argc == 4) {
        printf(
            "%zu\nstatus 0\n",
            asset_pack_icon_ram_cost(strtoul(argv[2], 0, 10), strtoul(argv[3], 0, 10)));
        return 0;
    }

    if(!strcmp(cmd, "slots")) {
        for(unsigned i = 0; asset_pack_font_slot_name(i); i++) {
            printf("%s\n", asset_pack_font_slot_name(i));
        }
        printf(
            "%u %u %u\nstatus 0\n",
            ASSET_PACK_FONT_SLOTS,
            ASSET_PACK_MAX_FONT_BYTES,
            ASSET_PACK_FONT_RAM_BUDGET);
        return 0;
    }

    size_t size = 0;
    uint8_t* data = read_file(argv[2], &size);
    if(!data) return 98;
    int status = 99;

    if(!strcmp(cmd, "blob") && argc == 5) {
        status = asset_pack_blob_check(
            data, size, strtoul(argv[3], 0, 10), strtoul(argv[4], 0, 10), hs_decode, NULL, scratch);
    } else if(!strcmp(cmd, "bmx")) {
        AssetPackBmx bmx;
        status = asset_pack_bmx_parse(data, size, &bmx);
        if(status == AssetPackOk) {
            status = asset_pack_blob_check(
                bmx.blob, bmx.blob_size, bmx.width, bmx.height, hs_decode, NULL, scratch);
            printf("%u %u\n", bmx.width, bmx.height);
        }
    } else if(!strcmp(cmd, "meta")) {
        AssetPackAnimMeta m;
        status = asset_pack_anim_meta_parse(data, size, &m);
        if(status == AssetPackOk) printf("%u %u %u %u\n", m.width, m.height, m.frame_rate, m.frame_count);
    } else if(!strcmp(cmd, "font")) {
        AssetPackFontMetrics m;
        status = asset_pack_font_metrics(data, size, &m) ? 0 : AssetPackErrTooShort;
        if(status == 0) printf("%d %d %d\n", m.max_char_height, m.ascent_a, m.descent_g);
    }
    free(data);
    printf("status %d\n", status);
    return status;
}
