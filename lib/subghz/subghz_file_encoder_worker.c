#include "subghz_file_encoder_worker.h"

#include <toolbox/stream/stream.h>
#include <flipper_format/flipper_format.h>
#include <flipper_format/flipper_format_i.h>
#include <lib/subghz/devices/devices.h>
#include <lib/toolbox/strint.h>

#define TAG "SubGhzFileEncoderWorker"

#define SUBGHZ_FILE_ENCODER_LOAD 512

/* Longest duration the radio can hold a level for in a single step */
#define SUBGHZ_FILE_ENCODER_DURATION_MAX 1000000

struct SubGhzFileEncoderWorker {
    FuriThread* thread;
    FuriStreamBuffer* stream;

    Storage* storage;
    FlipperFormat* flipper_format;

    volatile bool worker_running;
    volatile bool worker_stopping;
    bool is_storage_slow;
    bool is_decoding;
    FuriString* str_data;
    FuriString* file_path;
    const SubGhzDevice* device;

    SubGhzFileEncoderWorkerCallbackEnd callback_end;
    void* context_end;
};

void subghz_file_encoder_worker_callback_end(
    SubGhzFileEncoderWorker* instance,
    SubGhzFileEncoderWorkerCallbackEnd callback_end,
    void* context_end) {
    furi_assert(instance);
    furi_assert(callback_end);
    instance->callback_end = callback_end;
    instance->context_end = context_end;
}

void subghz_file_encoder_worker_add_level_duration(
    SubGhzFileEncoderWorker* instance,
    int32_t duration) {
    size_t ret = furi_stream_buffer_send(instance->stream, &duration, sizeof(int32_t), 100);
    if(sizeof(int32_t) != ret) FURI_LOG_E(TAG, "Invalid add duration in the stream");
}

bool subghz_file_encoder_worker_data_parse(SubGhzFileEncoderWorker* instance, const char* strStart) {
    // Line sample: "RAW_Data: -1, 2, -2..."

    // Look for the key in the line
    char* str = strstr(strStart, "RAW_Data: ");
    bool res = false;

    if(str) {
        // Skip key
        str = strchr(str, ' ');

        // Parse next element
        int32_t duration;
        while(strint_to_int32(str, &str, &duration, 10) == StrintParseNoError) {
            if((duration < -SUBGHZ_FILE_ENCODER_DURATION_MAX) ||
               (duration > SUBGHZ_FILE_ENCODER_DURATION_MAX)) {
                //the radio cannot hold a level for that long, so on TX the sample is
                //replaced by a short pulse - when decoding, clamping keeps the long
                //silence between two signals a long silence
                int32_t clamped = instance->is_decoding ? SUBGHZ_FILE_ENCODER_DURATION_MAX : 100;
                subghz_file_encoder_worker_add_level_duration(
                    instance, (duration > 0) ? clamped : -clamped);
                //FURI_LOG_I("PARSE", "Number overflow - %d", duration);
            } else {
                subghz_file_encoder_worker_add_level_duration(instance, duration);
            }
            if(*str == ',') str++; // could also be `\0`
        }

        res = true;
    }

    return res;
}

/* A RAW file's timings, read straight from it a few bytes at a time. Each line was read whole
 * first: a string of up to 4.6 KB, freed for every line and grown again a character at a time,
 * which at its largest wanted 7 KB at once part way through sending, with the least free. It ran
 * Sub-GHz out of memory sending a saved RAW signal, and restarted the Flipper */
typedef struct {
    Stream* stream;
    uint8_t data[64];
    uint16_t length;
    uint16_t at;
} SubGhzRawReader;

typedef enum {
    SubGhzRawLineData, /**< a RAW_Data line, its timings sent on */
    SubGhzRawLineOther, /**< some other line: it ends the signal */
    SubGhzRawLineEnd, /**< the end of the file, or the worker stopped */
} SubGhzRawLine;

/** The next byte, or -1 at the end of the file */
static int16_t subghz_raw_reader_next(SubGhzRawReader* reader) {
    if(reader->at == reader->length) {
        reader->length = stream_read(reader->stream, reader->data, sizeof(reader->data));
        reader->at = 0;
        if(!reader->length) return -1;
    }
    return reader->data[reader->at++];
}

