#include "asset_packs.h"
#include "asset_pack_format.h"

#include <assets_icon_index.h>
#include <furi.h>
#include <stdatomic.h>
#include <core/dangerous_defines.h>
#include <gui/icon_i.h>
#include <storage/storage.h>
#include <toolbox/compress.h>

#define TAG "AssetPacks"

/** Marker file present while a pack is loading; still there at boot means the load crashed. */
#define ASSET_PACKS_LOADING_MARKER "/int/.asset_pack_loading"

/* Directory recursion plus snprintf: kept generous, the stack is only held while loading */
#define LOADER_STACK_SIZE          (6 * 1024)
#define MAX_DIR_DEPTH              3u
#define NAME_BUFFER_SIZE           96u
#define PATH_SIZE                  128u
/** Largest icon file accepted: header, a stored frame and some slack for other packers. */
#define MAX_FILE_SIZE              (8u + 4u + ASSET_PACK_MAX_RAW_BYTES + 64u)
#define BLOB_ALIGN                 4u

typedef struct {
    const Icon* original; /**< built-in icon, the key */
    const Icon* replacement; /**< one malloc: Icon, frame pointers, frame blobs */
} PackIcon;

/** A loaded font. The font bytes u8g2 reads are `data`; `next` chains the graveyard. */
typedef struct FontBlock {
    struct FontBlock* next;
    uint32_t buried_tick;
    uint8_t data[];
} FontBlock;

typedef struct {
    FuriMutex* mutex;
    PackIcon* icons; /**< sorted by original; guarded by mutex */
    size_t icon_count;
    /** True when icons is non-empty. Written under the mutex, read without it by the draw hook. */
    atomic_bool active;
    /** Bumped whenever loaded icons are discarded, so a load that started earlier drops its result. */
    atomic_uint generation;
    /** Built-in font of each slot, from the canvas; the metrics pack fonts are checked against. */
    const uint8_t* const* stock_fonts;
    /** Pack font of each slot, NULL for the built-in one. Written under the mutex, read lock-free. */
    _Atomic(const uint8_t*) font_data[ASSET_PACK_FONT_SLOTS];
    FontBlock* fonts[ASSET_PACK_FONT_SLOTS]; /**< owner of each font_data; guarded by mutex */
    /**
     * Replaced fonts wait here before being freed: a draw that began just before the switch may
     * still be reading one. Guarded by mutex.
     */
    FontBlock* graveyard;
    FuriTimer* graveyard_timer;
    char name[ASSET_PACK_NAME_SIZE]; /**< active pack, empty for none */
    bool icons_unloaded;
    FuriThread* loader;
    AssetPacksChangedCallback changed_callback;
    void* changed_context;
} AssetPacks;

static AssetPacks asset_packs = {0};

/* ------------------------------------------------------------------------------------------ */
/* State                                                                                       */
/* ------------------------------------------------------------------------------------------ */

static void graveyard_timer_callback(void* context);

static void ensure_init(void) {
    if(!asset_packs.mutex) {
        asset_packs.mutex = furi_mutex_alloc(FuriMutexTypeNormal);
        asset_packs.graveyard_timer =
            furi_timer_alloc(graveyard_timer_callback, FuriTimerTypeOnce, NULL);
    }
}

static void free_icons(PackIcon* icons, size_t count) {
    for(size_t i = 0; i < count; i++) {
        free((void*)icons[i].replacement);
    }
    free(icons);
}

/** Drop the loaded icons. Caller holds the mutex. */
static void discard_icons_locked(void) {
    atomic_store_explicit(&asset_packs.active, false, memory_order_relaxed);
    PackIcon* icons = asset_packs.icons;
    size_t count = asset_packs.icon_count;
    asset_packs.icons = NULL;
    asset_packs.icon_count = 0;
    atomic_fetch_add_explicit(&asset_packs.generation, 1, memory_order_relaxed);
    free_icons(icons, count);
}

