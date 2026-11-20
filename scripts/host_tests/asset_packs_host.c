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
 *   marker ROOT NAME              when the crash marker exists: through load, stable, reload, drop
 *   fonts  ROOT NAME              which font slots a pack replaces; then drop it and watch the
 *                                 replaced fonts leave the graveyard after the grace period
 *   app    ROOT NAME SYM...       an app on the SD card with icons named SYM ("I_X_10x10", or
 *                                 "A_X_8x8:3" with 3 frames): which the pack replaces, while it
 *                                 runs, after it is gone and after the pack's icons are dropped
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

/* The five built-in fonts, generated from lib/u8g2/u8g2_fonts.c (stock_fonts.c) */
extern const uint8_t* const stock_fonts[ASSET_PACK_FONT_SLOTS];
extern const size_t stock_font_sizes[ASSET_PACK_FONT_SLOTS];

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

/** Icons that replace the firmware's, not those kept for apps alone */
static size_t count_loaded(void) {
    size_t loaded = 0;
    for(size_t i = 0; i < asset_packs.icon_count; i++) {
        if(asset_packs.icons[i].original) loaded++;
    }
    return loaded;
}

/* ---- an app on the SD card: its icons, as its symbol table would give them ---- */

#define FAKE_APP_MAX 32

struct FlipperApplication {
    size_t count;
    const char* names[FAKE_APP_MAX];
    Icon icons[FAKE_APP_MAX];
};

void flipper_application_for_each_object(
    FlipperApplication* app,
    size_t object_size,
    FlipperApplicationObjectCallback callback,
    void* context) {
    if(object_size != sizeof(Icon)) return;
    for(size_t i = 0; i < app->count; i++) {
        callback(app->names[i], &app->icons[i], context);
    }
}

/** "I_X_10x10" or "A_X_8x8:3" into an Icon of that size and frame count (frames never read) */
static void fake_app_add(FlipperApplication* app, char* symbol) {
    unsigned width = 1, height = 1, frames = 1;
    char* colon = strchr(symbol, ':');
    if(colon) {
        *colon = '\0';
        frames = (unsigned)atoi(colon + 1);
    }
    const char* size = strrchr(symbol, '_');
    if(size) sscanf(size + 1, "%ux%u", &width, &height);
    Icon icon = {
        .width = width, .height = height, .frame_count = frames, .frame_rate = 4, .frames = NULL};
    memcpy(&app->icons[app->count], &icon, sizeof(Icon));
    app->names[app->count] = symbol;
    app->count++;
}

