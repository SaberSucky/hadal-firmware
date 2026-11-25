#include "infrared_brute_force.h"

#include <furi.h>

#define TAG "InfraredBruteForce"

#include <stdlib.h>
#include <m-dict.h>
#include <m-array.h>
#include <flipper_format/flipper_format.h>

#include <storage/storage.h>

#include "infrared_learned.h"
#include "infrared_signal.h"
#include "infrared_signal_key.h"

ARRAY_DEF(SignalPositionArray, size_t, M_DEFAULT_OPLIST); //-V658

// A signal found while reading the database: where it is, whether it is a raw capture, and
// what it sends on the air. Eight bytes, as a database category has up to a few hundred: the
// key is folded to 32 bits (two codes of one button share one about once in 100000 runs).
typedef struct {
    uint32_t position : 31;
    uint32_t is_raw   : 1;
    uint32_t key;
} InfraredBruteForceCandidate;

// How often the free RAM is looked at while codes are collected, and what is left for the rest
#define IR_BF_MEMORY_CHECK_STEP 16
#define IR_BF_MEMORY_RESERVE    (6U * 1024U)

// Database files are far below 2 GB; a position that does not fit is never taken
#define IR_BF_CANDIDATE_POSITION_MAX (0x7FFFFFFFU)

_Static_assert(sizeof(InfraredBruteForceCandidate) == 8, "a candidate should stay 8 bytes");

ARRAY_DEF(InfraredBruteForceCandidateArray, InfraredBruteForceCandidate, M_POD_OPLIST); //-V658

typedef struct {
    size_t index;
    SignalPositionArray_t signals;
    // Only while infrared_brute_force_calculate_messages() runs; empty otherwise
    InfraredBruteForceCandidateArray_t candidates;
} InfraredBruteForceRecord;

static inline void ir_bf_record_init(InfraredBruteForceRecord* record) {
    record->index = 0;
    SignalPositionArray_init(record->signals);
    InfraredBruteForceCandidateArray_init(record->candidates);
}
#define IR_BF_RECORD_INIT(r) (ir_bf_record_init(&(r)))

static inline void
    ir_bf_record_init_set(InfraredBruteForceRecord* dest, const InfraredBruteForceRecord* src) {
    dest->index = src->index;
    SignalPositionArray_init_set(dest->signals, src->signals);
    InfraredBruteForceCandidateArray_init_set(dest->candidates, src->candidates);
}
#define IR_BF_RECORD_INIT_SET(d, s) (ir_bf_record_init_set(&(d), &(s)))

static inline void
    ir_bf_record_set(InfraredBruteForceRecord* dest, const InfraredBruteForceRecord* src) {
    dest->index = src->index;
    SignalPositionArray_set(dest->signals, src->signals);
    InfraredBruteForceCandidateArray_set(dest->candidates, src->candidates);
}
#define IR_BF_RECORD_SET(d, s) (ir_bf_record_set(&(d), &(s)))

static inline void ir_bf_record_clear(InfraredBruteForceRecord* record) {
    SignalPositionArray_clear(record->signals);
    InfraredBruteForceCandidateArray_clear(record->candidates);
}
#define IR_BF_RECORD_CLEAR(r) (ir_bf_record_clear(&(r)))

#define IR_BF_RECORD_OPLIST           \
    (INIT(IR_BF_RECORD_INIT),         \
     INIT_SET(IR_BF_RECORD_INIT_SET), \
     SET(IR_BF_RECORD_SET),           \
     CLEAR(IR_BF_RECORD_CLEAR))

DICT_DEF2(
    InfraredBruteForceRecordDict,
    FuriString*,
    FURI_STRING_OPLIST,
    InfraredBruteForceRecord,
    IR_BF_RECORD_OPLIST);

struct InfraredBruteForce {
    FlipperFormat* ff;
    const char* db_filename;
    FuriString* current_record_name;
    InfraredBruteForceRecord current_record;
    InfraredSignal* current_signal;
    InfraredBruteForceRecordDict_t records;
    bool is_started;
    InfraredTxSpeed speed;
};

InfraredBruteForce* infrared_brute_force_alloc(void) {
    InfraredBruteForce* brute_force = malloc(sizeof(InfraredBruteForce));
    brute_force->ff = NULL;
    brute_force->db_filename = NULL;
    brute_force->current_signal = NULL;
    brute_force->is_started = false;
    brute_force->current_record_name = furi_string_alloc();
    InfraredBruteForceRecordDict_init(brute_force->records);
    brute_force->speed = InfraredTxSpeedFast;
    return brute_force;
}

