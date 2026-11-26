#include "../hadal_settings_i.h"
#include "../subghz_freqs.h"

#include <furi_hal_subghz.h>

/* The user's own Sub-GHz frequencies: the firmware's list or not, the region's band plan, a list
 * to add to, and which of them the hopper visits. Sub-GHz reads them when it opens.
 *
 * One list, every row changed where it is, as Protocols' rows are:
 *   Built-in list        a switch: the firmware's own frequencies kept, or left out
 *   Use UK frequencies   only while Protocols > Region names one, and only after a confirmation:
 *                        the hopper on its bands alone. Nothing is deleted: the frequencies that
 *                        stop hopping stay rows, one press from hopping again
 *   Add a frequency      the number input
 *   433.920 MHz          Left and Right step No hop / Hop; OK turns it over. One step left of
 *                        No hop is "Remove", which only OK then carries out: removing takes two
 *                        presses that each say what they do, never one stray key. Right, or any
 *                        other row's change, takes the Remove back.
 *                        ("Remove", not "Remove?": the question mark is wider than the value's
 *                        slot, and the text would scroll.) */

#define FREQS_FILE_MAX (8192U)
#define FREQS_MAX      (64U)
#define ROWS_MAX       (FREQS_MAX + 4U)
#define LABEL_SIZE     (16U)

typedef enum {
    RowStandard,
    RowRegion,
    RowAdd,
    RowNote, // why Sub-GHz would pass the whole file by
    RowFreq,
    RowNone,
} RowKind;

enum {
    EventEntered = 1,
    EventRegionUse,
    EventDialogBack,
    EventStandardBase = 0x100, // + the switch's value
    EventValueBase = 0x1000, // + row: Left or Right moved a frequency's value
    EventOkBase = 0x2000, // + row: OK on a row that leaves OK to the app
};

/* A frequency row's values, left to right */
enum {
    FreqRemove,
    FreqNoHop,
    FreqHop,
    FreqValues,
};

static const char* const off_on[] = {"Off", "On"};

// The list shown: the frequencies, then those that only hop; and which of them hop
typedef struct {
    uint32_t hz[FREQS_MAX];
    bool hops[FREQS_MAX];
    bool listed[FREQS_MAX]; /* a "Frequency:" line, not only a hopper one */
    size_t count;
    size_t hopper_count;
} FreqsShown;

/** What a row is, for the change callbacks: they run with the list locked, so cannot ask it */
typedef struct {
    HadalSettings* app;
    uint8_t row;
} RowRef;

static struct {
    FreqsShown shown;
    uint8_t kinds[ROWS_MAX];
    uint8_t first_freq; /**< the row of shown.hz[0] */
    uint8_t rows;
    RowRef refs[ROWS_MAX];
    char labels[FREQS_MAX][LABEL_SIZE];
    char region_label[32];
    char region_locked[48];

    bool region_on; /**< Protocols > Region names one */
    bool region_ok; /**< and its band plan is on the card */
    HadalRegionPlan region;

    int16_t armed; /**< the row showing Remove, or -1 */
    bool note_shown; /**< whether the list was built with the note */
    bool in_sub_view; /**< the number input or a dialog is over the list */
} freqs;

static void freqs_collect(const char* text, FreqsShown* shown) {
    uint32_t hopper[FREQS_MAX];
    shown->hopper_count = subghz_freqs_list_hopper(text, hopper, FREQS_MAX);
    shown->count = subghz_freqs_list(text, shown->hz, FREQS_MAX);
    for(size_t i = 0; i < shown->count; i++) {
        shown->listed[i] = true;
        shown->hops[i] = false;
        for(size_t h = 0; h < shown->hopper_count; h++) {
            if(hopper[h] == shown->hz[i]) shown->hops[i] = true;
        }
    }
    for(size_t h = 0; h < shown->hopper_count && shown->count < FREQS_MAX; h++) {
        bool seen = false;
        for(size_t i = 0; i < shown->count; i++) {
            if(shown->hz[i] == hopper[h]) seen = true;
        }
        if(seen) continue;
        shown->hz[shown->count] = hopper[h];
        shown->hops[shown->count] = true;
        shown->listed[shown->count] = false;
        shown->count++;
    }
}

