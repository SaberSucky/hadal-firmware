/**
 * @file ra_atlas_plugin.h
 * @brief App side of the Frequency Atlas plugin contract.
 *
 * The atlas lists a region's band plan - read off the SD card, never compiled in - and keeps a
 * live reading per band: it parks the receiver on one band at a time, so the same decoders the
 * Read screen uses name whatever they recognise there.
 *
 * As with the other plugins, the .fal resolves its symbols from the firmware API table alone.
 * The radio stays the app's: the plugin asks for a band through tune(), and the app hands it
 * the RSSI and every decode, all on the GUI thread.
 *
 * Still receive-only. A decode comes with the frame as a .sub file's text, serialized by the
 * app on the capture thread while the decoder still held it; the plugin can save that as a file
 * in /ext/subghz, where the Sub-GHz app's own Saved list opens and sends it.
 */
#pragma once

#include <gui/view_dispatcher.h>

#define RA_ATLAS_PLUGIN_APP_ID      "RemoteAnalyzerFreqAtlas"
/** 2: decoded() hands over the frame as a .sub, and save() saves the one on screen */
#define RA_ATLAS_PLUGIN_API_VERSION 2

/** Where the band plans live on the card: <code>.txt, one file per region. */
#define RA_ATLAS_DIR         EXT_PATH("frequency_atlas")
/** The region Settings > Hadal > Protocols picked, shared with the atlas's own picker. */
#define RA_ATLAS_REGION_PATH EXT_PATH("apps_data/remote_analyzer/atlas_region")
#define RA_ATLAS_REGION_KEY  "Region"
/** What the region file says when no region is chosen. */
#define RA_ATLAS_REGION_OFF  "off"

/**
 * Listen on @p frequency with the app's own modulation, decoders reset. 0 stops the radio.
 * The app refuses a frequency the radio cannot tune, so the plugin only has to avoid asking.
 */
typedef void (*RaAtlasTune)(void* context, uint32_t frequency);

typedef struct {
    /* In: what the atlas needs to run */
    ViewDispatcher* view_dispatcher;
    uint32_t view_id_list; /**< the bands */
    uint32_t view_id_details; /**< one band in full */
    uint32_t view_id_picker; /**< the regions on the card */
    uint32_t park_view_id; /**< app-owned view to park on before ours are removed */
    void* context;
    RaAtlasTune tune;
    float trigger; /**< dBm at which a band counts as busy: the Read screen's level */
    const char* modulation; /**< the preset being listened with, for the details screen */
    /** The custom event the plugin sends to have save() called: a widget's button callback
     * runs with the widget locked, so the details screen cannot be redrawn from inside it. */
    uint32_t save_event;
} RaAtlasEnv;

/** What the .fal exports. */
typedef struct {
    /** Load the region and show it, or the region picker. Starts listening. */
    void (*on_enter)(RaAtlasEnv* env);
    /** Every RA_SWEEP_TICK_MS, with the RSSI on the frequency last tuned. */
    void (*tick)(RaAtlasEnv* env, float rssi);
    /**
     * A frame decoded on the band being listened to. @p note says where SubGHz stands on it.
     * @p capture is the frame as a .sub file's text, "" when the protocol has no file format
     * (Remote Analyzer's own decoders): borrowed for the call, copied if kept.
     */
    void (*decoded)(RaAtlasEnv* env, const char* protocol, const char* note, const char* capture);
    /** env->save_event landed: save what the details screen shows, and say how it went. */
    void (*save)(RaAtlasEnv* env);
    /** Park, unregister every screen and free. The app stops the radio afterwards. */
    void (*on_exit)(RaAtlasEnv* env);
} RaAtlasPlugin;