void infrared_brute_force_free(InfraredBruteForce* brute_force) {
    furi_check(brute_force);
    furi_assert(!brute_force->is_started);
    InfraredBruteForceRecordDict_clear(brute_force->records);
    furi_string_free(brute_force->current_record_name);
    free(brute_force);
}

void infrared_brute_force_set_db_filename(InfraredBruteForce* brute_force, const char* db_filename) {
    furi_check(brute_force);
    furi_assert(!brute_force->is_started);
    brute_force->db_filename = db_filename;
}

/* ------------------------------------------------------------------ */
/* What a signal sends                                                 */
/* ------------------------------------------------------------------ */

static uint32_t ir_bf_signal_key(const InfraredSignal* signal, InfraredDecoderHandler* decoder) {
    uint64_t key;
    if(infrared_signal_is_raw(signal)) {
        const InfraredRawSignal* raw = infrared_signal_get_raw_signal(signal);
        key = infrared_signal_key_raw(raw->timings, raw->timings_size, raw->frequency, decoder);
    } else {
        key = infrared_signal_key_message(infrared_signal_get_message(signal));
    }
    return (uint32_t)(key ^ (key >> 32));
}

/* ------------------------------------------------------------------ */
/* Sending order                                                       */
/* ------------------------------------------------------------------ */

#define IR_BF_ORDER_FILETYPE     "IR library order"
#define IR_BF_ORDER_VERSION      (1U)
#define IR_BF_ORDER_LIBRARY_SIZE "Library size"

// The order file next to a database ("tv.ir" -> "tv.order") lists, per button, the indices of
// its signals (0 = the button's first signal in the file) in the order to send them: the codes
// most remotes use first. It is made for one exact database file and ignored if the database's
// size differs, or if a button's list is not a permutation of its signals.
static FlipperFormat* ir_bf_order_open(Storage* storage, const char* db_filename) {
    const size_t len = strlen(db_filename);
    if(len < 3 || strcmp(db_filename + len - 3, ".ir") != 0) return NULL;

    FuriString* path = furi_string_alloc_set(db_filename);
    furi_string_left(path, len - 3);
    furi_string_cat_str(path, ".order");

    FlipperFormat* ff = flipper_format_buffered_file_alloc(storage);
    FuriString* filetype = furi_string_alloc();
    FileInfo info;
    uint32_t version = 0, library_size = 0;
    bool valid = false;

    do {
        if(storage_common_stat(storage, db_filename, &info) != FSE_OK) break;
        if(!flipper_format_buffered_file_open_existing(ff, furi_string_get_cstr(path))) break;
        if(!flipper_format_read_header(ff, filetype, &version)) break;
        if(!furi_string_equal_str(filetype, IR_BF_ORDER_FILETYPE)) break;
        if(version != IR_BF_ORDER_VERSION) break;
        if(!flipper_format_read_uint32(ff, IR_BF_ORDER_LIBRARY_SIZE, &library_size, 1)) break;
        if(library_size != info.size) break;
        valid = true;
    } while(false);

    furi_string_free(filetype);
    furi_string_free(path);
    if(!valid) {
        flipper_format_free(ff);
        ff = NULL;
    }
    return ff;
}

// The sending order of one button: order[i] is the candidate sent i-th. Falls back to file
// order when the order file has nothing valid for the button.
static void
    ir_bf_order_read(FlipperFormat* order_ff, const char* name, uint32_t* order, size_t count) {
    for(size_t i = 0; i < count; i++) {
        order[i] = i;
    }
    if(!order_ff || count == 0) return;

    uint32_t listed = 0;
    if(!flipper_format_rewind(order_ff)) return;
    if(!flipper_format_get_value_count(order_ff, name, &listed) || listed != count) return;
    if(!flipper_format_rewind(order_ff)) return;

    uint32_t* wanted = malloc(sizeof(uint32_t) * count);
    uint8_t* used = malloc((count + 7) / 8);
    memset(used, 0, (count + 7) / 8);
    bool valid = flipper_format_read_uint32(order_ff, name, wanted, count);
    for(size_t i = 0; valid && i < count; i++) {
        const uint32_t index = wanted[i];
        if(index >= count || (used[index / 8] & (1U << (index % 8)))) {
            valid = false;
        } else {
            used[index / 8] |= 1U << (index % 8);
        }
    }
    if(valid) memcpy(order, wanted, sizeof(uint32_t) * count);
    free(used);
    free(wanted);
}

