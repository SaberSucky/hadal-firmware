#include "nfc_supported_cards.h"

#include "../plugins/supported_cards/nfc_supported_card_plugin.h"

#include <flipper_application/flipper_application.h>
#include <flipper_application/plugins/plugin_manager.h>
#include <loader/firmware_api/firmware_api.h>

#include <furi.h>
#include <furi_hal_version.h>
#include <toolbox/crc32_calc.h>
#include <toolbox/stream/buffered_file_stream.h>
#include <path.h>
#include <m-array.h>

#define TAG "NfcSupportedCards"

#define NFC_SUPPORTED_CARDS_PLUGINS_PATH APP_DATA_PATH("plugins")
/* What each parser is for (protocol, features), so that it need not be loaded to be asked: every
 * session loaded and linked all of them (about 45) on the first read. Kept with a key of the
 * firmware's commit and every parser file's name and size; any change and they are asked again. */
#define NFC_SUPPORTED_CARDS_CACHE_PATH APP_DATA_PATH(".parsers.cache")

typedef enum {
    NfcSupportedCardsPluginFeatureHasVerify = (1U << 0),
    NfcSupportedCardsPluginFeatureHasRead = (1U << 1),
    NfcSupportedCardsPluginFeatureHasParse = (1U << 2),
} NfcSupportedCardsPluginFeature;

typedef struct {
    FuriString* name;
    NfcProtocol protocol;
    NfcSupportedCardsPluginFeature feature;
} NfcSupportedCardsPluginCache;

ARRAY_DEF(NfcSupportedCardsPluginCache, NfcSupportedCardsPluginCache, M_POD_OPLIST); //-V658

typedef enum {
    NfcSupportedCardsLoadStateIdle,
    NfcSupportedCardsLoadStateInProgress,
    NfcSupportedCardsLoadStateSuccess,
    NfcSupportedCardsLoadStateFail,
} NfcSupportedCardsLoadState;

typedef struct {
    Storage* storage;
    File* directory;
    char file_name[256];
    FlipperApplication* app;
} NfcSupportedCardsLoadContext;

struct NfcSupportedCards {
    CompositeApiResolver* api_resolver;
    NfcSupportedCardsPluginCache_t plugins_cache_arr;
    NfcSupportedCardsLoadState load_state;
    NfcSupportedCardsLoadContext* load_context;
    /** The plugin that parsed the last card, tried first next time (Info on the same card loaded
     * every parser of its protocol again); SIZE_MAX for none */
    size_t last_parsed;
};

NfcSupportedCards* nfc_supported_cards_alloc(CompositeApiResolver* api_resolver) {
    NfcSupportedCards* instance = malloc(sizeof(NfcSupportedCards));
    instance->api_resolver = api_resolver;
    instance->last_parsed = SIZE_MAX;

    NfcSupportedCardsPluginCache_init(instance->plugins_cache_arr);

    return instance;
}

void nfc_supported_cards_free(NfcSupportedCards* instance) {
    furi_assert(instance);

    NfcSupportedCardsPluginCache_it_t iter;
    for(NfcSupportedCardsPluginCache_it(iter, instance->plugins_cache_arr);
        !NfcSupportedCardsPluginCache_end_p(iter);
        NfcSupportedCardsPluginCache_next(iter)) {
        NfcSupportedCardsPluginCache* plugin_cache = NfcSupportedCardsPluginCache_ref(iter);
        furi_string_free(plugin_cache->name);
    }
    NfcSupportedCardsPluginCache_clear(instance->plugins_cache_arr);

    free(instance);
}

static NfcSupportedCardsLoadContext* nfc_supported_cards_load_context_alloc(void) {
    NfcSupportedCardsLoadContext* instance = malloc(sizeof(NfcSupportedCardsLoadContext));

    instance->storage = furi_record_open(RECORD_STORAGE);
    instance->directory = storage_file_alloc(instance->storage);

    if(!storage_dir_open(instance->directory, NFC_SUPPORTED_CARDS_PLUGINS_PATH)) {
        FURI_LOG_D(TAG, "Failed to open directory: %s", NFC_SUPPORTED_CARDS_PLUGINS_PATH);
    }

    return instance;
}

