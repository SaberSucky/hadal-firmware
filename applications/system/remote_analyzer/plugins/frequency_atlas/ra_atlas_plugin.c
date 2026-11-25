/**
 * @file ra_atlas_plugin.c
 * @brief Entry point of the Frequency Atlas .fal: the region, the band list and the details.
 */
#include "ra_atlas_plan.h"
#include "ra_atlas_scan.h"
#include "ra_atlas_view.h"
#include "../../helpers/ra_atlas_plugin.h"

#include <flipper_application/flipper_application.h>
#include <flipper_format/flipper_format.h>
#include <gui/modules/submenu.h>
#include <gui/modules/widget.h>
#include <storage/storage.h>
#include <furi_hal_subghz.h>
#include <furi.h>

#define TAG "RaAtlas"

#define RA_ATLAS_EXT         ".txt"
#define RA_ATLAS_REGIONS_MAX 16
#define RA_ATLAS_CODE_SIZE   16
#define RA_ATLAS_REGION_SIZE 8
/** Characters per details line: FontSecondary in the 124px the scroll element leaves */
#define RA_ATLAS_COLUMNS     24

/** One atlas at a time, and each scene entry maps a fresh copy of the plugin. */
static struct {
    RaAtlasEnv* env;
    Storage* storage;

    char* text; /**< the region file: the plan points into it */
    RaAtlasPlan* plan; /**< NULL until a region is loaded */
    char code[RA_ATLAS_CODE_SIZE]; /**< the region's file name, without .txt */
    char region[RA_ATLAS_REGION_SIZE]; /**< what the header calls it */
    RaAtlasScan* scan;
    RaAtlasRow rows[RA_ATLAS_SCAN_BANDS_MAX];

    RaAtlasView* list;
    Widget* details;
    Submenu* picker;
    FuriString* out; /**< what the details screen is showing */
    char codes[RA_ATLAS_REGIONS_MAX][RA_ATLAS_CODE_SIZE]; /**< the picker's entries */
    size_t code_count;
} ra_atlas;

/* ---- the region file ---- */

/**
 * Read and parse /ext/frequency_atlas/<code>.txt. A file that is missing, too big or not a
 * plan with a band in it is no region, and leaves @p plan and @p text untouched.
 */
static bool ra_atlas_load_file(const char* code, RaAtlasPlan* plan, char** text) {
    if(!ra_atlas_region_code_is_valid(code)) return false;

    FuriString* path = furi_string_alloc_printf("%s/%s%s", RA_ATLAS_DIR, code, RA_ATLAS_EXT);
    File* file = storage_file_alloc(ra_atlas.storage);
    char* buffer = NULL;
    bool ok = false;

    do {
        if(!storage_file_open(file, furi_string_get_cstr(path), FSAM_READ, FSOM_OPEN_EXISTING)) {
            break;
        }
        const uint64_t size = storage_file_size(file);
        if((size == 0) || (size > RA_ATLAS_FILE_MAX)) {
            FURI_LOG_W(TAG, "%s: %lu bytes", code, (unsigned long)size);
            break;
        }
        buffer = malloc((size_t)size + 1);
        if(storage_file_read(file, buffer, (size_t)size) != size) break;

        RaAtlasPlan* parsed = malloc(sizeof(RaAtlasPlan));
        ra_atlas_plan_parse(parsed, buffer, (size_t)size);
        if(ra_atlas_plan_is_valid(parsed)) {
            if(parsed->bad_lines) {
                FURI_LOG_W(TAG, "%s: %u lines skipped", code, (unsigned)parsed->bad_lines);
            }
            memcpy(plan, parsed, sizeof(RaAtlasPlan));
            *text = buffer;
            buffer = NULL;
            ok = true;
        }
        free(parsed);
    } while(false);

    free(buffer);
    storage_file_close(file);
    storage_file_free(file);
    furi_string_free(path);
    return ok;
}

/** The code Settings (or this picker, last time) chose. False when it is none or unreadable. */
static bool ra_atlas_saved_region(char* code, size_t size) {
    bool ok = false;
    FuriString* value = furi_string_alloc();
    FlipperFormat* file = flipper_format_file_alloc(ra_atlas.storage);
    if(flipper_format_file_open_existing(file, RA_ATLAS_REGION_PATH) &&
       flipper_format_read_string(file, RA_ATLAS_REGION_KEY, value)) {
        furi_string_trim(value);
        if(ra_atlas_region_code_is_valid(furi_string_get_cstr(value)) &&
           !furi_string_equal_str(value, RA_ATLAS_REGION_OFF)) {
            strlcpy(code, furi_string_get_cstr(value), size);
            ok = true;
        }
    }
    flipper_format_free(file);
    furi_string_free(value);
    return ok;
}

