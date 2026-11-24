#include "../hadal_settings_i.h"
#include "../subghz_freqs.h"

#include <furi_hal_subghz.h>

/* The user's own Sub-GHz frequencies: the firmware's list or not, a list to add to, and which
 * of them the hopper visits. Sub-GHz reads them when it opens. */

#define FREQS_FILE_MAX (8192U)
#define FREQS_MAX      (40U)

enum {
    FreqsStandard,
    FreqsAdd,
    FreqsFirst, /* + index of a frequency in the list shown: OK offers to remove it or to change
                   whether it hops */
};

enum {
    EventEntered = 1000,
    EventRemove,
    EventToggleHop,
    EventAskRemove,
    EventCancel,
};

// the number input, the error or a frequency's choices are on screen, over the list
static bool in_sub_view;
static bool asking_remove; /**< the frequency's dialog asks whether to remove it */
// the frequency whose choices are shown
static uint32_t chosen_hz;

// The list shown: the frequencies, then those that only hop; and which of them hop
typedef struct {
    uint32_t hz[FREQS_MAX];
    bool hops[FREQS_MAX];
    bool listed[FREQS_MAX]; /* a "Frequency:" line, not only a hopper one */
    size_t count;
    size_t hopper_count;
} FreqsShown;

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

/* As every Hadal dialog: the safe choice on the left, the one that changes things for good on
 * the right, asked again before it is done (asking_remove, beside in_sub_view) */