/** Free the graveyard blocks that have waited out the grace period. Caller holds the mutex. */
static void empty_graveyard_locked(void) {
    FontBlock** link = &asset_packs.graveyard;
    bool waiting = false;
    while(*link) {
        FontBlock* block = *link;
        if(furi_get_tick() - block->buried_tick >= ASSET_PACKS_FONT_GRACE_MS) {
            *link = block->next;
            free(block);
        } else {
            waiting = true;
            link = &block->next;
        }
    }
    if(waiting) {
        furi_timer_restart(
            asset_packs.graveyard_timer, furi_ms_to_ticks(ASSET_PACKS_FONT_GRACE_MS));
    }
}

static void graveyard_timer_callback(void* context) {
    UNUSED(context);
    furi_mutex_acquire(asset_packs.mutex, FuriWaitForever);
    empty_graveyard_locked();
    furi_mutex_release(asset_packs.mutex);
}

/** Stop using the pack fonts. They are freed later, see graveyard. Caller holds the mutex. */
static void discard_fonts_locked(void) {
    bool buried = false;
    for(size_t slot = 0; slot < ASSET_PACK_FONT_SLOTS; slot++) {
        FontBlock* block = asset_packs.fonts[slot];
        if(!block) continue;
        atomic_store_explicit(&asset_packs.font_data[slot], NULL, memory_order_release);
        asset_packs.fonts[slot] = NULL;
        block->buried_tick = furi_get_tick();
        block->next = asset_packs.graveyard;
        asset_packs.graveyard = block;
        buried = true;
    }
    if(buried) {
        furi_timer_restart(
            asset_packs.graveyard_timer, furi_ms_to_ticks(ASSET_PACKS_FONT_GRACE_MS));
    }
}

static void notify_changed(void) {
    if(asset_packs.changed_callback) {
        asset_packs.changed_callback(asset_packs.changed_context);
    }
}

void asset_packs_set_changed_callback(AssetPacksChangedCallback callback, void* context) {
    asset_packs.changed_callback = callback;
    asset_packs.changed_context = context;
}

/** A pack name must be a plain folder name. */
static bool name_is_valid(const char* name) {
    if(!name || !name[0] || name[0] == '.') return false;
    size_t length = strlen(name);
    if(length >= ASSET_PACK_NAME_SIZE) return false;
    for(size_t i = 0; i < length; i++) {
        if(name[i] == '/' || name[i] == '\\') return false;
    }
    return true;
}

void asset_packs_get_active(char* name, size_t size) {
    furi_check(name);
    furi_check(size > 0);
    ensure_init();
    furi_mutex_acquire(asset_packs.mutex, FuriWaitForever);
    strlcpy(name, asset_packs.name, size);
    furi_mutex_release(asset_packs.mutex);
}

/* ------------------------------------------------------------------------------------------ */
/* Drawing hook                                                                                */
/* ------------------------------------------------------------------------------------------ */

static const Icon* find_replacement_locked(const Icon* icon) {
    size_t low = 0, high = asset_packs.icon_count;
    while(low < high) {
        size_t mid = low + (high - low) / 2;
        const PackIcon* entry = &asset_packs.icons[mid];
        if(entry->original == icon) return entry->replacement;
        if((uintptr_t)entry->original < (uintptr_t)icon) {
            low = mid + 1;
        } else {
            high = mid;
        }
    }
    return NULL;
}

const Icon* asset_packs_icon_acquire(const Icon* icon) {
    // Lock-free fast path. A stale answer is harmless: stale "true" finds an empty table under
    // the lock, stale "false" draws the built-in icon once more.
    if(!atomic_load_explicit(&asset_packs.active, memory_order_relaxed)) return icon;

    furi_mutex_acquire(asset_packs.mutex, FuriWaitForever);
    const Icon* replacement = find_replacement_locked(icon);
    if(replacement) return replacement; // stays locked until asset_packs_icon_release()
    furi_mutex_release(asset_packs.mutex);
    return icon;
}

void asset_packs_icon_release(void) {
    furi_mutex_release(asset_packs.mutex);
}

void asset_packs_set_stock_fonts(const uint8_t* const* fonts) {
    asset_packs.stock_fonts = fonts;
}