static char* freqs_read(Storage* storage) {
    File* file = storage_file_alloc(storage);
    char* text = NULL;
    if(storage_file_open(file, SUBGHZ_FREQS_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        const uint64_t size = storage_file_size(file);
        if(size < FREQS_FILE_MAX) {
            text = malloc(size + 1);
            text[storage_file_read(file, text, size)] = '\0';
        }
    } else {
        text = strdup("");
    }
    storage_file_free(file);
    return text;
}

/** Writes and frees @p text. False when it was NULL (out of memory) or the card refused it */
static bool freqs_write(Storage* storage, char* text) {
    if(!text) return false;
    bool written = false;
    File* file = storage_file_alloc(storage);
    if(storage_file_open(file, SUBGHZ_FREQS_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        const size_t length = strlen(text);
        written = storage_file_write(file, text, length) == length;
    }
    storage_file_free(file);
    free(text);
    return written;
}

/** With the built-in list left out, Sub-GHz needs a frequency and a hopper one of the file's,
 * or it uses its own lists and ignores this file (subghz_setting_load()) */
static const char* freqs_note(const char* text, const FreqsShown* shown) {
    if(subghz_freqs_standard(text)) return NULL;
    size_t listed = 0;
    for(size_t i = 0; i < shown->count; i++) {
        if(shown->listed[i]) listed++;
    }
    if(!listed) return "Unused: none listed";
    if(!shown->hopper_count) return "Unused: none hop";
    return NULL;
}

static bool freqs_region_in_use(const char* text) {
    return freqs.region_ok && freqs.region.count && !subghz_freqs_standard(text) &&
           !subghz_freqs_region_changes(text, freqs.region.hz, freqs.region.count);
}

static const char* freq_value_text(size_t index, uint8_t value) {
    if(value == FreqRemove) return "Remove";
    if(value == FreqNoHop) return "No hop";
    return freqs.shown.listed[index] ? "Hop" : "Hop only";
}

static void standard_changed(VariableItem* item) {
    RowRef* ref = variable_item_get_context(item);
    const uint8_t index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, off_on[index]);
    view_dispatcher_send_custom_event(ref->app->view_dispatcher, EventStandardBase + index);
}

static void freq_changed(VariableItem* item) {
    RowRef* ref = variable_item_get_context(item);
    const uint8_t index = variable_item_get_current_value_index(item);
    const size_t freq = ref->row - freqs.first_freq;
    variable_item_set_current_value_text(item, freq_value_text(freq, index));
    view_dispatcher_send_custom_event(ref->app->view_dispatcher, EventValueBase + ref->row);
}

static void entered(void* context, int32_t number) {
    HadalSettings* app = context;
    // kHz in, Hz kept
    scene_manager_set_scene_state(app->scene_manager, HadalSettingsSceneSubghzFreqs, number);
    view_dispatcher_send_custom_event(app->view_dispatcher, EventEntered);
}

static void enter_callback(void* context, uint32_t index) {
    HadalSettings* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, EventOkBase + index);
}

static VariableItem* freqs_add_row(
    HadalSettings* app,
    RowKind kind,
    const char* label,
    uint8_t values,
    VariableItemChangeCallback callback) {
    const uint8_t row = freqs.rows++;
    freqs.kinds[row] = kind;
    freqs.refs[row] = (RowRef){.app = app, .row = row};
    return variable_item_list_add(app->variable_list, label, values, callback, &freqs.refs[row]);
}

/** The list from the file, the cursor back on the row it was on (the same frequency, or the
 * same place when that one is gone) */
