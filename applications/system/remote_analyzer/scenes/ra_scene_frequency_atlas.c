#include "../remote_analyzer_i.h"

#include <furi_hal.h>
#include <lib/subghz/protocols/base.h>
#include <flipper_format/flipper_format_i.h>
#include <toolbox/stream/stream.h>

/** @p fff's text into @p out. A .sub is plain text, so a NUL never stops it short. */
static void ra_scene_frequency_atlas_text(FlipperFormat* fff, FuriString* out) {
    Stream* stream = flipper_format_get_raw_stream(fff);
    furi_string_reset(out);
    stream_rewind(stream);
    char buffer[65];
    size_t read;
    while((read = stream_read(stream, (uint8_t*)buffer, sizeof(buffer) - 1)) > 0) {
        buffer[read] = '\0';
        furi_string_cat_str(out, buffer);
    }
}

/**
 * Runs on the capture worker's thread, like the Read screen's: the protocol's name, where SubGHz
 * stands on it, and the frame as a .sub. The rest of the reading belongs to Read.
 *
 * Serialized here and now, as the Sub-GHz app's history does: the decoder holds this frame only
 * until the next one, and the receiver is reset right below.
 */
static void ra_scene_frequency_atlas_rx_callback(
    SubGhzReceiver* receiver,
    SubGhzProtocolDecoderBase* decoder_base,
    void* context) {
    furi_assert(context);
    RemoteAnalyzer* app = context;

    /* The preset is the one tune() last set, which only changes with the worker stopped. Its
     * frequency is the one the frame came in on, whichever band the atlas files it under. */
    SubGhzRadioPreset preset = ra_radio_get_preset(app->radio);
    const bool serialized =
        subghz_protocol_decoder_base_serialize(decoder_base, app->atlas_rx_fff, &preset) ==
        SubGhzProtocolStatusOk;
    // As the Sub-GHz app saves a received signal: Repeat is a sending setting, not the frame's
    if(serialized) flipper_format_delete_key(app->atlas_rx_fff, "Repeat");

    const SubGhzProtocol* protocol = decoder_base->protocol;
    furi_mutex_acquire(app->signal_mutex, FuriWaitForever);
    strlcpy(
        app->atlas_protocol,
        (protocol && protocol->name) ? protocol->name : "Unknown",
        sizeof(app->atlas_protocol));
    app->atlas_note = remote_analyzer_support_note(decoder_base);
    if(serialized) {
        ra_scene_frequency_atlas_text(app->atlas_rx_fff, app->atlas_capture);
    } else {
        // Remote Analyzer's own decoders have no file format: nothing to save
        furi_string_reset(app->atlas_capture);
    }
    furi_mutex_release(app->signal_mutex);

    // Drop every decoder's partial frame, so the next burst starts from a clean slate
    subghz_receiver_reset(receiver);

    view_dispatcher_send_custom_event(app->view_dispatcher, RaCustomEventAtlasDecoded);
}

/** True while RX runs for the atlas, so the RSSI is only read off a receiver that is on. */
static bool ra_scene_frequency_atlas_listening = false;

/** The atlas's tune(): the band's frequency on the Read screen's modulation, or radio off. */
static void ra_scene_frequency_atlas_tune(void* context, uint32_t frequency) {
    RemoteAnalyzer* app = context;
    ra_scene_frequency_atlas_listening = false;

    /* RX down first: the decoders are reset from here, the GUI thread, while the worker
     * thread is the one feeding them. */
    ra_radio_stop(app->radio);
    ra_radio_reset_decoders(app->radio);
    // The plugin already skips these, but the radio is the app's to protect
    if(!frequency || !furi_hal_subghz_is_frequency_valid(frequency)) return;

    ra_radio_set_preset(app->radio, frequency, app->settings.preset_index);
    ra_radio_rx_start(app->radio);
    ra_scene_frequency_atlas_listening = true;
}

