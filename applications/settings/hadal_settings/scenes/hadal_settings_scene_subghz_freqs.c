#include "../hadal_settings_i.h"
#include "../subghz_freqs.h"

#include <furi_hal_subghz.h>

/* The user's own Sub-GHz frequencies: the firmware's list or not, and a list to add to.
 * Sub-GHz reads them when it opens. */

#define FREQS_FILE_MAX (8192U)
#define FREQS_MAX      (40U)

enum {
    FreqsStandard,
    FreqsAdd,
    FreqsFirst, /* + index of a user frequency: OK removes it */
};

enum {
    EventEntered = 1000,
};

// the number input or the error is on screen, over the list
static bool in_sub_view;

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
    char* text = freqs_read(app->storage);
    submenu_reset(app->submenu);
    if(!text) {
        submenu_set_header(app->submenu, "Frequencies");
        submenu_set_hints(app->submenu, "=The frequency file is too big");
        view_dispatcher_switch_to_view(app->view_dispatcher, HadalViewSubmenu);
        return;
    }
    submenu_set_header(app->submenu, "Frequencies");
    submenu_set_hints(app->submenu, "=Sub-GHz reads these at start");
    submenu_add_item(
        app->submenu,
        subghz_freqs_standard(text) ? "Built-in list: kept" : "Built-in list: left out",
        FreqsStandard,
        hadal_settings_submenu_callback,
        app);
    submenu_add_item(app->submenu, "Add a frequency...", FreqsAdd, hadal_settings_submenu_callback, app);
    uint32_t freqs[FREQS_MAX];
    const size_t count = subghz_freqs_list(text, freqs, FREQS_MAX);
    FuriString* label = furi_string_alloc();
    for(size_t i = 0; i < count; i++) {
        furi_string_printf(
            label,
            "Remove %lu.%03lu MHz",
            (unsigned long)(freqs[i] / 1000000),
            (unsigned long)(freqs[i] / 1000 % 1000));
        submenu_add_item(
            app->submenu, furi_string_get_cstr(label), FreqsFirst + i, hadal_settings_submenu_callback, app);
    }
    furi_string_free(label);
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
    } else if(event.event >= FreqsFirst) {
        uint32_t freqs[FREQS_MAX];
        const size_t count = subghz_freqs_list(text, freqs, FREQS_MAX);
        if(event.event - FreqsFirst < count) {
            freqs_write(app->storage, subghz_freqs_without(text, freqs[event.event - FreqsFirst]));
        }
        freqs_show(app);
    }
    free(text);
    return true;
}

void hadal_settings_scene_subghz_freqs_on_exit(void* context) {
    HadalSettings* app = context;
    submenu_reset(app->submenu);
    dialog_ex_reset(app->dialog);
}