static void freqs_show(HadalSettings* app) {
    // what the cursor is on, before the rows go
    const uint8_t selected = variable_item_list_get_selected_item_index(app->variable_list);
    const RowKind selected_kind = selected < freqs.rows ? freqs.kinds[selected] : RowNone;
    const uint32_t selected_hz = selected_kind == RowFreq ?
                                     freqs.shown.hz[selected - freqs.first_freq] :
                                     0;

    freqs.in_sub_view = false;
    freqs.armed = -1;
    freqs.rows = 0;
    freqs.first_freq = 0;
    freqs.note_shown = false;
    variable_item_list_reset(app->variable_list);
    variable_item_list_set_header(app->variable_list, "Frequencies");
    variable_item_list_set_enter_callback(app->variable_list, enter_callback, app);

    char* text = freqs_read(app->storage);
    if(!text) {
        freqs.shown.count = 0;
        freqs_add_row(app, RowNote, "File too big to edit", 0, NULL);
        view_dispatcher_switch_to_view(app->view_dispatcher, HadalViewVariableList);
        return;
    }
    freqs_collect(text, &freqs.shown);

    const char* note = freqs_note(text, &freqs.shown);
    if(note) {
        freqs_add_row(app, RowNote, note, 0, NULL);
        freqs.note_shown = true;
    }

    const bool standard = subghz_freqs_standard(text);
    VariableItem* item = freqs_add_row(app, RowStandard, "Built-in list", 2, standard_changed);
    variable_item_set_current_value_index(item, standard);
    variable_item_set_current_value_text(item, off_on[standard]);
    variable_item_set_values_text(item, off_on);

    // Region Off: no row, and the screen is what it always was
    if(freqs.region_on) {
        item = freqs_add_row(app, RowRegion, freqs.region_label, 0, NULL);
        variable_item_set_opens(item, true);
        if(!freqs.region_ok) {
            variable_item_set_locked(item, true, freqs.region_locked);
        } else if(freqs_region_in_use(text)) {
            variable_item_set_current_value_text(item, "In use");
        }
    }

    item = freqs_add_row(app, RowAdd, "Add a frequency", 0, NULL);
    variable_item_set_opens(item, true);

    freqs.first_freq = freqs.rows;
    for(size_t i = 0; i < freqs.shown.count; i++) {
        snprintf(
            freqs.labels[i],
            LABEL_SIZE,
            "%lu.%03lu MHz",
            (unsigned long)(freqs.shown.hz[i] / 1000000),
            (unsigned long)(freqs.shown.hz[i] / 1000 % 1000));
        const uint8_t value = freqs.shown.hops[i] ? FreqHop : FreqNoHop;
        item = freqs_add_row(app, RowFreq, freqs.labels[i], FreqValues, freq_changed);
        variable_item_set_current_value_index(item, value);
        variable_item_set_current_value_text(item, freq_value_text(i, value));
    }
    free(text);

    // back on the row the cursor was on
    uint8_t select = selected < freqs.rows ? selected : (freqs.rows ? freqs.rows - 1 : 0);
    for(uint8_t row = 0; row < freqs.rows; row++) {
        if(freqs.kinds[row] != selected_kind) continue;
        if(selected_kind == RowFreq && freqs.shown.hz[row - freqs.first_freq] != selected_hz) {
            continue;
        }
        select = row;
        break;
    }
    variable_item_list_set_selected_item(app->variable_list, select);
    view_dispatcher_switch_to_view(app->view_dispatcher, HadalViewVariableList);
}

static void dialog_callback(DialogExResult result, void* context) {
    HadalSettings* app = context;
    if(result == DialogExResultRight) {
        view_dispatcher_send_custom_event(app->view_dispatcher, EventRegionUse);
    } else if(result == DialogExResultLeft) {
        view_dispatcher_send_custom_event(app->view_dispatcher, EventDialogBack);
    }
}

/** A dialog over the list; Back, or Left when there is a @p left, goes back to it */
static void freqs_dialog(
    HadalSettings* app,
    const char* header,
    const char* text,
    const char* left,
    const char* right) {
    dialog_ex_reset(app->dialog);
    dialog_ex_set_header(app->dialog, header, 64, 0, AlignCenter, AlignTop);
    dialog_ex_set_text(app->dialog, text, 64, 12, AlignCenter, AlignTop);
    if(left) dialog_ex_set_left_button_text(app->dialog, left);
    if(right) dialog_ex_set_right_button_text(app->dialog, right);
    dialog_ex_set_result_callback(app->dialog, dialog_callback);
    dialog_ex_set_context(app->dialog, app);
    freqs.in_sub_view = true;
    view_dispatcher_switch_to_view(app->view_dispatcher, HadalViewDialog);
}

static void freqs_write_failed(HadalSettings* app) {
    freqs_dialog(
        app, "Not saved", "The SD card refused\nsubghz/assets/\nsetting_user", "Back", NULL);
}

/** What "Use UK frequencies" will do, asked before it is done */
static void freqs_region_ask(HadalSettings* app, const char* text) {
    static char header[40];
    static char body[112];
    const HadalRegionPlan* plan = &freqs.region;
    if(!plan->count) {
        snprintf(header, sizeof(header), "%s frequencies", plan->name);
        freqs_dialog(
            app,
            header,
            "None of its bands are\nin the radio's range\n(Extended Sub-GHz)",
            "Back",
            NULL);
    } else if(freqs_region_in_use(text)) {
        snprintf(header, sizeof(header), "%s frequencies", plan->name);
        snprintf(
            body,
            sizeof(body),
            "The hopper visits its %u\nbands and nothing else.\nThe built-in list is\nleft out.",
            (unsigned)plan->count);
        freqs_dialog(app, header, body, "Back", NULL);
    } else {
        // Nothing is deleted: what stops hopping stays a row, one press from hopping again
        snprintf(header, sizeof(header), "Use %s frequencies?", plan->name);
        snprintf(
            body,
            sizeof(body),
            "Hops its %u bands only.\nYour other frequencies\nstay, without hopping.\n"
            "Built-in list left out.",
            (unsigned)plan->count);
        freqs_dialog(app, header, body, "Cancel", "Use");
    }
}