static void subghz_file_encoder_worker_send(SubGhzFileEncoderWorker* instance, int32_t duration) {
    if((duration < -SUBGHZ_FILE_ENCODER_DURATION_MAX) ||
       (duration > SUBGHZ_FILE_ENCODER_DURATION_MAX)) {
        //the radio cannot hold a level for that long, so on TX the sample is
        //replaced by a short pulse - when decoding, clamping keeps the long
        //silence between two signals a long silence
        int32_t clamped = instance->is_decoding ? SUBGHZ_FILE_ENCODER_DURATION_MAX : 100;
        subghz_file_encoder_worker_add_level_duration(instance, (duration > 0) ? clamped : -clamped);
    } else {
        subghz_file_encoder_worker_add_level_duration(instance, duration);
    }
}

/** One line of the file. Blank lines (and a Windows line end's \r) are passed over */
static SubGhzRawLine
    subghz_file_encoder_worker_line(SubGhzFileEncoderWorker* instance, SubGhzRawReader* reader) {
    static const char key[] = "RAW_Data:";
    int16_t c;
    do {
        c = subghz_raw_reader_next(reader);
    } while(c == ' ' || c == '\t' || c == '\r' || c == '\n');
    if(c < 0) return SubGhzRawLineEnd;

    for(size_t i = 0; key[i]; i++) {
        if(c != key[i]) {
            while(c >= 0 && c != '\n') c = subghz_raw_reader_next(reader);
            return SubGhzRawLineOther;
        }
        c = subghz_raw_reader_next(reader);
    }

    char token[16];
    size_t length = 0;
    bool skip_rest = false; // after a token that is not a number, as the line parse did
    while(true) {
        if(!instance->worker_running) return SubGhzRawLineEnd;
        const bool line_end = (c < 0) || (c == '\n');
        if(line_end || c == ' ' || c == ',' || c == '\r' || c == '\t') {
            if(length && !skip_rest) {
                token[length] = '\0';
                int32_t duration;
                if(strint_to_int32(token, NULL, &duration, 10) == StrintParseNoError) {
                    subghz_file_encoder_worker_send(instance, duration);
                } else {
                    skip_rest = true;
                }
            }
            length = 0;
            if(line_end) return SubGhzRawLineData;
        } else if(length < sizeof(token) - 1) {
            token[length++] = (char)c;
        } else {
            skip_rest = true; // longer than any number
        }
        c = subghz_raw_reader_next(reader);
    }
}

void subghz_file_encoder_worker_get_text_progress(
    SubGhzFileEncoderWorker* instance,
    FuriString* output) {
    UNUSED(output);
    Stream* stream = flipper_format_get_raw_stream(instance->flipper_format);
    size_t total_size = stream_size(stream);
    size_t current_offset = stream_tell(stream);
    size_t buffer_avail = furi_stream_buffer_bytes_available(instance->stream);

    furi_string_printf(output, "%03u%%", 100 * (current_offset - buffer_avail) / total_size);
}

LevelDuration subghz_file_encoder_worker_get_level_duration(void* context) {
    furi_assert(context);
    SubGhzFileEncoderWorker* instance = context;
    int32_t duration;
    int ret = furi_stream_buffer_receive(instance->stream, &duration, sizeof(int32_t), 0);
    if(ret == sizeof(int32_t)) {
        LevelDuration level_duration = {.level = LEVEL_DURATION_RESET};
        if(duration < 0) {
            level_duration = level_duration_make(false, -duration);
        } else if(duration > 0) {
            level_duration = level_duration_make(true, duration);
        } else if(duration == 0) { //-V547
            level_duration = level_duration_reset();
            FURI_LOG_I(TAG, "Stop transmission");
            instance->worker_stopping = true;
        }
        return level_duration;
    } else {
        instance->is_storage_slow = true;
        return level_duration_wait();
    }
}

/** Worker thread
 * 
 * @param context 
 * @return exit code 
 */