/* ------------------------------------------------------------------ */
/* Codes that worked: sent first next time                             */
/* ------------------------------------------------------------------ */

#define IR_BF_LEARNED_DIR      EXT_PATH("apps_data/infrared")
#define IR_BF_LEARNED_PATH     IR_BF_LEARNED_DIR "/learned.txt"
#define IR_BF_LEARNED_MAX_SIZE (16U * 1024U)
// Learned codes moved to the front of one button's list
#define IR_BF_LEARNED_FRONT (8U)

// The whole learned file, or NULL
static char* ir_bf_learned_read(Storage* storage) {
    File* file = storage_file_alloc(storage);
    char* text = NULL;
    if(storage_file_open(file, IR_BF_LEARNED_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        const uint64_t size = storage_file_size(file);
        // short of RAM, the order the user's choices taught is left out, not the remote
        if(size <= IR_BF_LEARNED_MAX_SIZE &&
           memmgr_heap_get_max_free_block() > (size_t)size + IR_BF_MEMORY_RESERVE) {
            text = malloc((size_t)size + 1);
            const size_t read = storage_file_read(file, text, (size_t)size);
            text[read] = '\0';
        }
    }
    storage_file_free(file);
    return text;
}

static bool ir_bf_learned_write(Storage* storage, const char* text) {
    storage_simply_mkdir(storage, IR_BF_LEARNED_DIR);
    File* file = storage_file_alloc(storage);
    const size_t length = strlen(text);
    bool success = storage_file_open(file, IR_BF_LEARNED_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS) &&
                   storage_file_write(file, text, length) == length;
    storage_file_free(file);
    return success;
}

// "/ext/infrared/assets/tv.ir" -> "tv.ir"
static const char* ir_bf_library_name(const char* db_filename) {
    const char* slash = strrchr(db_filename, '/');
    return slash ? slash + 1 : db_filename;
}

// Move the codes that worked before to the front, best first; the rest keep their order
static void ir_bf_learned_front(
    InfraredBruteForceCandidate* kept,
    size_t kept_count,
    const char* learned_text,
    const char* library,
    const char* button) {
    if(!learned_text || !kept_count) return;
    InfraredLearnedCode codes[IR_BF_LEARNED_FRONT];
    const size_t count =
        infrared_learned_find(learned_text, library, button, codes, IR_BF_LEARNED_FRONT);
    size_t front = 0;
    for(size_t c = 0; c < count; c++) {
        for(size_t k = front; k < kept_count; k++) {
            if(kept[k].key != codes[c].key) continue;
            const InfraredBruteForceCandidate moved = kept[k];
            memmove(&kept[front + 1], &kept[front], (k - front) * sizeof(InfraredBruteForceCandidate));
            kept[front++] = moved;
            break;
        }
    }
}

// Fill the record's signal list from its candidates, in sending order, without a code that
// was already in the list (the database often has one code several times: a model's remote
// was added twice, or as NEC and as NECext, or once parsed and once raw). Of a parsed signal
// and a raw capture of the same code, the parsed one is sent (clean timings), in the place
// of whichever came first.
static void ir_bf_record_finish(
    InfraredBruteForceRecord* record,
    const char* name,
    FlipperFormat* order_ff,
    const char* learned_text,
    const char* library) {
    const size_t count = InfraredBruteForceCandidateArray_size(record->candidates);
    uint32_t* order = malloc(sizeof(uint32_t) * (count ? count : 1));
    ir_bf_order_read(order_ff, name, order, count);

    InfraredBruteForceCandidate* kept =
        malloc(sizeof(InfraredBruteForceCandidate) * (count ? count : 1));
    size_t kept_count = 0;

    for(size_t i = 0; i < count; i++) {
        const InfraredBruteForceCandidate* candidate =
            InfraredBruteForceCandidateArray_cget(record->candidates, order[i]);
        size_t k = 0;
        while(k < kept_count && kept[k].key != candidate->key) {
            k++;
        }
        if(k == kept_count) {
            kept[kept_count++] = *candidate;
        } else if(kept[k].is_raw && !candidate->is_raw) {
            kept[k] = *candidate;
        }
    }
    // each block goes as soon as it is done with: a button of hundreds of codes held its order,
    // its candidates, what it kept and its positions all at once
    free(order);
    InfraredBruteForceCandidateArray_clear(record->candidates);
    InfraredBruteForceCandidateArray_init(record->candidates);

    ir_bf_learned_front(kept, kept_count, learned_text, library, name);

    // one allocation of the right size, not doubling up to it
    SignalPositionArray_reset(record->signals);
    SignalPositionArray_reserve(record->signals, kept_count);
    for(size_t k = 0; k < kept_count; k++) {
        SignalPositionArray_push_back(record->signals, kept[k].position);
    }

    free(kept);
}

InfraredErrorCode infrared_brute_force_calculate_messages(InfraredBruteForce* brute_force) {
    furi_check(brute_force);
    furi_assert(!brute_force->is_started);
    // A check, not an assert: on a release build a missing filename would reach
    // flipper_format_buffered_file_open_existing() as NULL
    furi_check(brute_force->db_filename);
    InfraredErrorCode error = InfraredErrorCodeNone;

    Storage* storage = furi_record_open(RECORD_STORAGE);
    FlipperFormat* ff = flipper_format_buffered_file_alloc(storage);
    FuriString* signal_name = furi_string_alloc();
    InfraredSignal* signal = infrared_signal_alloc();
    InfraredDecoderHandler* decoder = infrared_alloc_decoder();

    // Start every record empty, so that a second calculation does not add to the first
    InfraredBruteForceRecordDict_it_t it;
    for(InfraredBruteForceRecordDict_it(it, brute_force->records);
        !InfraredBruteForceRecordDict_end_p(it);
        InfraredBruteForceRecordDict_next(it)) {
        InfraredBruteForceRecord* record = &InfraredBruteForceRecordDict_ref(it)->value;
        SignalPositionArray_reset(record->signals);
        InfraredBruteForceCandidateArray_reset(record->candidates);
    }

    do {
        if(!flipper_format_buffered_file_open_existing(ff, brute_force->db_filename)) {
            error = InfraredErrorCodeFileOperationFailed;
            break;
        }

        while(infrared_signal_read_name(ff, signal_name) == InfraredErrorCodeNone) {
            // A database holds many names no category asked for (e.g. ~600 of tv.ir's
            // 613). Look the name up first and skip the body of the ones we will never
            // send: read_name() scans past the unparsed body to the next entry, so only
            // the handful of registered signals are parsed and validated here.
            InfraredBruteForceRecord* record =
                InfraredBruteForceRecordDict_get(brute_force->records, signal_name);
            if(!record) continue;

            const size_t signal_start = flipper_format_tell(ff);
            if(signal_start > IR_BF_CANDIDATE_POSITION_MAX) {
                error = InfraredErrorCodeFileOperationFailed;
                break;
            }

            error = infrared_signal_read_body(signal, ff);
            if(INFRARED_ERROR_PRESENT(error)) break;

            // read_body() rejects everything is_valid() catches; kept so that a reader which
            // one day does not cannot put an unvalidated position into the dictionary
            if(!infrared_signal_is_valid(signal)) {
                error = InfraredErrorCodeSignalRawIsInvalid;
                break;
            }

            InfraredBruteForceCandidate candidate = {
                .position = (uint32_t)signal_start,
                .is_raw = infrared_signal_is_raw(signal),
                .key = ir_bf_signal_key(signal, decoder),
            };
            // A button with hundreds of codes grows its list by reallocating, and finishing it
            // takes as much again (its order, what it keeps, its positions); with the RAM nearly
            // gone that failed, and a failed allocation stops the Flipper. Short of RAM, the
            // button keeps the codes it has (most-used first still comes from them).
            const size_t held = InfraredBruteForceCandidateArray_size(record->candidates);
            if(held % IR_BF_MEMORY_CHECK_STEP == 0 &&
               memmgr_heap_get_max_free_block() <
                   held * sizeof(InfraredBruteForceCandidate) * 4 + IR_BF_MEMORY_RESERVE) {
                FURI_LOG_W(
                    TAG,
                    "Low on RAM: %s keeps %zu codes",
                    furi_string_get_cstr(signal_name),
                    held);
                continue;
            }
            InfraredBruteForceCandidateArray_push_back(record->candidates, candidate);
        }
    } while(false);

    // Put each record's signals in sending order, once each (on error the records stay empty)
    FlipperFormat* order_ff =
        INFRARED_ERROR_PRESENT(error) ? NULL : ir_bf_order_open(storage, brute_force->db_filename);
    char* learned_text = INFRARED_ERROR_PRESENT(error) ? NULL : ir_bf_learned_read(storage);
    const char* library = ir_bf_library_name(brute_force->db_filename);
    for(InfraredBruteForceRecordDict_it(it, brute_force->records);
        !InfraredBruteForceRecordDict_end_p(it);
        InfraredBruteForceRecordDict_next(it)) {
        InfraredBruteForceRecordDict_itref_t* ref = InfraredBruteForceRecordDict_ref(it);
        if(INFRARED_ERROR_PRESENT(error)) {
            InfraredBruteForceCandidateArray_reset(ref->value.candidates);
        } else {
            ir_bf_record_finish(
                &ref->value, furi_string_get_cstr(ref->key), order_ff, learned_text, library);
        }
    }
    if(order_ff) flipper_format_free(order_ff);
    free(learned_text);

    infrared_free_decoder(decoder);
    infrared_signal_free(signal);
    furi_string_free(signal_name);

    flipper_format_free(ff);
    furi_record_close(RECORD_STORAGE);
    return error;
}

bool infrared_brute_force_start(
    InfraredBruteForce* brute_force,
    uint32_t index,
    uint32_t* record_count) {
    furi_check(brute_force);
    furi_assert(!brute_force->is_started);
    bool success = false;
    *record_count = 0;

    InfraredBruteForceRecordDict_it_t it;
    for(InfraredBruteForceRecordDict_it(it, brute_force->records);
        !InfraredBruteForceRecordDict_end_p(it);
        InfraredBruteForceRecordDict_next(it)) {
        const InfraredBruteForceRecordDict_itref_t* record = InfraredBruteForceRecordDict_cref(it);
        if(record->value.index == index) {
            *record_count = SignalPositionArray_size(record->value.signals);
            if(*record_count) {
                furi_string_set(brute_force->current_record_name, record->key);
                brute_force->current_record = record->value;
            }
            break;
        }
    }

    if(*record_count) {
        Storage* storage = furi_record_open(RECORD_STORAGE);
        brute_force->ff = flipper_format_buffered_file_alloc(storage);
        brute_force->current_signal = infrared_signal_alloc();
        brute_force->is_started = true;
        success =
            flipper_format_buffered_file_open_existing(brute_force->ff, brute_force->db_filename);
        if(!success) infrared_brute_force_stop(brute_force);
    }
    return success;
}

bool infrared_brute_force_is_started(const InfraredBruteForce* brute_force) {
    furi_check(brute_force);
    return brute_force->is_started;
}

void infrared_brute_force_stop(InfraredBruteForce* brute_force) {
    furi_check(brute_force);
    furi_assert(brute_force->is_started);
    furi_string_reset(brute_force->current_record_name);
    infrared_signal_free(brute_force->current_signal);
    flipper_format_free(brute_force->ff);
    brute_force->current_signal = NULL;
    brute_force->ff = NULL;
    brute_force->is_started = false;
    furi_record_close(RECORD_STORAGE);
}

bool infrared_brute_force_load_signal(
    InfraredBruteForce* brute_force,
    uint32_t signal_index,
    InfraredSignal* signal) {
    furi_check(brute_force);
    furi_check(signal);
    furi_assert(brute_force->is_started);

    if(signal_index >= SignalPositionArray_size(brute_force->current_record.signals)) return false;

    size_t signal_start =
        *SignalPositionArray_cget(brute_force->current_record.signals, signal_index);
    if(!flipper_format_seek(brute_force->ff, signal_start, FlipperFormatOffsetFromStart))
        return false;

    return !INFRARED_ERROR_PRESENT(infrared_signal_read_body(signal, brute_force->ff));
}

const char* infrared_brute_force_get_current_record_name(const InfraredBruteForce* brute_force) {
    furi_check(brute_force);
    return furi_string_get_cstr(brute_force->current_record_name);
}

bool infrared_brute_force_send(InfraredBruteForce* brute_force, uint32_t signal_index) {
    furi_check(brute_force);
    furi_assert(brute_force->is_started);

    if(!infrared_brute_force_load_signal(brute_force, signal_index, brute_force->current_signal))
        return false;

    infrared_signal_transmit_at(brute_force->current_signal, brute_force->speed);
    return true;
}

bool infrared_brute_force_learn(InfraredBruteForce* brute_force, uint32_t signal_index, bool saved) {
    furi_check(brute_force);
    furi_assert(brute_force->is_started);
    if(!brute_force->db_filename) return false;

    InfraredSignal* signal = infrared_signal_alloc();
    bool success = infrared_brute_force_load_signal(brute_force, signal_index, signal);
    if(success) {
        // the decoder is only needed to read a raw capture
        InfraredDecoderHandler* decoder =
            infrared_signal_is_raw(signal) ? infrared_alloc_decoder() : NULL;
        const uint32_t key = ir_bf_signal_key(signal, decoder);
        if(decoder) infrared_free_decoder(decoder);

        Storage* storage = furi_record_open(RECORD_STORAGE);
        char* text = ir_bf_learned_read(storage);
        char* updated = infrared_learned_bump(
            text ? text : "",
            ir_bf_library_name(brute_force->db_filename),
            furi_string_get_cstr(brute_force->current_record_name),
            key,
            saved ? InfraredLearnedSaved : InfraredLearnedSentAgain);
        success = updated && ir_bf_learned_write(storage, updated);
        free(updated);
        free(text);
        furi_record_close(RECORD_STORAGE);
    }
    infrared_signal_free(signal);
    return success;
}

void infrared_brute_force_add_record(
    InfraredBruteForce* brute_force,
    uint32_t index,
    const char* name) {
    InfraredBruteForceRecord value;
    ir_bf_record_init(&value);
    value.index = index;
    FuriString* key;
    key = furi_string_alloc_set(name);
    InfraredBruteForceRecordDict_set_at(brute_force->records, key, value);
    furi_string_free(key);
}

void infrared_brute_force_reset(InfraredBruteForce* brute_force) {
    furi_check(brute_force);
    furi_assert(!brute_force->is_started);
    // Otherwise a caller that forgets to set it next time silently re-reads this database
    brute_force->db_filename = NULL;
    InfraredBruteForceRecordDict_reset(brute_force->records);
}

void infrared_brute_force_set_fast(InfraredBruteForce* brute_force, bool fast) {
    infrared_brute_force_set_speed(
        brute_force, fast ? InfraredTxSpeedFast : InfraredTxSpeedNormal);
}

void infrared_brute_force_set_speed(InfraredBruteForce* brute_force, InfraredTxSpeed speed) {
    furi_check(brute_force);
    furi_check(speed <= InfraredTxSpeedTurbo);
    brute_force->speed = speed;
}

/* ------------------------------------------------------------------ */
/* Remote Finder                                                       */
/* ------------------------------------------------------------------ */

typedef struct {
    InfraredSignal* signal;
    FuriString* name;
} InfraredBruteForceDeviceItem;

static inline void ir_bf_device_item_init(InfraredBruteForceDeviceItem* item) {
    item->signal = infrared_signal_alloc();
    item->name = furi_string_alloc();
}
#define IR_BF_DEVICE_ITEM_INIT(i) (ir_bf_device_item_init(&(i)))

static inline void ir_bf_device_item_init_set(
    InfraredBruteForceDeviceItem* dest,
    const InfraredBruteForceDeviceItem* src) {
    dest->signal = infrared_signal_alloc();
    infrared_signal_set_signal(dest->signal, src->signal);
    dest->name = furi_string_alloc_set(src->name);
}
#define IR_BF_DEVICE_ITEM_INIT_SET(d, s) (ir_bf_device_item_init_set(&(d), &(s)))

static inline void ir_bf_device_item_set(
    InfraredBruteForceDeviceItem* dest,
    const InfraredBruteForceDeviceItem* src) {
    infrared_signal_set_signal(dest->signal, src->signal);
    furi_string_set(dest->name, src->name);
}
#define IR_BF_DEVICE_ITEM_SET(d, s) (ir_bf_device_item_set(&(d), &(s)))

static inline void ir_bf_device_item_clear(InfraredBruteForceDeviceItem* item) {
    infrared_signal_free(item->signal);
    furi_string_free(item->name);
}
#define IR_BF_DEVICE_ITEM_CLEAR(i) (ir_bf_device_item_clear(&(i)))

#define IR_BF_DEVICE_ITEM_OPLIST           \
    (INIT(IR_BF_DEVICE_ITEM_INIT),         \
     INIT_SET(IR_BF_DEVICE_ITEM_INIT_SET), \
     SET(IR_BF_DEVICE_ITEM_SET),           \
     CLEAR(IR_BF_DEVICE_ITEM_CLEAR))

ARRAY_DEF(InfraredBruteForceDeviceItemArray, InfraredBruteForceDeviceItem, IR_BF_DEVICE_ITEM_OPLIST)

struct InfraredBruteForceDevice {
    InfraredBruteForceDeviceItemArray_t items;
    FuriString* suggested_name;
};

InfraredBruteForceDevice* infrared_brute_force_device_alloc(void) {
    InfraredBruteForceDevice* device = malloc(sizeof(InfraredBruteForceDevice));
    InfraredBruteForceDeviceItemArray_init(device->items);
    device->suggested_name = furi_string_alloc();
    return device;
}

void infrared_brute_force_device_free(InfraredBruteForceDevice* device) {
    furi_check(device);
    InfraredBruteForceDeviceItemArray_clear(device->items);
    furi_string_free(device->suggested_name);
    free(device);
}

void infrared_brute_force_device_reset(InfraredBruteForceDevice* device) {
    furi_check(device);
    InfraredBruteForceDeviceItemArray_reset(device->items);
    furi_string_reset(device->suggested_name);
}

size_t infrared_brute_force_device_get_count(const InfraredBruteForceDevice* device) {
    furi_check(device);
    return InfraredBruteForceDeviceItemArray_size(device->items);
}

const InfraredSignal*
    infrared_brute_force_device_get_signal(const InfraredBruteForceDevice* device, size_t index) {
    furi_check(device);
    if(index >= InfraredBruteForceDeviceItemArray_size(device->items)) return NULL;
    return InfraredBruteForceDeviceItemArray_cget(device->items, index)->signal;
}

const char*
    infrared_brute_force_device_get_name(const InfraredBruteForceDevice* device, size_t index) {
    furi_check(device);
    if(index >= InfraredBruteForceDeviceItemArray_size(device->items)) return NULL;
    return furi_string_get_cstr(
        InfraredBruteForceDeviceItemArray_cget(device->items, index)->name);
}

const char*
    infrared_brute_force_device_get_suggested_name(const InfraredBruteForceDevice* device) {
    furi_check(device);
    return furi_string_get_cstr(device->suggested_name);
}

// One entry of the on-demand library index. Names are not stored here: they are
// read back from the file by offset only for the few entries a run touches.
typedef struct {
    size_t name_offset; /**< Stream position of the signal's "name:" line. */
    size_t body_offset; /**< Stream position of the signal body (matches load_signal). */
    bool is_raw;
    InfraredProtocol protocol;
    uint32_t address;
} InfraredBruteForceIndexEntry;

ARRAY_DEF(InfraredBruteForceIndexArray, InfraredBruteForceIndexEntry, M_POD_OPLIST)

// Two entries belong to the same device only if both are raw, or both are parsed
// with the same protocol and address. The command differs per button, so ignore it.
static bool ir_bf_index_same_device(
    const InfraredBruteForceIndexEntry* a,
    const InfraredBruteForceIndexEntry* b) {
    if(a->is_raw || b->is_raw) return a->is_raw && b->is_raw;
    return a->protocol == b->protocol && a->address == b->address;
}

// Read just the button name of the entry at name_offset into out_name.
static bool ir_bf_read_entry_name(FlipperFormat* ff, size_t name_offset, FuriString* out_name) {
    if(!flipper_format_seek(ff, name_offset, FlipperFormatOffsetFromStart)) return false;
    return infrared_signal_read_name(ff, out_name) == InfraredErrorCodeNone;
}

// True if name already appears among the run's names; a repeat marks a device boundary.
static bool ir_bf_name_seen(FuriString** seen, size_t seen_count, const FuriString* name) {
    for(size_t i = 0; i < seen_count; i++) {
        if(furi_string_equal(seen[i], name)) return true;
    }
    return false;
}

size_t infrared_brute_force_collect_device(
    InfraredBruteForce* brute_force,
    uint32_t signal_index,
    size_t max_signals,
    InfraredBruteForceDevice* device) {
    furi_check(brute_force);
    furi_check(device);
    furi_assert(brute_force->is_started);

    infrared_brute_force_device_reset(device);

    if(max_signals == 0) return 0;
    if(signal_index >= SignalPositionArray_size(brute_force->current_record.signals)) return 0;
    const size_t target_offset =
        *SignalPositionArray_cget(brute_force->current_record.signals, signal_index);

    // The database is open already, for sending: read through that handle. A second one would
    // never open, as storage waits for a file's other handle to close first, and the app froze
    // (load_signal() seeks to each signal, so where this leaves the handle does not matter)
    FlipperFormat* ff = brute_force->ff;
    InfraredBruteForceIndexArray_t index;
    InfraredBruteForceIndexArray_init(index);
    InfraredSignal* signal = infrared_signal_alloc();
    FuriString* name = furi_string_alloc();

    // seen[] holds copies of the names already in the run (capped at max_signals)
    FuriString** seen = malloc(sizeof(FuriString*) * max_signals);
    size_t seen_count = 0;
    size_t collected = 0;

    do {
        if(!flipper_format_rewind(ff)) break;

        // Pass 1: index every signal's position and device key (no names, no body kept)
        size_t target_pos = (size_t)-1;
        while(true) {
            const size_t name_offset = flipper_format_tell(ff);
            if(infrared_signal_read_name(ff, name) != InfraredErrorCodeNone) break;
            const size_t body_offset = flipper_format_tell(ff);
            if(INFRARED_ERROR_PRESENT(infrared_signal_read_body(signal, ff))) break;

            InfraredBruteForceIndexEntry entry = {
                .name_offset = name_offset,
                .body_offset = body_offset,
                .is_raw = infrared_signal_is_raw(signal),
            };
            if(!entry.is_raw) {
                const InfraredMessage* message = infrared_signal_get_message(signal);
                entry.protocol = message->protocol;
                entry.address = message->address;
            }
            if(body_offset == target_offset) {
                target_pos = InfraredBruteForceIndexArray_size(index);
            }
            InfraredBruteForceIndexArray_push_back(index, entry);
        }

        const size_t count = InfraredBruteForceIndexArray_size(index);
        if(target_pos == (size_t)-1) break; // confirmed signal not found in the file

        const InfraredBruteForceIndexEntry* target =
            InfraredBruteForceIndexArray_cget(index, target_pos);

        // Pass 2: grow a run of the same device around the target, stopping at a
        // key change or a repeated button name, capped at max_signals.
        if(!ir_bf_read_entry_name(ff, target->name_offset, name)) break;
        seen[seen_count++] = furi_string_alloc_set(name);
        size_t lo = target_pos, hi = target_pos;

        while(lo > 0 && seen_count < max_signals) {
            const InfraredBruteForceIndexEntry* e =
                InfraredBruteForceIndexArray_cget(index, lo - 1);
            if(!ir_bf_index_same_device(e, target)) break;
            if(!ir_bf_read_entry_name(ff, e->name_offset, name)) break;
            if(ir_bf_name_seen(seen, seen_count, name)) break;
            seen[seen_count++] = furi_string_alloc_set(name);
            lo--;
        }
        while(hi + 1 < count && seen_count < max_signals) {
            const InfraredBruteForceIndexEntry* e =
                InfraredBruteForceIndexArray_cget(index, hi + 1);
            if(!ir_bf_index_same_device(e, target)) break;
            if(!ir_bf_read_entry_name(ff, e->name_offset, name)) break;
            if(ir_bf_name_seen(seen, seen_count, name)) break;
            seen[seen_count++] = furi_string_alloc_set(name);
            hi++;
        }

        // Pass 3: read the run's names and bodies in file order into the collection
        for(size_t i = lo; i <= hi; i++) {
            const InfraredBruteForceIndexEntry* e = InfraredBruteForceIndexArray_cget(index, i);
            if(!flipper_format_seek(ff, e->name_offset, FlipperFormatOffsetFromStart)) break;
            if(infrared_signal_read_name(ff, name) != InfraredErrorCodeNone) break;
            if(INFRARED_ERROR_PRESENT(infrared_signal_read_body(signal, ff))) break;

            InfraredBruteForceDeviceItem* item =
                InfraredBruteForceDeviceItemArray_push_new(device->items);
            infrared_signal_set_signal(item->signal, signal);
            furi_string_set(item->name, name);
            collected++;
        }

        furi_string_set(device->suggested_name, brute_force->current_record_name);
    } while(false);

    for(size_t i = 0; i < seen_count; i++) {
        furi_string_free(seen[i]);
    }
    free(seen);
    furi_string_free(name);
    infrared_signal_free(signal);
    InfraredBruteForceIndexArray_clear(index);

    if(collected == 0) infrared_brute_force_device_reset(device);
    return collected;
}