/** Put a Remove back to what the frequency is, unless it is on @p keep */
static void freqs_disarm(HadalSettings* app, int16_t keep) {
    if(freqs.armed < 0 || freqs.armed == keep) return;
    const size_t index = freqs.armed - freqs.first_freq;
    VariableItem* item = variable_item_list_get(app->variable_list, freqs.armed);
    if(item && index < freqs.shown.count) {
        const uint8_t value = freqs.shown.hops[index] ? FreqHop : FreqNoHop;
        variable_item_set_current_value_index(item, value);
        variable_item_set_current_value_text(item, freq_value_text(index, value));
    }
    freqs.armed = -1;
}

/** A frequency row moved to @p value: Remove waits for OK, the others are written at once */
static void freqs_set_value(HadalSettings* app, uint8_t row, uint8_t value, const char* text) {
    const size_t index = row - freqs.first_freq;
    const uint32_t hz = freqs.shown.hz[index];
    VariableItem* item = variable_item_list_get(app->variable_list, row);
    if(value == FreqRemove) {
        freqs.armed = row;
        return;
    }
    if(freqs.armed == row) freqs.armed = -1;

    const bool hop = value == FreqHop;
    char* edited = NULL;
    if(hop && !freqs.shown.hops[index]) {
        edited = subghz_freqs_with_hopper(text, hz);
    } else if(!hop && freqs.shown.hops[index]) {
        if(freqs.shown.listed[index]) {
            edited = subghz_freqs_without_hopper(text, hz);
        } else {
            // only a hopper line: listed instead, so passing it by does not lose it
            char* listed = subghz_freqs_with(text, hz);
            edited = listed ? subghz_freqs_without_hopper(listed, hz) : NULL;
            free(listed);
        }
    } else {
        return; // already so: Right off a Remove, back to what it never stopped being
    }
    if(!freqs_write(app->storage, edited)) {
        freqs_write_failed(app);
        return;
    }
    freqs.shown.hops[index] = hop;
    freqs.shown.listed[index] = true;
    if(item) variable_item_set_current_value_text(item, freq_value_text(index, value));

    // the note, or "In use", may come or go with it
    char* now = freqs_read(app->storage);
    if(!now) return;
    FreqsShown* check = malloc(sizeof(FreqsShown));
    freqs_collect(now, check);
    const bool note = freqs_note(now, check) != NULL;
    free(check);
    if(note != freqs.note_shown) {
        freqs_show(app);
    } else if(freqs.region_on && freqs.region_ok) {
        for(uint8_t r = 0; r < freqs.rows; r++) {
            if(freqs.kinds[r] != RowRegion) continue;
            VariableItem* region = variable_item_list_get(app->variable_list, r);
            if(region) {
                variable_item_set_current_value_text(
                    region, freqs_region_in_use(now) ? "In use" : "");
            }
        }
    }
    free(now);
}