const uint8_t* asset_packs_font(unsigned slot, const uint8_t* stock) {
    if(slot >= ASSET_PACK_FONT_SLOTS) return stock;
    const uint8_t* font = atomic_load_explicit(&asset_packs.font_data[slot], memory_order_acquire);
    return font ? font : stock;
}

/* ------------------------------------------------------------------------------------------ */
/* Loading                                                                                     */
/* ------------------------------------------------------------------------------------------ */

typedef struct {
    Storage* storage;
    Compress* compress;
    uint8_t* scratch; /**< ASSET_PACK_DECODE_BUFFER_SIZE, for validation decodes */
    unsigned generation; /**< stop if asset_packs.generation moves away from this */
    PackIcon* icons;
    size_t icon_count;
    size_t icon_capacity;
    size_t ram_used;
    FontBlock* fonts[ASSET_PACK_FONT_SLOTS]; /**< checked fonts, not yet installed */
    size_t font_count;
    size_t font_bytes;
    char pack_path[PATH_SIZE];
} LoadState;

static bool load_cancelled(const LoadState* state) {
    return atomic_load_explicit(&asset_packs.generation, memory_order_relaxed) !=
           state->generation;
}

static bool decode_for_check(
    const uint8_t* blob,
    size_t blob_size,
    uint8_t* out,
    size_t capacity,
    size_t* out_len,
    void* context) {
    // The same decoder compress_icon_decode() uses at draw time, so a frame that passes here
    // cannot fail there (which would crash the firmware).
    return compress_decode(context, (uint8_t*)blob, blob_size, out, capacity, out_len);
}

static const Icon* find_builtin_icon(const char* pack_path) {
    const uint32_t hash = asset_pack_path_hash(pack_path);
    size_t low = 0, high = assets_icon_index_count;
    while(low < high) {
        size_t mid = low + (high - low) / 2;
        const uint32_t entry = assets_icon_index[mid].path_hash;
        if(entry == hash) return assets_icon_index[mid].icon;
        if(entry < hash) {
            low = mid + 1;
        } else {
            high = mid;
        }
    }
    return NULL;
}

static bool already_loaded(const LoadState* state, const Icon* original) {
    for(size_t i = 0; i < state->icon_count; i++) {
        if(state->icons[i].original == original) return true;
    }
    return false;
}

