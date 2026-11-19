/*
 * Host test of lib/asset_packs/asset_packs.c, driven by scripts/asset_packer_test.py.
 *
 * The real manager and the real firmware decoder (lib/toolbox/compress.c) are compiled here on a
 * PC, on top of the thin stand-ins in stubs/ for furi threads/mutexes and storage, together with
 * the firmware's real generated icons and icon index. Packs written by the Python packer are
 * loaded through the real scanning, validation and install code, under AddressSanitizer.
 *
 *   verify ROOT NAME              load a pack, report every index icon: replaced or stock
 *   cycle  ROOT NAME N            unload and reload N times, checking each state
 *   switch ROOT A B               select A and at once B: only B may end up loaded
 *   stress ROOT NAME SECONDS      a drawing thread against unload/reload/switch
 *   crash  ROOT                   the crash marker is reported once
 *
 * ROOT is a directory standing in for the SD card (ROOT/ext/asset_packs/...) and internal
 * storage (ROOT/int/). HOST_LOG=1 prints the manager's log lines.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <unistd.h>

#include <furi.h>

// The code under test, included so its internal state can be inspected
#include "../../lib/asset_packs/asset_packs.c"

#define CANVAS_DECODE_BUFFER (128u * 64 / 8)

static CompressIcon* canvas_decoder;

static void wait_for_loader(void) {
    if(asset_packs.loader) furi_thread_join(asset_packs.loader);
}

static size_t frame_bitmap(const Icon* icon, unsigned frame, uint8_t* out) {
    // Exactly what canvas_draw_icon() does before blitting
    uint8_t* data = NULL;
    compress_icon_decode(canvas_decoder, icon->frames[frame], &data);
    size_t size = (size_t)((icon->width + 7) / 8) * icon->height;
    memcpy(out, data, size);
    return size;
}

static bool icons_draw_identically(const Icon* a, const Icon* b) {
    if(a->width != b->width || a->height != b->height || a->frame_count != b->frame_count) {
        return false;
    }
    uint8_t pa[CANVAS_DECODE_BUFFER], pb[CANVAS_DECODE_BUFFER];
    for(unsigned f = 0; f < a->frame_count; f++) {
        size_t na = frame_bitmap(a, f, pa), nb = frame_bitmap(b, f, pb);
        if(na != nb || memcmp(pa, pb, na) != 0) return false;
    }
    return true;
}

static void report_all(void) {
    for(size_t i = 0; i < assets_icon_index_count; i++) {
        const Icon* original = assets_icon_index[i].icon;
        const Icon* shown = asset_packs_icon_acquire(original);
        if(shown == original) {
            printf("S %08x\n", assets_icon_index[i].path_hash);
        } else {
            bool same = icons_draw_identically(original, shown);
            asset_packs_icon_release();
            printf("R %08x %s\n", assets_icon_index[i].path_hash, same ? "same" : "diff");
        }
    }
}

static size_t count_replaced(void) {
    size_t replaced = 0;
    for(size_t i = 0; i < assets_icon_index_count; i++) {
        const Icon* original = assets_icon_index[i].icon;
        if(asset_packs_icon_acquire(original) != original) {
            asset_packs_icon_release();
            replaced++;
        }
    }
    return replaced;
}

/* ---- stress ---- */

static atomic_bool stress_run;
static atomic_ulong stress_draws;
static const char* stress_name;

static void* drawer(void* arg) {
    (void)arg;
    CompressIcon* decoder = compress_icon_alloc(CANVAS_DECODE_BUFFER);
    uint8_t* data;
    while(stress_run) {
        for(size_t i = 0; i < assets_icon_index_count && stress_run; i++) {
            const Icon* original = assets_icon_index[i].icon;
            const Icon* shown = asset_packs_icon_acquire(original);
            for(unsigned f = 0; f < shown->frame_count; f++) {
                compress_icon_decode(decoder, shown->frames[f], &data);
                volatile uint8_t touch = data[0];
                (void)touch;
            }
            if(shown != original) asset_packs_icon_release();
            atomic_fetch_add(&stress_draws, 1);
        }
    }
    compress_icon_free(decoder);
    return NULL;
}