/** Same file and wording Settings > Hadal > Protocols writes, so either can read the other's. */
static void ra_atlas_save_region(const char* code) {
    storage_simply_mkdir(ra_atlas.storage, EXT_PATH("apps_data"));
    storage_simply_mkdir(ra_atlas.storage, EXT_PATH("apps_data/remote_analyzer"));
    File* file = storage_file_alloc(ra_atlas.storage);
    if(storage_file_open(file, RA_ATLAS_REGION_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        FuriString* text = furi_string_alloc_printf(
            "# Frequency Atlas region: Settings > Hadal > Protocols, or the atlas itself.\n"
            "# A file name in " RA_ATLAS_DIR " without .txt, or off.\n"
            "%s: %s\n",
            RA_ATLAS_REGION_KEY,
            code);
        storage_file_write(file, furi_string_get_cstr(text), furi_string_size(text));
        furi_string_free(text);
    }
    storage_file_close(file);
    storage_file_free(file);
}

/* ---- live state onto the screen ---- */

static void ra_atlas_tune_current(void) {
    if(!ra_atlas.plan) return;
    const size_t i = ra_atlas_scan_current(ra_atlas.scan);
    ra_atlas.env->tune(ra_atlas.env->context, ra_atlas.plan->bands[i].listen);
}

static void ra_atlas_refresh(void) {
    if(!ra_atlas.plan) return;
    const uint32_t now = furi_get_tick();
    const size_t current = ra_atlas_scan_current(ra_atlas.scan);

    for(size_t i = 0; i < ra_atlas.plan->count; i++) {
        RaAtlasRow* row = &ra_atlas.rows[i];
        const RaAtlasLive* live = ra_atlas_scan_live(ra_atlas.scan, i);
        row->label = ra_atlas.plan->bands[i].label;
        row->start = ra_atlas.plan->bands[i].start;
        row->end = ra_atlas.plan->bands[i].end;
        row->status = ra_atlas_scan_status(ra_atlas.scan, i, now);
        row->ago = ra_atlas_scan_seen_ago(ra_atlas.scan, i, now);
        row->listening = live->tunable && (i == current);
        strlcpy(row->protocol, live->decoded ? live->protocol : "", sizeof(row->protocol));
    }
    ra_atlas_view_update(
        ra_atlas.list,
        ra_atlas.rows,
        ra_atlas.plan->count,
        ra_atlas_scan_is_held(ra_atlas.scan),
        ra_atlas_scan_is_signal(ra_atlas.scan, now));
}

/** Make @p plan (and the @p text it points into) the one on screen, and start listening. */
static void ra_atlas_use(const char* code, RaAtlasPlan* plan, char* text) {
    // The rows point into the old text, so the list lets go of them before it is freed
    ra_atlas_view_reset(ra_atlas.list, "", 0);
    free(ra_atlas.text);
    if(ra_atlas.plan != plan) {
        if(!ra_atlas.plan) ra_atlas.plan = malloc(sizeof(RaAtlasPlan));
        memcpy(ra_atlas.plan, plan, sizeof(RaAtlasPlan));
    }
    ra_atlas.text = text;
    strlcpy(ra_atlas.code, code, sizeof(ra_atlas.code));

    // The file's own short name, else the file name
    const char* region = ra_atlas.plan->region;
    if(*region) {
        strlcpy(ra_atlas.region, region, sizeof(ra_atlas.region));
    } else {
        size_t i = 0;
        for(; code[i] && (i + 1 < sizeof(ra_atlas.region)); i++) {
            const char c = code[i];
            ra_atlas.region[i] = ((c >= 'a') && (c <= 'z')) ? (char)(c - 'a' + 'A') : c;
        }
        ra_atlas.region[i] = '\0';
    }

    bool tunable[RA_ATLAS_SCAN_BANDS_MAX];
    for(size_t i = 0; i < ra_atlas.plan->count; i++) {
        tunable[i] = furi_hal_subghz_is_frequency_valid(ra_atlas.plan->bands[i].listen);
    }
    const bool any = ra_atlas_scan_init(
        ra_atlas.scan,
        tunable,
        ra_atlas.plan->count,
        ra_atlas.env->trigger,
        furi_kernel_get_tick_frequency(),
        furi_get_tick());

    ra_atlas_view_reset(ra_atlas.list, ra_atlas.region, ra_atlas.plan->count);
    if(any) {
        ra_atlas_tune_current();
    } else {
        ra_atlas.env->tune(ra_atlas.env->context, 0);
    }
    ra_atlas_refresh();
}

/* ---- details ---- */

static void ra_atlas_cat_wrapped(FuriString* out, const char* text) {
    const size_t length = strlen(text);
    // At worst a break after every RA_ATLAS_COLUMNS characters, and one per space
    const size_t size = length * 2 + 2;
    char* wrapped = malloc(size);
    ra_atlas_wrap(text, RA_ATLAS_COLUMNS, wrapped, size);
    furi_string_cat_str(out, wrapped);
    furi_string_cat_str(out, "\n");
    free(wrapped);
}

static void ra_atlas_details(size_t index) {
    const RaAtlasBand* band = &ra_atlas.plan->bands[index];
    const RaAtlasLive* live = ra_atlas_scan_live(ra_atlas.scan, index);
    const uint32_t now = furi_get_tick();
    char start[12];
    char end[12];
    char listen[12];
    ra_atlas_format_mhz(band->start, start, sizeof(start));
    ra_atlas_format_mhz(band->end, end, sizeof(end));
    ra_atlas_format_mhz(band->listen, listen, sizeof(listen));

    FuriString* out = ra_atlas.out;
    // One leading marker bolds a line and nothing after it, which is the line's own end
    furi_string_printf(out, "\e#%s\n%s - %s MHz\n", band->label, start, end);

    switch(ra_atlas_scan_status(ra_atlas.scan, index, now)) {
    case RaAtlasStatusOutOfRange:
        furi_string_cat_printf(out, "Out of the radio's range\n(listens at %s)\n", listen);
        break;
    case RaAtlasStatusActive:
        furi_string_cat_printf(out, "Busy now, peak %d dBm\n", (int)live->rssi_peak);
        break;
    case RaAtlasStatusSeen:
        furi_string_cat_printf(
            out,
            "Heard %lus ago, peak %d dBm\n",
            (unsigned long)ra_atlas_scan_seen_ago(ra_atlas.scan, index, now),
            (int)live->rssi_peak);
        break;
    default:
        furi_string_cat_str(out, "Nothing heard yet\n");
        break;
    }
    if(live->tunable) {
        furi_string_cat_printf(
            out,
            "Listens at %s %s\n",
            listen,
            ra_atlas.env->modulation ? ra_atlas.env->modulation : "");
    }
    if(live->decoded) {
        furi_string_cat_printf(out, "Decoded: %s\n", live->protocol);
        if(live->note) ra_atlas_cat_wrapped(out, live->note);
    }

    if(*band->use) {
        furi_string_cat_str(out, "\e#Typical use\n");
        ra_atlas_cat_wrapped(out, band->use);
    }
    if(*band->limit) {
        furi_string_cat_str(out, "\e#Limit (info only)\n");
        ra_atlas_cat_wrapped(out, band->limit);
    }
    if(*ra_atlas.plan->source) {
        furi_string_cat_str(out, "\e#Source\n");
        ra_atlas_cat_wrapped(out, ra_atlas.plan->source);
    }
    if(*ra_atlas.plan->note) ra_atlas_cat_wrapped(out, ra_atlas.plan->note);

    widget_reset(ra_atlas.details);
    widget_add_text_scroll_element(ra_atlas.details, 0, 0, 128, 64, furi_string_get_cstr(out));
    view_dispatcher_switch_to_view(ra_atlas.env->view_dispatcher, ra_atlas.env->view_id_details);
}

/* ---- the region picker ---- */

static void ra_atlas_picker_callback(void* context, uint32_t index) {
    UNUSED(context);
    if(index >= ra_atlas.code_count) return;

    RaAtlasPlan* plan = malloc(sizeof(RaAtlasPlan));
    char* text = NULL;
    // Read again, not kept from the listing: only one region's text is ever in memory
    if(ra_atlas_load_file(ra_atlas.codes[index], plan, &text)) {
        ra_atlas_use(ra_atlas.codes[index], plan, text);
        ra_atlas_save_region(ra_atlas.code);
        view_dispatcher_switch_to_view(ra_atlas.env->view_dispatcher, ra_atlas.env->view_id_list);
    }
    free(plan);
}

/** Fill the picker with every region file that is a plan. @return how many there are. */
static size_t ra_atlas_picker_fill(void) {
    submenu_reset(ra_atlas.picker);
    submenu_set_header(ra_atlas.picker, "Frequency Atlas region");
    ra_atlas.code_count = 0;

    File* dir = storage_file_alloc(ra_atlas.storage);
    RaAtlasPlan* plan = malloc(sizeof(RaAtlasPlan));
    char name[64];
    FileInfo info;
    size_t selected = 0;

    if(storage_dir_open(dir, RA_ATLAS_DIR)) {
        while((ra_atlas.code_count < RA_ATLAS_REGIONS_MAX) &&
              storage_dir_read(dir, &info, name, sizeof(name))) {
            if(file_info_is_dir(&info)) continue;
            const size_t length = strlen(name);
            const size_t ext = strlen(RA_ATLAS_EXT);
            if((length <= ext) || (strcmp(name + length - ext, RA_ATLAS_EXT) != 0)) continue;
            name[length - ext] = '\0';
            if(!ra_atlas_region_code_is_valid(name)) continue;

            /* The directory stays open while each file is read: storage lets a directory
             * and a file be open together, it is two handles on one file that it does not. */
            char* text = NULL;
            if(!ra_atlas_load_file(name, plan, &text)) continue;

            const size_t index = ra_atlas.code_count++;
            strlcpy(ra_atlas.codes[index], name, RA_ATLAS_CODE_SIZE);
            FuriString* label = furi_string_alloc_set_str(*plan->name ? plan->name : name);
            submenu_add_item(
                ra_atlas.picker,
                furi_string_get_cstr(label),
                index,
                ra_atlas_picker_callback,
                NULL);
            furi_string_free(label);
            if(strcmp(name, ra_atlas.code) == 0) selected = index;
            free(text);
        }
    }
    storage_dir_close(dir);
    storage_file_free(dir);
    free(plan);

    if(ra_atlas.code_count == 0) {
        submenu_add_item(ra_atlas.picker, "No band plans on the SD", 0, NULL, NULL);
    }
    submenu_set_selected_item(ra_atlas.picker, selected);
    return ra_atlas.code_count;
}

static void ra_atlas_show_picker(void) {
    ra_atlas_picker_fill();
    view_dispatcher_switch_to_view(ra_atlas.env->view_dispatcher, ra_atlas.env->view_id_picker);
}

/* ---- navigation ---- */

/** Back on the details screen goes to the list, not out of the scene. */
static uint32_t ra_atlas_back_to_list(void* context) {
    UNUSED(context);
    return ra_atlas.env->view_id_list;
}

/** Back on the picker goes to the list when there is one, and out of the scene otherwise. */
static uint32_t ra_atlas_picker_back(void* context) {
    UNUSED(context);
    return ra_atlas.plan ? ra_atlas.env->view_id_list : VIEW_NONE;
}

static void ra_atlas_list_callback(RaAtlasViewEvent event, size_t index, void* context) {
    UNUSED(context);
    if(!ra_atlas.plan && (event != RaAtlasViewEventRegion)) return;
    const uint32_t now = furi_get_tick();

    switch(event) {
    case RaAtlasViewEventDetails:
        if(index < ra_atlas.plan->count) ra_atlas_details(index);
        break;
    case RaAtlasViewEventHold: {
        bool retune = false;
        if(ra_atlas_scan_hold(ra_atlas.scan, index, now, &retune) && retune) {
            ra_atlas_tune_current();
        }
        ra_atlas_refresh();
        break;
    }
    case RaAtlasViewEventRelease:
        ra_atlas_scan_release(ra_atlas.scan, now);
        ra_atlas_refresh();
        break;
    case RaAtlasViewEventRegion:
        ra_atlas_show_picker();
        break;
    }
}

/* ---- the plugin ---- */

static void ra_atlas_plugin_on_enter(RaAtlasEnv* env) {
    furi_assert(env);
    furi_assert(!ra_atlas.env);

    memset(&ra_atlas, 0, sizeof(ra_atlas));
    ra_atlas.env = env;
    ra_atlas.storage = furi_record_open(RECORD_STORAGE);
    ra_atlas.scan = malloc(sizeof(RaAtlasScan));
    ra_atlas.out = furi_string_alloc();

    ra_atlas.list = ra_atlas_view_alloc();
    ra_atlas.details = widget_alloc();
    ra_atlas.picker = submenu_alloc();
    ra_atlas_view_set_callback(ra_atlas.list, ra_atlas_list_callback, NULL);
    view_set_previous_callback(widget_get_view(ra_atlas.details), ra_atlas_back_to_list);
    view_set_previous_callback(submenu_get_view(ra_atlas.picker), ra_atlas_picker_back);

    view_dispatcher_add_view(
        env->view_dispatcher, env->view_id_list, ra_atlas_view_get_view(ra_atlas.list));
    view_dispatcher_add_view(
        env->view_dispatcher, env->view_id_details, widget_get_view(ra_atlas.details));
    view_dispatcher_add_view(
        env->view_dispatcher, env->view_id_picker, submenu_get_view(ra_atlas.picker));

    char code[RA_ATLAS_CODE_SIZE];
    RaAtlasPlan* plan = malloc(sizeof(RaAtlasPlan));
    char* text = NULL;
    if(ra_atlas_saved_region(code, sizeof(code)) && ra_atlas_load_file(code, plan, &text)) {
        ra_atlas_use(code, plan, text);
        view_dispatcher_switch_to_view(env->view_dispatcher, env->view_id_list);
    } else {
        // No region chosen, or its file is gone: ask, with the radio off meanwhile
        env->tune(env->context, 0);
        ra_atlas_show_picker();
    }
    free(plan);
}

static void ra_atlas_plugin_tick(RaAtlasEnv* env, float rssi) {
    UNUSED(env);
    if(!ra_atlas.plan) return;
    if(ra_atlas_scan_tick(ra_atlas.scan, rssi, furi_get_tick()) == RaAtlasActionRetune) {
        ra_atlas_tune_current();
    }
    ra_atlas_refresh();
}

static void ra_atlas_plugin_decoded(RaAtlasEnv* env, const char* protocol, const char* note) {
    UNUSED(env);
    if(!ra_atlas.plan) return;
    ra_atlas_scan_decoded(ra_atlas.scan, protocol, note, furi_get_tick());
    ra_atlas_refresh();
}

static void ra_atlas_plugin_on_exit(RaAtlasEnv* env) {
    furi_assert(env);
    furi_assert(ra_atlas.env == env);

    /* Park on an app-owned view before dropping ours: removing the current view leaves the
     * dispatcher with none, which stops the event loop for good. */
    view_dispatcher_switch_to_view(env->view_dispatcher, env->park_view_id);
    view_dispatcher_remove_view(env->view_dispatcher, env->view_id_picker);
    view_dispatcher_remove_view(env->view_dispatcher, env->view_id_details);
    view_dispatcher_remove_view(env->view_dispatcher, env->view_id_list);

    submenu_free(ra_atlas.picker);
    widget_free(ra_atlas.details);
    ra_atlas_view_free(ra_atlas.list);

    furi_string_free(ra_atlas.out);
    free(ra_atlas.scan);
    free(ra_atlas.plan);
    free(ra_atlas.text);
    furi_record_close(RECORD_STORAGE);

    memset(&ra_atlas, 0, sizeof(ra_atlas));
}

static const RaAtlasPlugin ra_atlas_plugin = {
    .on_enter = ra_atlas_plugin_on_enter,
    .tick = ra_atlas_plugin_tick,
    .decoded = ra_atlas_plugin_decoded,
    .on_exit = ra_atlas_plugin_on_exit,
};

static const FlipperAppPluginDescriptor ra_atlas_plugin_descriptor = {
    .appid = RA_ATLAS_PLUGIN_APP_ID,
    .ep_api_version = RA_ATLAS_PLUGIN_API_VERSION,
    .entry_point = &ra_atlas_plugin,
};

const FlipperAppPluginDescriptor* remote_analyzer_atlas_ep(void) {
    return &ra_atlas_plugin_descriptor;
}