static void report_app(const char* when, FlipperApplication* app) {
    for(size_t i = 0; i < app->count; i++) {
        const Icon* original = &app->icons[i];
        const Icon* shown = asset_packs_icon_acquire(original);
        if(shown != original) {
            printf("%s %s R %ux%u %u\n", when, app->names[i], shown->width, shown->height, shown->frame_count);
            asset_packs_icon_release();
        } else {
            printf("%s %s S\n", when, app->names[i]);
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

/* ---- fonts ---- */

static bool marker_exists(void) {
    char path[512];
    snprintf(path, sizeof(path), "%s/int/.asset_pack_loading", host_storage_root);
    return access(path, F_OK) == 0;
}

static size_t graveyard_count(void) {
    furi_mutex_acquire(asset_packs.mutex, FuriWaitForever);
    size_t count = 0;
    for(const FontBlock* b = asset_packs.graveyard; b; b = b->next) count++;
    furi_mutex_release(asset_packs.mutex);
    return count;
}

/** Bytes of Fonts/<slot>.u8f in the pack, or NULL. */
static uint8_t* read_pack_font(const char* pack, unsigned slot, size_t* size) {
    char path[512];
    snprintf(
        path,
        sizeof(path),
        "%s/ext/asset_packs/%s/Fonts/%s.u8f",
        host_storage_root,
        pack,
        asset_pack_font_slot_name(slot));
    FILE* f = fopen(path, "rb");
    if(!f) return NULL;
    uint8_t* data = malloc(ASSET_PACK_MAX_FONT_BYTES * 2);
    *size = fread(data, 1, ASSET_PACK_MAX_FONT_BYTES * 2, f);
    fclose(f);
    return data;
}

/** One line per slot: S for the built-in font, R for a pack font (same/diff against its file). */
static size_t report_fonts(const char* pack) {
    size_t replaced = 0;
    for(unsigned slot = 0; slot < ASSET_PACK_FONT_SLOTS; slot++) {
        const uint8_t* font = asset_packs_font(slot, stock_fonts[slot]);
        if(font == stock_fonts[slot]) {
            printf("F %u S\n", slot);
            continue;
        }
        replaced++;
        size_t size = 0;
        uint8_t* file = read_pack_font(pack, slot, &size);
        bool same = file && memcmp(font, file, size) == 0;
        printf("F %u R %s %zu\n", slot, same ? "same" : "diff", size);
        free(file);
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
        // A draw that picks its fonts, takes its time (the pack may be switched meanwhile) and
        // then reads them end to end. Pack fonts have the length of the built-in ones.
        for(unsigned slot = 0; slot < ASSET_PACK_FONT_SLOTS && stress_run; slot++) {
            const uint8_t* font = asset_packs_font(slot, stock_fonts[slot]);
            usleep(1500);
            unsigned sum = 0;
            for(size_t i = 0; i < stock_font_sizes[slot]; i++) sum += font[i];
            volatile unsigned keep = sum;
            (void)keep;
        }
    }
    compress_icon_free(decoder);
    return NULL;
}

// the firmware notes the pack's state for crash reports; nothing to note on the host
void furi_crash_report_note_pack(const char* text) {
    (void)text;
}

int main(int argc, char** argv) {
    if(argc < 3) return 2;
    const char* command = argv[1];
    host_storage_root = argv[2];
    host_log_enabled = getenv("HOST_LOG") != NULL;
    asset_packs_set_stock_fonts(stock_fonts);
    canvas_decoder = compress_icon_alloc(CANVAS_DECODE_BUFFER);
    int rc = 0;

    if(!strcmp(command, "verify") && argc == 4) {
        asset_packs_set_active(argv[3]);
        wait_for_loader();
        printf("loaded %zu\n", count_loaded());
        printf("apponly %zu\n", asset_packs.icon_count - count_loaded());
        report_all();
    } else if(!strcmp(command, "app") && argc >= 5) {
        asset_packs_set_active(argv[3]);
        wait_for_loader();
        FlipperApplication* app = calloc(1, sizeof(FlipperApplication));
        for(int i = 4; i < argc && app->count < FAKE_APP_MAX; i++) fake_app_add(app, argv[i]);
        asset_packs_app_loaded(app);
        report_app("run", app);
        asset_packs_app_unloaded();
        report_app("gone", app);
        // loaded again, then the pack's icons dropped while it runs (a pack switch)
        asset_packs_app_loaded(app);
        asset_packs_unload_icons();
        report_app("dropped", app);
        asset_packs_app_unloaded();
        free(app);
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
    } else if(!strcmp(command, "fonts") && argc == 4) {
        asset_packs_set_active(argv[3]);
        wait_for_loader();
        const size_t replaced = report_fonts(argv[3]);
        printf("replaced %zu\n", replaced);
        // Unloading the icons for an app must leave the fonts alone
        asset_packs_unload_icons();
        asset_packs_reload_icons();
        wait_for_loader();
        printf("after reload %zu\n", report_fonts(argv[3]));
        // Dropping the pack buries its fonts; the grace period frees them
        asset_packs_set_active(NULL);
        printf("after drop %zu buried %zu\n", report_fonts(argv[3]), graveyard_count());
        usleep((ASSET_PACKS_FONT_GRACE_MS + 400) * 1000);
        printf("later buried %zu\n", graveyard_count());
    } else if(!strcmp(command, "marker") && argc == 4) {
        asset_packs_set_active(argv[3]);
        wait_for_loader();
        printf("loaded %zu\n", asset_packs.icon_count);
        printf("after load %d\n", marker_exists());
        asset_packs_mark_stable();
        printf("after stable %d\n", marker_exists());
        asset_packs_unload_icons();
        asset_packs_reload_icons();
        wait_for_loader();
        printf("after reload %d\n", marker_exists());
        // chosen again, it proves itself again
        asset_packs_set_active(argv[3]);
        wait_for_loader();
        printf("after reselect %d\n", marker_exists());
        asset_packs_set_active(NULL);
        printf("after deselect %d\n", marker_exists());
    } else if(!strcmp(command, "appstart") && argc == 4) {
        // a finished loader keeps its thread (a 6 KB stack on the Flipper) until an app starts
        asset_packs_set_active(argv[3]);
        wait_for_loader();
        printf("loader kept %d\n", asset_packs.loader != NULL);
        asset_packs_app_starting();
        printf("loader after app start %d\n", asset_packs.loader != NULL);
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
    // Fonts buried by that leave the graveyard on their own, so nothing is left allocated
    for(int waited = 0; graveyard_count() > 0 && waited < 600; waited++) usleep(10000);
    if(graveyard_count() != 0) {
        printf("FAIL: fonts still in the graveyard\n");
        rc = 1;
    }
    compress_icon_free(canvas_decoder);
    return rc;
}