int main(int argc, char** argv) {
    if(argc < 3) return 2;
    const char* command = argv[1];
    host_storage_root = argv[2];
    host_log_enabled = getenv("HOST_LOG") != NULL;
    canvas_decoder = compress_icon_alloc(CANVAS_DECODE_BUFFER);
    int rc = 0;

    if(!strcmp(command, "verify") && argc == 4) {
        asset_packs_set_active(argv[3]);
        wait_for_loader();
        printf("loaded %zu\n", asset_packs.icon_count);
        report_all();
    } else if(!strcmp(command, "cycle") && argc == 5) {
        asset_packs_set_active(argv[3]);
        wait_for_loader();
        const size_t loaded = asset_packs.icon_count;
        printf("loaded %zu\n", loaded);
        for(int n = atoi(argv[4]); n > 0; n--) {
            asset_packs_unload_icons();
            if(asset_packs.icon_count != 0 || asset_packs.active || count_replaced() != 0) {
                printf("FAIL: icons still there after unload\n");
                rc = 1;
                break;
            }
            asset_packs_reload_icons();
            wait_for_loader();
            if(asset_packs.icon_count != loaded || count_replaced() != loaded) {
                printf("FAIL: reload gave %zu of %zu\n", asset_packs.icon_count, loaded);
                rc = 1;
                break;
            }
        }
        // reload when nothing was unloaded must do nothing
        asset_packs_reload_icons();
        wait_for_loader();
        printf("cycles done, %zu loaded\n", asset_packs.icon_count);
    } else if(!strcmp(command, "switch") && argc == 5) {
        asset_packs_set_active(argv[3]);
        asset_packs_set_active(argv[4]); // cancels the first load, however far it got
        wait_for_loader();
        char name[ASSET_PACK_NAME_SIZE];
        asset_packs_get_active(name, sizeof(name));
        printf("active %s\nloaded %zu\n", name, asset_packs.icon_count);
        report_all();
    } else if(!strcmp(command, "stress") && argc == 5) {
        stress_name = argv[3];
        asset_packs_set_active(stress_name);
        wait_for_loader();
        atomic_store(&stress_run, true);
        pthread_t thread;
        pthread_create(&thread, NULL, drawer, NULL);
        const uint32_t end = furi_get_tick() + (uint32_t)(atof(argv[4]) * 1000);
        unsigned long ops = 0;
        while(furi_get_tick() < end) {
            switch(ops++ % 5) {
            case 0:
                asset_packs_unload_icons();
                break;
            case 1:
                asset_packs_reload_icons();
                wait_for_loader();
                break;
            case 2:
                asset_packs_set_active("");
                break;
            case 3:
                asset_packs_set_active(stress_name);
                break;
            default:
                wait_for_loader();
                break;
            }
        }
        atomic_store(&stress_run, false);
        pthread_join(thread, NULL);
        wait_for_loader();
        printf("draws %lu ops %lu\n", (unsigned long)atomic_load(&stress_draws), ops);
    } else if(!strcmp(command, "crash")) {
        char path[512];
        snprintf(path, sizeof(path), "%s/int/.asset_pack_loading", host_storage_root);
        FILE* f = fopen(path, "wb");
        if(f) fclose(f);
        bool first = asset_packs_previous_load_crashed();
        bool second = asset_packs_previous_load_crashed();
        printf("crash %d %d\n", first, second);
    } else {
        rc = 2;
    }

    wait_for_loader();
    // Deselecting joins and frees the last loader thread and drops any icons: leak check clean
    asset_packs_set_active(NULL);
    if(asset_packs.loader != NULL) {
        printf("FAIL: a loader is still tracked after deselecting\n");
        rc = 1;
    }
    compress_icon_free(canvas_decoder);
    return rc;
}