static void choice_callback(DialogExResult result, void* context) {
    HadalSettings* app = context;
    uint32_t event = 0;
    if(asking_remove) {
        event = result == DialogExResultRight ? EventRemove :
                result == DialogExResultLeft  ? EventCancel :
                                                0;
    } else {
        event = result == DialogExResultLeft  ? EventToggleHop :
                result == DialogExResultRight ? EventAskRemove :
                                                0;
    }
    if(event) view_dispatcher_send_custom_event(app->view_dispatcher, event);
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

static void freqs_write(Storage* storage, char* text) {
    if(!text) return;
    File* file = storage_file_alloc(storage);
    if(storage_file_open(file, SUBGHZ_FREQS_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        storage_file_write(file, text, strlen(text));
    }
    storage_file_free(file);
    free(text);
}

static void entered(void* context, int32_t number) {
    HadalSettings* app = context;
    // kHz in, Hz kept
    scene_manager_set_scene_state(app->scene_manager, HadalSettingsSceneSubghzFreqs, number);
    view_dispatcher_send_custom_event(app->view_dispatcher, EventEntered);
}

static void freqs_show(HadalSettings* app) {
    in_sub_view = false;
    asking_remove = false;
    char* text = freqs_read(app->storage);
    submenu_reset(app->submenu);
    if(!text) {
        submenu_set_header(app->submenu, "Frequencies");
        submenu_set_hints(app->submenu, "=The frequency file is too big");
        view_dispatcher_switch_to_view(app->view_dispatcher, HadalViewSubmenu);
        return;
    }
    FreqsShown* shown = malloc(sizeof(FreqsShown));
    freqs_collect(text, shown);
    submenu_set_header(app->submenu, "Frequencies");
    // left out, the built-in hopping list goes too: with none of these hopping, Sub-GHz would
    // ignore the whole file
    submenu_set_hints(
        app->submenu,
        !subghz_freqs_standard(text) && !shown->hopper_count ? "=Set one to hop, or none count" :
                                                                "=Sub-GHz reads these at start");
    submenu_add_item(
        app->submenu,
        subghz_freqs_standard(text) ? "Built-in list: kept" : "Built-in list: left out",
        FreqsStandard,
        hadal_settings_submenu_callback,
        app);
    submenu_add_item(app->submenu, "Add a frequency...", FreqsAdd, hadal_settings_submenu_callback, app);
    FuriString* label = furi_string_alloc();
    for(size_t i = 0; i < shown->count; i++) {
        furi_string_printf(
            label,
            "%lu.%03lu MHz%s",
            (unsigned long)(shown->hz[i] / 1000000),
            (unsigned long)(shown->hz[i] / 1000 % 1000),
            !shown->listed[i] ? "  hop only" : shown->hops[i] ? "  hops" : "");
        submenu_add_item(
            app->submenu, furi_string_get_cstr(label), FreqsFirst + i, hadal_settings_submenu_callback, app);
    }
    furi_string_free(label);
    free(shown);
    free(text);
    view_dispatcher_switch_to_view(app->view_dispatcher, HadalViewSubmenu);
}

void hadal_settings_scene_subghz_freqs_on_enter(void* context) {
    freqs_show(context);
}

bool hadal_settings_scene_subghz_freqs_on_event(void* context, SceneManagerEvent event) {
    HadalSettings* app = context;
    if(event.type == SceneManagerEventTypeBack) {
        // back from the number input to the list, else out
        if(in_sub_view) {
            freqs_show(app);
            return true;
        }
        return false;
    }
    if(event.type != SceneManagerEventTypeCustom) return false;

    char* text = freqs_read(app->storage);
    if(!text) return true;
    if(event.event == FreqsStandard) {
        freqs_write(app->storage, subghz_freqs_set_standard(text, !subghz_freqs_standard(text)));
        freqs_show(app);
    } else if(event.event == FreqsAdd) {
        number_input_set_header_text(app->number_input, "Frequency, kHz");
        number_input_set_result_callback(app->number_input, entered, app, 433920, 281000, 962000);
        in_sub_view = true;
        view_dispatcher_switch_to_view(app->view_dispatcher, HadalViewNumberInput);
    } else if(event.event == EventEntered) {
        const uint32_t hz =
            scene_manager_get_scene_state(app->scene_manager, HadalSettingsSceneSubghzFreqs) * 1000U;
        if(furi_hal_subghz_is_frequency_valid(hz)) {
            freqs_write(app->storage, subghz_freqs_with(text, hz));
            freqs_show(app);
        } else {
            dialog_ex_reset(app->dialog);
            dialog_ex_set_header(app->dialog, "Not a frequency", 64, 0, AlignCenter, AlignTop);
            dialog_ex_set_text(
                app->dialog,
                "The radio cannot tune it\n(Extended Sub-GHz in\nProtocols widens the range)",
                64,
                14,
                AlignCenter,
                AlignTop);
            in_sub_view = true;
            view_dispatcher_switch_to_view(app->view_dispatcher, HadalViewDialog);
        }
    } else if(event.event == EventAskRemove) {
        asking_remove = true;
        dialog_ex_set_text(app->dialog, "Remove it from the list?", 64, 24, AlignCenter, AlignTop);
        dialog_ex_set_left_button_text(app->dialog, "Cancel");
        dialog_ex_set_right_button_text(app->dialog, "Remove");
    } else if(event.event == EventCancel) {
        asking_remove = false;
        freqs_show(app);
    } else if(event.event == EventRemove) {
        asking_remove = false;
        // gone from both lists
        char* without = subghz_freqs_without(text, chosen_hz);
        if(without) {
            freqs_write(app->storage, subghz_freqs_without_hopper(without, chosen_hz));
            free(without);
        }
        freqs_show(app);
    } else if(event.event == EventToggleHop) {
        uint32_t hopper[FREQS_MAX];
        const size_t hopper_count = subghz_freqs_list_hopper(text, hopper, FREQS_MAX);
        bool hops = false;
        for(size_t i = 0; i < hopper_count; i++) {
            if(hopper[i] == chosen_hz) hops = true;
        }
        freqs_write(
            app->storage,
            hops ? subghz_freqs_without_hopper(text, chosen_hz) :
                   subghz_freqs_with_hopper(text, chosen_hz));
        freqs_show(app);
    } else if(event.event >= FreqsFirst) {
        FreqsShown* shown = malloc(sizeof(FreqsShown));
        freqs_collect(text, shown);
        const size_t index = event.event - FreqsFirst;
        if(index < shown->count) {
            chosen_hz = shown->hz[index];
            char header[24];
            snprintf(
                header,
                sizeof(header),
                "%lu.%03lu MHz",
                (unsigned long)(chosen_hz / 1000000),
                (unsigned long)(chosen_hz / 1000 % 1000));
            dialog_ex_reset(app->dialog);
            dialog_ex_set_header(app->dialog, header, 64, 2, AlignCenter, AlignTop);
            dialog_ex_set_text(
                app->dialog,
                shown->hops[index] ? "The hopper visits it" : "The hopper passes it by",
                64,
                24,
                AlignCenter,
                AlignTop);
            asking_remove = false;
            dialog_ex_set_left_button_text(app->dialog, shown->hops[index] ? "No hop" : "Hop");
            dialog_ex_set_right_button_text(app->dialog, "Remove");
            dialog_ex_set_result_callback(app->dialog, choice_callback);
            dialog_ex_set_context(app->dialog, app);
            in_sub_view = true;
            view_dispatcher_switch_to_view(app->view_dispatcher, HadalViewDialog);
        } else {
            freqs_show(app);
        }
        free(shown);
    }
    free(text);
    return true;
}

void hadal_settings_scene_subghz_freqs_on_exit(void* context) {
    HadalSettings* app = context;
    submenu_reset(app->submenu);
    dialog_ex_reset(app->dialog);
}