void hadal_settings_scene_subghz_freqs_on_enter(void* context) {
    HadalSettings* app = context;
    freqs.rows = 0;
    freqs.armed = -1;

    char code[HADAL_REGION_CODE_SIZE];
    hadal_settings_atlas_region_get(app->storage, code);
    freqs.region_on = strcmp(code, "off") != 0;
    freqs.region_ok = freqs.region_on && hadal_region_plan_load(app->storage, code, &freqs.region);
    if(freqs.region_on) {
        const char* name = freqs.region.name;
        char upper[HADAL_REGION_CODE_SIZE];
        if(!freqs.region_ok) {
            size_t i = 0;
            for(; code[i] && i + 1 < sizeof(upper); i++) {
                const char c = code[i];
                upper[i] = (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
            }
            upper[i] = '\0';
            name = upper;
        }
        snprintf(freqs.region_label, sizeof(freqs.region_label), "Use %s frequencies", name);
        snprintf(
            freqs.region_locked,
            sizeof(freqs.region_locked),
            "No %s band\nplan on the\nSD card",
            name);
    }

    variable_item_list_reset(app->variable_list);
    variable_item_list_set_selected_item(app->variable_list, 0);
    freqs_show(app);
    // on the switch, not on a note above it
    for(uint8_t row = 0; row < freqs.rows; row++) {
        if(freqs.kinds[row] == RowStandard) {
            variable_item_list_set_selected_item(app->variable_list, row);
            break;
        }
    }
}

bool hadal_settings_scene_subghz_freqs_on_event(void* context, SceneManagerEvent event) {
    HadalSettings* app = context;
    if(event.type == SceneManagerEventTypeBack) {
        // back from the number input or a dialog to the list, else out
        if(freqs.in_sub_view) {
            freqs_show(app);
            return true;
        }
        return false;
    }
    if(event.type != SceneManagerEventTypeCustom) return false;

    char* text = freqs_read(app->storage);
    if(!text) {
        freqs_show(app);
        return true;
    }
    const uint32_t id = event.event;

    if(id >= EventOkBase && id < EventOkBase + ROWS_MAX) {
        const uint8_t row = id - EventOkBase;
        const RowKind kind = row < freqs.rows ? freqs.kinds[row] : RowNone;
        freqs_disarm(app, kind == RowFreq ? row : -1);
        if(kind == RowRegion && freqs.region_ok) {
            freqs_region_ask(app, text);
        } else if(kind == RowAdd) {
            number_input_set_header_text(app->number_input, "Frequency, kHz");
            number_input_set_result_callback(
                app->number_input, entered, app, 433920, 281000, 962000);
            freqs.in_sub_view = true;
            view_dispatcher_switch_to_view(app->view_dispatcher, HadalViewNumberInput);
        } else if(kind == RowFreq) {
            VariableItem* item = variable_item_list_get(app->variable_list, row);
            const uint8_t value = item ? variable_item_get_current_value_index(item) : FreqNoHop;
            if(value == FreqRemove) {
                // the second step: gone from both lists
                const uint32_t hz = freqs.shown.hz[row - freqs.first_freq];
                char* without = subghz_freqs_without(text, hz);
                const bool removed =
                    without && freqs_write(app->storage, subghz_freqs_without_hopper(without, hz));
                free(without);
                if(removed) {
                    freqs_show(app);
                } else {
                    freqs_write_failed(app);
                }
            } else {
                // OK turns it over, as on a switch
                const uint8_t other = value == FreqHop ? FreqNoHop : FreqHop;
                if(item) variable_item_set_current_value_index(item, other);
                freqs_set_value(app, row, other, text);
            }
        }
    } else if(id >= EventValueBase && id < EventValueBase + ROWS_MAX) {
        const uint8_t row = id - EventValueBase;
        if(row < freqs.rows && freqs.kinds[row] == RowFreq) {
            freqs_disarm(app, row);
            VariableItem* item = variable_item_list_get(app->variable_list, row);
            if(item) freqs_set_value(app, row, variable_item_get_current_value_index(item), text);
        }
    } else if(id == EventStandardBase || id == EventStandardBase + 1) {
        freqs_disarm(app, -1);
        const bool kept = id == EventStandardBase + 1;
        if(freqs_write(app->storage, subghz_freqs_set_standard(text, kept))) {
            freqs_show(app);
        } else {
            freqs_write_failed(app);
        }
    } else if(id == EventRegionUse) {
        if(freqs_write(
               app->storage,
               subghz_freqs_with_region(text, freqs.region.hz, freqs.region.count))) {
            freqs_show(app);
        } else {
            freqs_write_failed(app);
        }
    } else if(id == EventDialogBack) {
        freqs_show(app);
    } else if(id == EventEntered) {
        const uint32_t khz =
            scene_manager_get_scene_state(app->scene_manager, HadalSettingsSceneSubghzFreqs);
        const uint32_t hz = khz * 1000U;
        if(furi_hal_subghz_is_frequency_valid(hz)) {
            if(freqs_write(app->storage, subghz_freqs_with(text, hz))) {
                freqs_show(app);
            } else {
                freqs_write_failed(app);
            }
        } else {
            freqs_dialog(
                app,
                "Not a frequency",
                "The radio cannot tune it\n(Extended Sub-GHz in\nProtocols widens the range)",
                NULL,
                NULL);
        }
    }
    free(text);
    return true;
}

void hadal_settings_scene_subghz_freqs_on_exit(void* context) {
    HadalSettings* app = context;
    variable_item_list_reset(app->variable_list);
    dialog_ex_reset(app->dialog);
    freqs.rows = 0;
}