static void nfc_supported_cards_load_context_free(NfcSupportedCardsLoadContext* instance) {
    if(instance->app) {
        flipper_application_free(instance->app);
    }

    storage_dir_close(instance->directory);
    storage_file_free(instance->directory);

    furi_record_close(RECORD_STORAGE);
    free(instance);
}

static const NfcSupportedCardsPlugin* nfc_supported_cards_get_plugin(
    NfcSupportedCardsLoadContext* instance,
    const char* name,
    const ElfApiInterface* api_interface) {
    furi_assert(instance);
    furi_assert(name);

    const NfcSupportedCardsPlugin* plugin = NULL;
    FuriString* plugin_path = furi_string_alloc_printf(
        "%s/%s%s", NFC_SUPPORTED_CARDS_PLUGINS_PATH, name, NFC_SUPPORTED_CARDS_PLUGIN_SUFFIX);
    do {
        if(instance->app) flipper_application_free(instance->app);
        instance->app = flipper_application_alloc(instance->storage, api_interface);

        if(flipper_application_preload(instance->app, furi_string_get_cstr(plugin_path)) !=
           FlipperApplicationPreloadStatusSuccess)
            break;
        if(!flipper_application_is_plugin(instance->app)) break;
        if(flipper_application_map_to_memory(instance->app) != FlipperApplicationLoadStatusSuccess)
            break;
        const FlipperAppPluginDescriptor* descriptor =
            flipper_application_plugin_get_descriptor(instance->app);

        if(descriptor == NULL) break;

        if(strcmp(descriptor->appid, NFC_SUPPORTED_CARD_PLUGIN_APP_ID) != 0) break;
        if(descriptor->ep_api_version != NFC_SUPPORTED_CARD_PLUGIN_API_VERSION) break;

        plugin = descriptor->entry_point;
    } while(false);
    furi_string_free(plugin_path);

    return plugin;
}

static const NfcSupportedCardsPlugin* nfc_supported_cards_get_next_plugin(
    NfcSupportedCardsLoadContext* instance,
    const ElfApiInterface* api_interface) {
    const NfcSupportedCardsPlugin* plugin = NULL;

    do {
        if(!storage_file_is_open(instance->directory)) break;
        if(!storage_dir_read(
               instance->directory, NULL, instance->file_name, sizeof(instance->file_name)))
            break;

        const size_t suffix_len = strlen(NFC_SUPPORTED_CARDS_PLUGIN_SUFFIX);
        const size_t file_name_len = strlen(instance->file_name);
        if(file_name_len <= suffix_len) break;

        size_t suffix_start_pos = file_name_len - suffix_len;
        if(memcmp(
               &instance->file_name[suffix_start_pos],
               NFC_SUPPORTED_CARDS_PLUGIN_SUFFIX,
               suffix_len) != 0) //-V1051
            break;

        // Trim suffix from file_name to save memory. The suffix will be concatenated on plugin load.
        instance->file_name[suffix_start_pos] = '\0';

        plugin = nfc_supported_cards_get_plugin(instance, instance->file_name, api_interface);
    } while(plugin == NULL); //-V654

    return plugin;
}

// The key of the parsers there are now: the firmware's commit, each file's name and size
static uint32_t nfc_supported_cards_cache_key(Storage* storage) {
    const char* commit = version_get_githash(furi_hal_version_get_firmware_version());
    uint32_t key = crc32_calc_buffer(0, commit, strlen(commit));
    File* dir = storage_file_alloc(storage);
    char name[128];
    FileInfo info;
    if(storage_dir_open(dir, NFC_SUPPORTED_CARDS_PLUGINS_PATH)) {
        while(storage_dir_read(dir, &info, name, sizeof(name))) {
            key = crc32_calc_buffer(key, name, strlen(name));
            const uint32_t size = (uint32_t)info.size;
            key = crc32_calc_buffer(key, &size, sizeof(size));
        }
    }
    storage_dir_close(dir);
    storage_file_free(dir);
    return key;
}