void ra_scene_frequency_atlas_on_enter(void* context) {
    RemoteAnalyzer* app = context;

    ra_radio_stop(app->radio);
    ra_radio_reset_decoders(app->radio);
    ra_radio_set_speaker(app->radio, false);
    // Before the callback that fills them, with the radio stopped
    app->atlas_rx_fff = flipper_format_string_alloc();
    app->atlas_capture = furi_string_alloc();
    app->atlas_capture_gui = furi_string_alloc();
    ra_radio_set_rx_callback(app->radio, ra_scene_frequency_atlas_rx_callback, app);
    ra_scene_frequency_atlas_listening = false;
    remote_analyzer_forget_last_signal(app);

    const char* modulation = subghz_setting_get_preset_name(
        ra_radio_get_setting(app->radio), app->settings.preset_index);
    furi_string_set_str(app->atlas_modulation, modulation ? modulation : "");

    app->atlas_env.view_dispatcher = app->view_dispatcher;
    app->atlas_env.view_id_list = RaViewIdPlugin;
    app->atlas_env.view_id_details = RaViewIdPluginDetails;
    app->atlas_env.view_id_picker = RaViewIdPluginList;
    app->atlas_env.park_view_id = RaViewIdSubmenu;
    app->atlas_env.context = app;
    app->atlas_env.tune = ra_scene_frequency_atlas_tune;
    // What the Read screen counts as a signal is what the atlas counts as a busy band
    app->atlas_env.trigger = app->settings.rssi_trigger;
    app->atlas_env.modulation = furi_string_get_cstr(app->atlas_modulation);
    app->atlas_env.save_event = RaCustomEventAtlasSave;

    app->plugin_loader = ra_plugin_loader_load(
        RA_ATLAS_PLUGIN_APP_ID,
        RA_ATLAS_PLUGIN_API_VERSION,
        RA_PLUGIN_PATH("remote_analyzer_atlas"),
        (const void**)&app->atlas_plugin);
    if(app->atlas_plugin == NULL) {
        // Back out from on_event, once the scene manager is done with this transition
        view_dispatcher_send_custom_event(app->view_dispatcher, RaCustomEventPluginMissing);
        return;
    }

    app->atlas_plugin->on_enter(&app->atlas_env);
    furi_timer_start(app->tick_timer, furi_ms_to_ticks(RA_SWEEP_TICK_MS));
}

bool ra_scene_frequency_atlas_on_event(void* context, SceneManagerEvent event) {
    RemoteAnalyzer* app = context;

    if(event.type != SceneManagerEventTypeCustom) return false;

    switch(event.event) {
    case RaCustomEventPluginMissing:
        // Opened straight from Settings there is no menu to go back to, so the app closes
        if(!scene_manager_previous_scene(app->scene_manager)) {
            view_dispatcher_stop(app->view_dispatcher);
        }
        return true;

    case RaCustomEventTick:
        if(app->atlas_plugin != NULL) {
            const float rssi = ra_scene_frequency_atlas_listening ? furi_hal_subghz_get_rssi() :
                                                                    -127.0f;
            app->atlas_plugin->tick(&app->atlas_env, rssi);
        }
        return true;

    case RaCustomEventAtlasDecoded: {
        char protocol[sizeof(app->atlas_protocol)];
        furi_mutex_acquire(app->signal_mutex, FuriWaitForever);
        strlcpy(protocol, app->atlas_protocol, sizeof(protocol));
        const char* note = app->atlas_note;
        // The name and the frame of one decode, taken together
        furi_string_set(app->atlas_capture_gui, app->atlas_capture);
        // A remote held down repeats one protocol, which is news only the first time
        const bool fresh = !furi_string_equal_str(app->signal_prev, protocol);
        furi_string_set_str(app->signal_prev, protocol);
        furi_mutex_release(app->signal_mutex);

        if(app->atlas_plugin != NULL) {
            app->atlas_plugin->decoded(
                &app->atlas_env, protocol, note, furi_string_get_cstr(app->atlas_capture_gui));
        }

        notification_message(app->notifications, &sequence_blink_green_100);
        if(fresh && app->settings.alert) {
            notification_message(app->notifications, &ra_sequence_new_signal);
        }
        notification_message(app->notifications, &sequence_display_backlight_on);
        return true;
    }

    case RaCustomEventAtlasSave:
        if(app->atlas_plugin != NULL) app->atlas_plugin->save(&app->atlas_env);
        return true;

    default:
        return false;
    }
}

void ra_scene_frequency_atlas_on_exit(void* context) {
    RemoteAnalyzer* app = context;

    furi_timer_stop(app->tick_timer);

    if(app->atlas_plugin != NULL) {
        app->atlas_plugin->on_exit(&app->atlas_env);
        app->atlas_plugin = NULL;
    }

    /* The radio was the atlas's to move, and the decoders were reporting to this scene: both
     * go back to how Read expects to find them, before the code that drove them is unmapped. */
    ra_radio_stop(app->radio);
    ra_scene_frequency_atlas_listening = false;
    remote_analyzer_restore_rx_callback(app);
    ra_radio_reset_decoders(app->radio);
    remote_analyzer_forget_last_signal(app);
    // The worker is joined and the callback is Read's again: nothing writes these any more
    flipper_format_free(app->atlas_rx_fff);
    furi_string_free(app->atlas_capture);
    furi_string_free(app->atlas_capture_gui);
    app->atlas_rx_fff = NULL;
    app->atlas_capture = NULL;
    app->atlas_capture_gui = NULL;
    ra_radio_set_preset(app->radio, app->settings.frequency, app->settings.preset_index);
    app->tuned_frequency = 0; // Read starts from an empty screen

    ra_plugin_loader_unload(app->plugin_loader);
    app->plugin_loader = NULL;

    notification_message(app->notifications, &sequence_reset_rgb);
}