static int32_t subghz_file_encoder_worker_thread(void* context) {
    SubGhzFileEncoderWorker* instance = context;
    FURI_LOG_I(TAG, "Worker start");
    bool res = false;
    instance->is_storage_slow = false;
    Stream* stream = flipper_format_get_raw_stream(instance->flipper_format);
    do {
        if(!flipper_format_buffered_file_open_existing(
               instance->flipper_format, furi_string_get_cstr(instance->file_path))) {
            FURI_LOG_E(
                TAG,
                "Unable to open file for read: %s",
                furi_string_get_cstr(instance->file_path));
            break;
        }
        if(!flipper_format_read_string(instance->flipper_format, "Protocol", instance->str_data)) {
            FURI_LOG_E(TAG, "Missing Protocol");
            break;
        }

        //skip the end of the previous line "\n"
        stream_seek(stream, 1, StreamOffsetFromCurrent);
        res = true;
        instance->worker_stopping = false;
        FURI_LOG_I(TAG, "Start transmission");
    } while(0);

    SubGhzRawReader reader = {.stream = stream};
    while(res && instance->worker_running) {
        size_t stream_free_byte = furi_stream_buffer_spaces_available(instance->stream);
        // a line (up to 512 timings) is read when it has room
        if((stream_free_byte / sizeof(int32_t)) >= SUBGHZ_FILE_ENCODER_LOAD) {
            if(subghz_file_encoder_worker_line(instance, &reader) != SubGhzRawLineData) {
                subghz_file_encoder_worker_add_level_duration(instance, LEVEL_DURATION_RESET);
                break;
            }
        } else {
            furi_delay_ms(1);
        }
    }
    //waiting for the end of the transfer
    if(instance->is_storage_slow) {
        FURI_LOG_E(TAG, "Storage is slow");
    }

    FURI_LOG_I(TAG, "End read file");
    //nothing was put on the air when decoding, and is_async_complete_tx() never turns
    //true outside of a transmission, so this would spin until the worker is stopped
    while(!instance->is_decoding && instance->device &&
          !subghz_devices_is_async_complete_tx(instance->device) && instance->worker_running) {
        furi_delay_ms(5);
    }

    FURI_LOG_I(TAG, "End transmission");
    while(instance->worker_running) {
        if(instance->worker_stopping) {
            if(instance->callback_end) instance->callback_end(instance->context_end);
        }
        furi_delay_ms(50);
    }
    flipper_format_buffered_file_close(instance->flipper_format);

    FURI_LOG_I(TAG, "Worker stop");
    return 0;
}

SubGhzFileEncoderWorker* subghz_file_encoder_worker_alloc(void) {
    SubGhzFileEncoderWorker* instance = malloc(sizeof(SubGhzFileEncoderWorker));

    instance->thread =
        furi_thread_alloc_ex("SubGhzFEWorker", 2048, subghz_file_encoder_worker_thread, instance);
    // Two full lines of a RAW file (a line holds up to 512 timings; the worker reads one when
    // half is free): 8 KB here was what sending a saved RAW signal ran Sub-GHz out of memory
    // with, beside the history and the app. A line waits for room, so none is dropped
    instance->stream = furi_stream_buffer_alloc(
        sizeof(int32_t) * SUBGHZ_FILE_ENCODER_LOAD * 2, sizeof(int32_t));

    instance->storage = furi_record_open(RECORD_STORAGE);
    // buffered: a RAW_Data line is read 32 bytes at a time, one storage call each otherwise
    instance->flipper_format = flipper_format_buffered_file_alloc(instance->storage);

    instance->str_data = furi_string_alloc();
    instance->file_path = furi_string_alloc();
    instance->worker_stopping = true;
    instance->is_decoding = false;

    return instance;
}

void subghz_file_encoder_worker_free(SubGhzFileEncoderWorker* instance) {
    furi_assert(instance);

    furi_stream_buffer_free(instance->stream);
    furi_thread_free(instance->thread);

    furi_string_free(instance->str_data);
    furi_string_free(instance->file_path);

    flipper_format_free(instance->flipper_format);
    furi_record_close(RECORD_STORAGE);

    free(instance);
}

bool subghz_file_encoder_worker_start(
    SubGhzFileEncoderWorker* instance,
    const char* file_path,
    const char* radio_device_name) {
    furi_assert(instance);
    furi_assert(!instance->worker_running);

    furi_stream_buffer_reset(instance->stream);
    furi_string_set(instance->file_path, file_path);
    //without a radio the samples go to a decoder, not on the air
    instance->device = radio_device_name ? subghz_devices_get_by_name(radio_device_name) : NULL;
    instance->is_decoding = (instance->device == NULL);
    instance->worker_running = true;
    furi_thread_start(instance->thread);

    return true;
}

void subghz_file_encoder_worker_stop(SubGhzFileEncoderWorker* instance) {
    furi_assert(instance);
    furi_assert(instance->worker_running);

    instance->worker_running = false;
    furi_thread_join(instance->thread);
}

bool subghz_file_encoder_worker_is_running(SubGhzFileEncoderWorker* instance) {
    furi_assert(instance);
    return instance->worker_running;
}