static bool nfc_supported_cards_cache_read(NfcSupportedCards* instance, uint32_t key) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    Stream* stream = buffered_file_stream_alloc(storage);
    FuriString* line = furi_string_alloc();
    bool ok = false;
    if(buffered_file_stream_open(
           stream, NFC_SUPPORTED_CARDS_CACHE_PATH, FSAM_READ, FSOM_OPEN_EXISTING) &&
       stream_read_line(stream, line) && strtoul(furi_string_get_cstr(line), NULL, 16) == key) {
        ok = true;
        while(stream_read_line(stream, line)) {
            char name[64];
            unsigned protocol, feature;
            if(sscanf(furi_string_get_cstr(line), "%63s %u %u", name, &protocol, &feature) != 3 ||
               protocol >= NfcProtocolNum) {
                ok = false;
                break;
            }
            NfcSupportedCardsPluginCache plugin_cache = {
                .name = furi_string_alloc_set_str(name),
                .protocol = (NfcProtocol)protocol,
                .feature = (NfcSupportedCardsPluginFeature)feature,
            };
            NfcSupportedCardsPluginCache_push_back(instance->plugins_cache_arr, plugin_cache);
        }
    }
    if(!ok) {
        // half read: what was taken goes, and the parsers are asked
        for(size_t i = 0; i < NfcSupportedCardsPluginCache_size(instance->plugins_cache_arr); i++) {
            furi_string_free(NfcSupportedCardsPluginCache_get(instance->plugins_cache_arr, i)->name);
        }
        NfcSupportedCardsPluginCache_reset(instance->plugins_cache_arr);
    }
    furi_string_free(line);
    buffered_file_stream_close(stream);
    stream_free(stream);
    furi_record_close(RECORD_STORAGE);
    return ok && NfcSupportedCardsPluginCache_size(instance->plugins_cache_arr) > 0;
}

static void nfc_supported_cards_cache_write(NfcSupportedCards* instance, uint32_t key) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    Stream* stream = buffered_file_stream_alloc(storage);
    if(buffered_file_stream_open(
           stream, NFC_SUPPORTED_CARDS_CACHE_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        stream_write_format(stream, "%08lX\n", (unsigned long)key);
        for(size_t i = 0; i < NfcSupportedCardsPluginCache_size(instance->plugins_cache_arr); i++) {
            const NfcSupportedCardsPluginCache* cache =
                NfcSupportedCardsPluginCache_cget(instance->plugins_cache_arr, i);
            stream_write_format(
                stream,
                "%s %u %u\n",
                furi_string_get_cstr(cache->name),
                (unsigned)cache->protocol,
                (unsigned)cache->feature);
        }
    }
    buffered_file_stream_close(stream);
    stream_free(stream);
    furi_record_close(RECORD_STORAGE);
}

void nfc_supported_cards_load_cache(NfcSupportedCards* instance) {
    furi_assert(instance);

    do {
        if((instance->load_state == NfcSupportedCardsLoadStateSuccess) ||
           (instance->load_state == NfcSupportedCardsLoadStateFail))
            break;

        Storage* storage = furi_record_open(RECORD_STORAGE);
        const uint32_t key = nfc_supported_cards_cache_key(storage);
        furi_record_close(RECORD_STORAGE);
        if(nfc_supported_cards_cache_read(instance, key)) {
            instance->load_state = NfcSupportedCardsLoadStateSuccess;
            break;
        }

        instance->load_context = nfc_supported_cards_load_context_alloc();

        while(true) {
            const ElfApiInterface* api_interface =
                composite_api_resolver_get(instance->api_resolver);
            const NfcSupportedCardsPlugin* plugin =
                nfc_supported_cards_get_next_plugin(instance->load_context, api_interface);
            if(plugin == NULL) break; //-V547

            NfcSupportedCardsPluginCache plugin_cache = {}; //-V779
            plugin_cache.name = furi_string_alloc_set(instance->load_context->file_name);
            plugin_cache.protocol = plugin->protocol;
            if(plugin->verify) {
                plugin_cache.feature |= NfcSupportedCardsPluginFeatureHasVerify;
            }
            if(plugin->read) {
                plugin_cache.feature |= NfcSupportedCardsPluginFeatureHasRead;
            }
            if(plugin->parse) {
                plugin_cache.feature |= NfcSupportedCardsPluginFeatureHasParse;
            }
            NfcSupportedCardsPluginCache_push_back(instance->plugins_cache_arr, plugin_cache);
        }

        nfc_supported_cards_load_context_free(instance->load_context);

        size_t plugins_loaded = NfcSupportedCardsPluginCache_size(instance->plugins_cache_arr);
        if(plugins_loaded == 0) {
            FURI_LOG_D(TAG, "Plugins not found");
            instance->load_state = NfcSupportedCardsLoadStateFail;
        } else {
            FURI_LOG_D(TAG, "Loaded %zu plugins", plugins_loaded);
            instance->load_state = NfcSupportedCardsLoadStateSuccess;
            nfc_supported_cards_cache_write(instance, key);
        }

    } while(false);
}