/** Whole file into a new buffer; NULL if missing, empty or larger than max_size. */
static uint8_t* read_file(Storage* storage, const char* path, size_t max_size, size_t* size) {
    uint8_t* data = NULL;
    File* file = storage_file_alloc(storage);
    if(storage_file_open(file, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        uint64_t file_size = storage_file_size(file);
        if(file_size > 0 && file_size <= max_size) {
            data = malloc((size_t)file_size);
            if(storage_file_read(file, data, (size_t)file_size) != (size_t)file_size) {
                free(data);
                data = NULL;
            } else {
                *size = (size_t)file_size;
            }
        }
        storage_file_close(file);
    }
    storage_file_free(file);
    return data;
}

/** "base/name" into out; false if it does not fit (never truncates silently). */
static bool join_path(char* out, size_t size, const char* base, const char* name) {
    int n = snprintf(out, size, "%s/%s", base, name);
    return n >= 0 && (size_t)n < size;
}

static size_t align_up(size_t value) {
    return (value + BLOB_ALIGN - 1) & ~(size_t)(BLOB_ALIGN - 1);
}

/**
 * Take a checked icon into the load state. blobs/sizes describe the frames; their data is
 * copied into one allocation that holds the Icon, its frame pointer array and the frames.
 */
static void add_icon(
    LoadState* state,
    const Icon* original,
    const uint8_t* const* blobs,
    const size_t* sizes,
    size_t frame_count) {
    size_t blob_bytes = 0;
    for(size_t i = 0; i < frame_count; i++) {
        blob_bytes += align_up(sizes[i]);
    }
    const size_t pointers_size = frame_count * sizeof(uint8_t*);
    uint8_t* block = malloc(sizeof(Icon) + pointers_size + blob_bytes);
    Icon* icon = (Icon*)block;
    const uint8_t** frames = (const uint8_t**)(block + sizeof(Icon));
    uint8_t* cursor = block + sizeof(Icon) + pointers_size;
    for(size_t i = 0; i < frame_count; i++) {
        memcpy(cursor, blobs[i], sizes[i]);
        frames[i] = cursor;
        cursor += align_up(sizes[i]);
    }
    FURI_CONST_ASSIGN(icon->width, original->width);
    FURI_CONST_ASSIGN(icon->height, original->height);
    FURI_CONST_ASSIGN(icon->frame_count, original->frame_count);
    FURI_CONST_ASSIGN(icon->frame_rate, original->frame_rate);
    FURI_CONST_ASSIGN_PTR(icon->frames, frames);

    state->icons[state->icon_count].original = original;
    state->icons[state->icon_count].replacement = icon;
    state->icon_count++;
}

/** True if an icon with this many frames of this many blob bytes still fits the RAM budget. */
static bool take_budget(LoadState* state, size_t frame_count, size_t blob_bytes) {
    const size_t cost = asset_pack_icon_ram_cost(frame_count, blob_bytes);
    if(state->ram_used + cost > ASSET_PACK_ICON_RAM_BUDGET) return false;
    state->ram_used += cost;
    return true;
}

static void load_static_icon(LoadState* state, const char* file_path, const char* pack_path) {
    const Icon* original = find_builtin_icon(pack_path);
    if(!original || original->frame_count != 1 || already_loaded(state, original)) return;

    size_t size = 0;
    uint8_t* data = read_file(state->storage, file_path, MAX_FILE_SIZE, &size);
    if(!data) return;

    AssetPackBmx bmx;
    AssetPackStatus status = asset_pack_bmx_parse(data, size, &bmx);
    if(status == AssetPackOk && (bmx.width != original->width || bmx.height != original->height)) {
        status = AssetPackErrSize;
    }
    if(status == AssetPackOk) {
        status = asset_pack_blob_check(
            bmx.blob,
            bmx.blob_size,
            bmx.width,
            bmx.height,
            decode_for_check,
            state->compress,
            state->scratch);
    }
    if(status != AssetPackOk) {
        FURI_LOG_W(TAG, "%s: skipped (%d)", pack_path, status);
    } else if(!take_budget(state, 1, bmx.blob_size)) {
        FURI_LOG_W(TAG, "%s: skipped, icon RAM budget used up", pack_path);
    } else {
        const uint8_t* blobs[1] = {bmx.blob};
        const size_t sizes[1] = {bmx.blob_size};
        add_icon(state, original, blobs, sizes, 1);
    }
    free(data);
}

static void load_animated_icon(LoadState* state, const char* dir_path, const char* pack_path) {
    const Icon* original = find_builtin_icon(pack_path);
    if(!original || original->frame_count < 2 || already_loaded(state, original)) return;

    char path[PATH_SIZE];
    if(!join_path(path, sizeof(path), dir_path, "meta")) return;
    size_t meta_size = 0;
    uint8_t* meta_data = read_file(state->storage, path, 16, &meta_size);
    if(!meta_data) return;
    AssetPackAnimMeta meta;
    AssetPackStatus status = asset_pack_anim_meta_parse(meta_data, meta_size, &meta);
    free(meta_data);
    if(status != AssetPackOk || meta.width != original->width ||
       meta.height != original->height || meta.frame_count != original->frame_count ||
       meta.frame_count > ASSET_PACK_MAX_ICON_FRAMES) {
        FURI_LOG_W(TAG, "%s: skipped, meta does not match the built-in icon", pack_path);
        return;
    }

    const size_t frame_count = meta.frame_count;
    uint8_t* frames[ASSET_PACK_MAX_ICON_FRAMES] = {0};
    size_t sizes[ASSET_PACK_MAX_ICON_FRAMES] = {0};
    size_t blob_bytes = 0;
    bool ok = true;

    for(size_t i = 0; i < frame_count && ok; i++) {
        char frame_name[16];
        snprintf(frame_name, sizeof(frame_name), "frame_%02u.bm", (unsigned)i);
        ok = join_path(path, sizeof(path), dir_path, frame_name);
        if(ok) frames[i] = read_file(state->storage, path, MAX_FILE_SIZE, &sizes[i]);
        ok = ok && frames[i] &&
             asset_pack_blob_check(
                 frames[i],
                 sizes[i],
                 meta.width,
                 meta.height,
                 decode_for_check,
                 state->compress,
                 state->scratch) == AssetPackOk;
        blob_bytes += sizes[i];
        // Stop reading as soon as the icon cannot fit, instead of holding all its frames first
        if(ok && state->ram_used + asset_pack_icon_ram_cost(frame_count, blob_bytes) >
                     ASSET_PACK_ICON_RAM_BUDGET) {
            break;
        }
    }

    if(!ok) {
        FURI_LOG_W(TAG, "%s: skipped, a frame is missing or invalid", pack_path);
    } else if(!take_budget(state, frame_count, blob_bytes)) {
        FURI_LOG_W(TAG, "%s: skipped, icon RAM budget used up", pack_path);
    } else {
        add_icon(state, original, (const uint8_t* const*)frames, sizes, frame_count);
    }
    for(size_t i = 0; i < frame_count; i++) {
        free(frames[i]);
    }
}

static bool has_suffix(const char* text, const char* suffix) {
    size_t text_len = strlen(text), suffix_len = strlen(suffix);
    return text_len >= suffix_len && strcmp(text + text_len - suffix_len, suffix) == 0;
}

/**
 * Walk dir_path (a folder under Icons/); pack_prefix is its path relative to Icons/.
 * A folder holding a `meta` file is an animated icon, any other folder is a category.
 */
static void scan_directory(
    LoadState* state,
    const char* dir_path,
    const char* pack_prefix,
    unsigned depth) {
    if(load_cancelled(state) || depth > MAX_DIR_DEPTH) return;

    if(pack_prefix[0]) {
        char meta_path[PATH_SIZE];
        if(join_path(meta_path, sizeof(meta_path), dir_path, "meta") &&
           storage_common_exists(state->storage, meta_path)) {
            load_animated_icon(state, dir_path, pack_prefix);
            return;
        }
    }

    File* dir = storage_file_alloc(state->storage);
    if(storage_dir_open(dir, dir_path)) {
        FileInfo info;
        char name[NAME_BUFFER_SIZE];
        while(!load_cancelled(state) && storage_dir_read(dir, &info, name, sizeof(name))) {
            if(strlen(name) + 1 >= sizeof(name)) continue; // possibly truncated
            char child_path[PATH_SIZE];
            char child_pack[PATH_SIZE];
            if(!join_path(child_path, sizeof(child_path), dir_path, name)) continue;
            if(file_info_is_dir(&info)) {
                bool fits = pack_prefix[0] ?
                                join_path(child_pack, sizeof(child_pack), pack_prefix, name) :
                                strlcpy(child_pack, name, sizeof(child_pack)) <
                                    sizeof(child_pack);
                if(!fits) continue;
                scan_directory(state, child_path, child_pack, depth + 1);
            } else if(pack_prefix[0] && has_suffix(name, ".bmx")) {
                char stem[NAME_BUFFER_SIZE];
                size_t stem_length = strlen(name) - 4;
                memcpy(stem, name, stem_length);
                stem[stem_length] = '\0';
                if(!join_path(child_pack, sizeof(child_pack), pack_prefix, stem)) continue;
                load_static_icon(state, child_path, child_pack);
            }
        }
        storage_dir_close(dir);
    }
    storage_file_free(dir);
}

/** Read, check and take the pack's Fonts/<Slot>.u8f files. */
static void load_fonts(LoadState* state, const char* fonts_path) {
    if(!asset_packs.stock_fonts) return;

    for(size_t slot = 0; slot < ASSET_PACK_FONT_SLOTS && !load_cancelled(state); slot++) {
        char file_name[32], path[PATH_SIZE];
        snprintf(file_name, sizeof(file_name), "%s.u8f", asset_pack_font_slot_name(slot));
        if(!join_path(path, sizeof(path), fonts_path, file_name)) continue;

        size_t size = 0;
        uint8_t* data = read_file(state->storage, path, ASSET_PACK_MAX_FONT_BYTES, &size);
        if(!data) continue;

        AssetPackFontMetrics expected;
        AssetPackStatus status = AssetPackErrTooShort;
        if(asset_pack_font_metrics(asset_packs.stock_fonts[slot], ASSET_PACK_FONT_HEADER_SIZE, &expected)) {
            status = asset_pack_font_check(data, size, &expected);
        }
        if(status != AssetPackOk) {
            FURI_LOG_W(TAG, "%s: skipped (%d)", file_name, status);
        } else if(state->font_bytes + size > ASSET_PACK_FONT_RAM_BUDGET) {
            FURI_LOG_W(TAG, "%s: skipped, font RAM budget used up", file_name);
        } else {
            FontBlock* block = malloc(sizeof(FontBlock) + size);
            memcpy(block->data, data, size);
            state->fonts[slot] = block;
            state->font_count++;
            state->font_bytes += size;
        }
        free(data);
    }
}

/** Lookup is by the original icon's address, so keep the table sorted by it. */
static void sort_icons(PackIcon* icons, size_t count) {
    for(size_t i = 1; i < count; i++) {
        PackIcon entry = icons[i];
        size_t j = i;
        while(j > 0 && (uintptr_t)icons[j - 1].original > (uintptr_t)entry.original) {
            icons[j] = icons[j - 1];
            j--;
        }
        icons[j] = entry;
    }
}

typedef struct {
    unsigned generation;
    bool load_fonts; /**< false when only the icons come back after an app */
    char name[ASSET_PACK_NAME_SIZE];
} LoaderContext;

static void write_marker(Storage* storage) {
    File* file = storage_file_alloc(storage);
    if(storage_file_open(file, ASSET_PACKS_LOADING_MARKER, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        storage_file_close(file);
    }
    storage_file_free(file);
}

static int32_t loader_thread(void* context) {
    LoaderContext* loader = context;
    LoadState state = {.generation = loader->generation};
    const uint32_t started = furi_get_tick();

    state.storage = furi_record_open(RECORD_STORAGE);
    bool path_ok = join_path(state.pack_path, sizeof(state.pack_path), ASSET_PACKS_PATH, loader->name);

    // Present for the whole load: if the firmware dies in here, the next boot sees it and
    // skips the pack instead of crashing again.
    write_marker(state.storage);

    char icons_path[PATH_SIZE];
    if(path_ok && join_path(icons_path, sizeof(icons_path), state.pack_path, "Icons") &&
       storage_common_exists(state.storage, icons_path)) {
        state.compress = compress_alloc(CompressTypeHeatshrink, &compress_config_heatshrink_default);
        state.scratch = malloc(ASSET_PACK_DECODE_BUFFER_SIZE);
        state.icon_capacity = assets_icon_index_count; // an icon can be replaced at most once
        state.icons = malloc(sizeof(PackIcon) * state.icon_capacity);

        scan_directory(&state, icons_path, "", 0);

        compress_free(state.compress);
        free(state.scratch);
    } else {
        FURI_LOG_I(TAG, "%s has no icons", loader->name);
    }

    char fonts_path[PATH_SIZE];
    if(loader->load_fonts && path_ok &&
       join_path(fonts_path, sizeof(fonts_path), state.pack_path, "Fonts") &&
       storage_common_exists(state.storage, fonts_path)) {
        load_fonts(&state, fonts_path);
    }

    sort_icons(state.icons, state.icon_count);

    furi_mutex_acquire(asset_packs.mutex, FuriWaitForever);
    bool installed = false;
    if(!load_cancelled(&state) && state.font_count > 0) {
        discard_fonts_locked(); // nothing should be there; never leak or double-own if something is
        for(size_t slot = 0; slot < ASSET_PACK_FONT_SLOTS; slot++) {
            if(!state.fonts[slot]) continue;
            asset_packs.fonts[slot] = state.fonts[slot];
            atomic_store_explicit(
                &asset_packs.font_data[slot], state.fonts[slot]->data, memory_order_release);
            state.fonts[slot] = NULL;
        }
        installed = true;
    }
    if(!load_cancelled(&state) && state.icon_count > 0) {
        PackIcon* old = asset_packs.icons;
        size_t old_count = asset_packs.icon_count;
        asset_packs.icons = state.icons;
        asset_packs.icon_count = state.icon_count;
        atomic_store_explicit(&asset_packs.active, true, memory_order_relaxed);
        free_icons(old, old_count); // nothing is drawing: the mutex is held
        state.icons = NULL;
        installed = true;
    }
    furi_mutex_release(asset_packs.mutex);

    if(state.icons) free_icons(state.icons, state.icon_count);
    for(size_t slot = 0; slot < ASSET_PACK_FONT_SLOTS; slot++) {
        free(state.fonts[slot]); // only those that were not installed are still here
    }

    storage_simply_remove(state.storage, ASSET_PACKS_LOADING_MARKER);
    furi_record_close(RECORD_STORAGE);

    FURI_LOG_I(
        TAG,
        "%s: %u icons, %u B, %u fonts, %u B, %lu ms%s",
        loader->name,
        (unsigned)state.icon_count,
        (unsigned)state.ram_used,
        (unsigned)state.font_count,
        (unsigned)state.font_bytes,
        (unsigned long)(furi_get_tick() - started),
        installed ? "" : " (not installed)");

    if(installed) notify_changed();
    free(loader);
    return 0;
}

/** Start a background load of the active pack. At most one loader runs: wait for the last. */
static void start_loader(bool load_fonts) {
    if(asset_packs.loader) {
        furi_thread_join(asset_packs.loader);
        furi_thread_free(asset_packs.loader);
        asset_packs.loader = NULL;
    }
    if(!asset_packs.name[0]) return;

    LoaderContext* loader = malloc(sizeof(LoaderContext));
    loader->generation = atomic_load_explicit(&asset_packs.generation, memory_order_relaxed);
    loader->load_fonts = load_fonts;
    strlcpy(loader->name, asset_packs.name, sizeof(loader->name));

    asset_packs.loader =
        furi_thread_alloc_ex("AssetPackLoader", LOADER_STACK_SIZE, loader_thread, loader);
    furi_thread_set_priority(asset_packs.loader, FuriThreadPriorityLow);
    furi_thread_start(asset_packs.loader);
}

/* ------------------------------------------------------------------------------------------ */
/* Public control                                                                              */
/* ------------------------------------------------------------------------------------------ */

void asset_packs_set_active(const char* name) {
    ensure_init();

    furi_mutex_acquire(asset_packs.mutex, FuriWaitForever);
    asset_packs.name[0] = '\0';
    if(name_is_valid(name)) strlcpy(asset_packs.name, name, sizeof(asset_packs.name));
    asset_packs.icons_unloaded = false;
    discard_icons_locked(); // also cancels a load that is still running
    discard_fonts_locked();
    furi_mutex_release(asset_packs.mutex);

    notify_changed();
    start_loader(true);
}

void asset_packs_unload_icons(void) {
    ensure_init();

    furi_mutex_acquire(asset_packs.mutex, FuriWaitForever);
    asset_packs.icons_unloaded = true;
    discard_icons_locked();
    furi_mutex_release(asset_packs.mutex);
}

void asset_packs_reload_icons(void) {
    ensure_init();

    furi_mutex_acquire(asset_packs.mutex, FuriWaitForever);
    bool reload = asset_packs.icons_unloaded && asset_packs.name[0];
    asset_packs.icons_unloaded = false;
    // Fonts stay loaded through unload/reload; they only come back if the unload cut a load short
    bool fonts_missing = true;
    for(size_t slot = 0; slot < ASSET_PACK_FONT_SLOTS; slot++) {
        if(asset_packs.fonts[slot]) fonts_missing = false;
    }
    furi_mutex_release(asset_packs.mutex);

    if(reload) start_loader(fonts_missing);
}

bool asset_packs_previous_load_crashed(void) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    bool crashed = storage_common_exists(storage, ASSET_PACKS_LOADING_MARKER);
    if(crashed) storage_simply_remove(storage, ASSET_PACKS_LOADING_MARKER);
    furi_record_close(RECORD_STORAGE);
    return crashed;
}
