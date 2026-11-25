/* Host driver for the Frequency Atlas's band plan parser and scan
 * (applications/system/remote_analyzer/plugins/frequency_atlas/ra_atlas_plan.c, ra_atlas_scan.c).
 *
 * argv: parse          stdin: a plan; prints "valid <0|1> bands <n> bad <n>", the header
 *                      fields, then "band <start> <end> <listen> <label>|<use>|<limit>" each
 *       wrap COLUMNS   stdin: text; prints it wrapped, then "<end>"
 *       mhz HZ         prints the frequency as the atlas writes it
 *       code TEXT      prints 1 when TEXT is a usable region code, else 0
 *       scan TUNABLE TRIGGER
 *                      TUNABLE is one 0/1 per band; ticks are milliseconds (1000 a second).
 *                      stdin: one command a line, each printing one line:
 *                        tick MS RSSI    -> "<retune|none> <current>"
 *                        decode MS NAME  -> "decoded <current>"
 *                        hold MS INDEX   -> "hold <0|1> <retune 0|1> <current>"
 *                        release MS      -> "release <current>"
 *                        status MS INDEX -> "status <out|quiet|active|seen> <seconds ago>"
 *                        signal MS       -> "signal <0|1>"
 *                        live INDEX      -> "live <protocol or -> <peak rssi>"
 *                      init prints "init <0|1> <current>" first.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ra_atlas_plan.h"
#include "ra_atlas_scan.h"

static char* read_all(size_t* size_out) {
    size_t size = 0, cap = 1024;
    char* text = malloc(cap);
    size_t n;
    while((n = fread(text + size, 1, cap - size - 1, stdin)) > 0) {
        size += n;
        if(cap - size < 2) text = realloc(text, cap *= 2);
    }
    text[size] = '\0';
    if(size_out) *size_out = size;
    return text;
}

static int run_parse(void) {
    size_t size = 0;
    char* text = read_all(&size);
    // Exactly the file's bytes plus the one the parser may write past them
    char* buffer = malloc(size + 1);
    memcpy(buffer, text, size);
    free(text);

    RaAtlasPlan plan;
    ra_atlas_plan_parse(&plan, buffer, size);
    printf(
        "valid %d bands %u bad %u\n",
        ra_atlas_plan_is_valid(&plan),
        (unsigned)plan.count,
        (unsigned)plan.bad_lines);
    printf(
        "region %s\nname %s\nsource %s\nnote %s\n", plan.region, plan.name, plan.source, plan.note);
    for(size_t i = 0; i < plan.count; i++) {
        const RaAtlasBand* b = &plan.bands[i];
        printf(
            "band %lu %lu %lu %s|%s|%s\n",
            (unsigned long)b->start,
            (unsigned long)b->end,
            (unsigned long)b->listen,
            b->label,
            b->use,
            b->limit);
    }
    free(buffer);
    return 0;
}

static const char* status_name(RaAtlasStatus status) {
    switch(status) {
    case RaAtlasStatusOutOfRange:
        return "out";
    case RaAtlasStatusQuiet:
        return "quiet";
    case RaAtlasStatusActive:
        return "active";
    default:
        return "seen";
    }
}

static int run_scan(const char* tunable_text, float trigger) {
    bool tunable[RA_ATLAS_SCAN_BANDS_MAX];
    const size_t count = strlen(tunable_text);
    if(count > RA_ATLAS_SCAN_BANDS_MAX) return 2;
    for(size_t i = 0; i < count; i++)
        tunable[i] = tunable_text[i] == '1';

    RaAtlasScan scan;
    const bool any = ra_atlas_scan_init(&scan, tunable, count, trigger, 1000, 0);
    printf("init %d %u\n", any, (unsigned)ra_atlas_scan_current(&scan));

    char line[256];
    while(fgets(line, sizeof(line), stdin)) {
        char cmd[16] = {0};
        char arg[64] = {0};
        unsigned long ms = 0;
        const int n = sscanf(line, "%15s %lu %63s", cmd, &ms, arg);
        if(n < 1) continue;
        if(!strcmp(cmd, "tick")) {
            const RaAtlasAction action =
                ra_atlas_scan_tick(&scan, strtof(arg, NULL), (uint32_t)ms);
            printf(
                "%s %u\n",
                action == RaAtlasActionRetune ? "retune" : "none",
                (unsigned)ra_atlas_scan_current(&scan));
        } else if(!strcmp(cmd, "decode")) {
            ra_atlas_scan_decoded(&scan, arg, "note", (uint32_t)ms);
            printf("decoded %u\n", (unsigned)ra_atlas_scan_current(&scan));
        } else if(!strcmp(cmd, "hold")) {
            bool retune = false;
            const bool ok = ra_atlas_scan_hold(&scan, (size_t)atoi(arg), (uint32_t)ms, &retune);
            printf("hold %d %d %u\n", ok, retune, (unsigned)ra_atlas_scan_current(&scan));
        } else if(!strcmp(cmd, "release")) {
            ra_atlas_scan_release(&scan, (uint32_t)ms);
            printf("release %u\n", (unsigned)ra_atlas_scan_current(&scan));
        } else if(!strcmp(cmd, "status")) {
            const size_t index = (size_t)atoi(arg);
            printf(
                "status %s %lu\n",
                status_name(ra_atlas_scan_status(&scan, index, (uint32_t)ms)),
                (unsigned long)ra_atlas_scan_seen_ago(&scan, index, (uint32_t)ms));
        } else if(!strcmp(cmd, "signal")) {
            printf("signal %d\n", ra_atlas_scan_is_signal(&scan, (uint32_t)ms));
        } else if(!strcmp(cmd, "live")) {
            // "live INDEX": the index is the number sscanf read as ms
            const RaAtlasLive* live = ra_atlas_scan_live(&scan, (size_t)ms);
            if(!live) {
                printf("live none\n");
            } else {
                printf("live %s %d\n", live->decoded ? live->protocol : "-", (int)live->rssi_peak);
            }
        } else {
            return 2;
        }
    }
    return 0;
}

int main(int argc, char** argv) {
    if(argc < 2) return 2;
    if(!strcmp(argv[1], "parse")) return run_parse();
    if(!strcmp(argv[1], "wrap") && argc > 2) {
        char* text = read_all(NULL);
        const size_t size = strlen(text) * 2 + 2;
        char* out = malloc(size);
        ra_atlas_wrap(text, (size_t)atoi(argv[2]), out, size);
        printf("%s<end>\n", out);
        // And cut short into a buffer far too small, which must still terminate
        char tiny[8];
        const size_t n = ra_atlas_wrap(text, (size_t)atoi(argv[2]), tiny, sizeof(tiny));
        if(n >= sizeof(tiny) || tiny[n] != '\0') return 3;
        free(out);
        free(text);
        return 0;
    }
    if(!strcmp(argv[1], "mhz") && argc > 2) {
        char out[16];
        ra_atlas_format_mhz((uint32_t)strtoul(argv[2], NULL, 10), out, sizeof(out));
        printf("%s\n", out);
        return 0;
    }
    if(!strcmp(argv[1], "code") && argc > 2) {
        printf("%d\n", ra_atlas_region_code_is_valid(argv[2]));
        return 0;
    }
    if(!strcmp(argv[1], "scan") && argc > 3) return run_scan(argv[2], strtof(argv[3], NULL));
    return 2;
}