bool nfc_supported_cards_read(NfcSupportedCards* instance, NfcDevice* device, Nfc* nfc) {
    furi_assert(instance);
    furi_assert(device);
    furi_assert(nfc);

    bool card_read = false;
    NfcProtocol protocol = nfc_device_get_protocol(device);

    do {
        if(instance->load_state != NfcSupportedCardsLoadStateSuccess) break;

        instance->load_context = nfc_supported_cards_load_context_alloc();

        NfcSupportedCardsPluginCache_it_t iter;
        for(NfcSupportedCardsPluginCache_it(iter, instance->plugins_cache_arr);
            !NfcSupportedCardsPluginCache_end_p(iter);
            NfcSupportedCardsPluginCache_next(iter)) {
            NfcSupportedCardsPluginCache* plugin_cache = NfcSupportedCardsPluginCache_ref(iter);
            if(plugin_cache->protocol != protocol) continue;
            if((plugin_cache->feature & NfcSupportedCardsPluginFeatureHasRead) == 0) continue;

            const ElfApiInterface* api_interface =
                composite_api_resolver_get(instance->api_resolver);
            const NfcSupportedCardsPlugin* plugin = nfc_supported_cards_get_plugin(
                instance->load_context, furi_string_get_cstr(plugin_cache->name), api_interface);
            if(plugin == NULL) continue;

            if(plugin->verify) {
                if(!plugin->verify(nfc)) continue;
            }

            if(plugin->read) {
                if(plugin->read(nfc, device)) {
                    card_read = true;
                    break;
                }
            }
        }

        nfc_supported_cards_load_context_free(instance->load_context);
    } while(false);

    return card_read;
}

// One plugin's parse of the card: false if it is not its protocol, cannot parse, or declines
static bool nfc_supported_cards_try_parse(
    NfcSupportedCards* instance,
    NfcSupportedCardsPluginCache* plugin_cache,
    NfcProtocol protocol,
    NfcDevice* device,
    FuriString* parsed_data) {
    if(plugin_cache->protocol != protocol) return false;
    if((plugin_cache->feature & NfcSupportedCardsPluginFeatureHasParse) == 0) return false;

    const ElfApiInterface* api_interface = composite_api_resolver_get(instance->api_resolver);
    const NfcSupportedCardsPlugin* plugin = nfc_supported_cards_get_plugin(
        instance->load_context, furi_string_get_cstr(plugin_cache->name), api_interface);
    if(plugin == NULL || !plugin->parse) return false;

    // a plugin may write before it decides the card is not its own, so start clean
    furi_string_reset(parsed_data);
    return plugin->parse(device, parsed_data);
}

bool nfc_supported_cards_parse(
    NfcSupportedCards* instance,
    NfcDevice* device,
    FuriString* parsed_data) {
    furi_assert(instance);
    furi_assert(device);
    furi_assert(parsed_data);

    bool card_parsed = false;
    NfcProtocol protocol = nfc_device_get_protocol(device);

    do {
        if(instance->load_state != NfcSupportedCardsLoadStateSuccess) break;

        instance->load_context = nfc_supported_cards_load_context_alloc();

        const size_t count = NfcSupportedCardsPluginCache_size(instance->plugins_cache_arr);
        // the plugin that knew the last card first: most often the same card again
        if(instance->last_parsed < count &&
           nfc_supported_cards_try_parse(
               instance,
               NfcSupportedCardsPluginCache_get(instance->plugins_cache_arr, instance->last_parsed),
               protocol,
               device,
               parsed_data)) {
            card_parsed = true;
        }
        for(size_t i = 0; !card_parsed && i < count; i++) {
            if(i == instance->last_parsed) continue;
            if(nfc_supported_cards_try_parse(
                   instance,
                   NfcSupportedCardsPluginCache_get(instance->plugins_cache_arr, i),
                   protocol,
                   device,
                   parsed_data)) {
                card_parsed = true;
                instance->last_parsed = i;
            }
        }

        nfc_supported_cards_load_context_free(instance->load_context);
    } while(false);

    return card_parsed;
}
