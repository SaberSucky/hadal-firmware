#include "infrared_brute_force.h"

#include <stdlib.h>
#include <m-dict.h>
#include <m-array.h>
#include <flipper_format/flipper_format.h>

#include "infrared_signal.h"

ARRAY_DEF(SignalPositionArray, size_t, M_DEFAULT_OPLIST); //-V658

typedef struct {
    size_t index;
    SignalPositionArray_t signals;
} InfraredBruteForceRecord;

static inline void ir_bf_record_init(InfraredBruteForceRecord* record) {
    record->index = 0;
    SignalPositionArray_init(record->signals);
}
#define IR_BF_RECORD_INIT(r) (ir_bf_record_init(&(r)))

static inline void
    ir_bf_record_init_set(InfraredBruteForceRecord* dest, const InfraredBruteForceRecord* src) {
    dest->index = src->index;
    SignalPositionArray_init_set(dest->signals, src->signals);
}
#define IR_BF_RECORD_INIT_SET(d, s) (ir_bf_record_init_set(&(d), &(s)))

static inline void
    ir_bf_record_set(InfraredBruteForceRecord* dest, const InfraredBruteForceRecord* src) {
    dest->index = src->index;
    SignalPositionArray_set(dest->signals, src->signals);
}
#define IR_BF_RECORD_SET(d, s) (ir_bf_record_set(&(d), &(s)))

static inline void ir_bf_record_clear(InfraredBruteForceRecord* record) {
    SignalPositionArray_clear(record->signals);
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
    bool is_fast;
};

InfraredBruteForce* infrared_brute_force_alloc(void) {
    InfraredBruteForce* brute_force = malloc(sizeof(InfraredBruteForce));
    brute_force->ff = NULL;
    brute_force->db_filename = NULL;
    brute_force->current_signal = NULL;
    brute_force->is_started = false;
    brute_force->current_record_name = furi_string_alloc();
    InfraredBruteForceRecordDict_init(brute_force->records);
    brute_force->is_fast = true;
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

    do {
        if(!flipper_format_buffered_file_open_existing(ff, brute_force->db_filename)) {
            error = InfraredErrorCodeFileOperationFailed;
            break;
        }

        while(infrared_signal_read_name(ff, signal_name) == InfraredErrorCodeNone) {
            const size_t signal_start = flipper_format_tell(ff);

            error = infrared_signal_read_body(signal, ff);
            if(INFRARED_ERROR_PRESENT(error)) break;

            // read_body() rejects everything is_valid() catches; kept so that a reader which
            // one day does not cannot put an unvalidated position into the dictionary
            if(!infrared_signal_is_valid(signal)) {
                error = InfraredErrorCodeSignalRawIsInvalid;
                break;
            }

            InfraredBruteForceRecord* record =
                InfraredBruteForceRecordDict_get(brute_force->records, signal_name);
            // A database holds names the caller never asked for; those are not ours to collect
            if(record) SignalPositionArray_push_back(record->signals, signal_start);
        }
    } while(false);

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

    if(brute_force->is_fast) {
        infrared_signal_transmit_fast(brute_force->current_signal);
    } else {
        infrared_signal_transmit(brute_force->current_signal);
    }
    return true;
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
    furi_check(brute_force);
    brute_force->is_fast = fast;
}

/* ------------------------------------------------------------------ */
/* Remote Finder                                                       */
/* ------------------------------------------------------------------ */

typedef struct {
    InfraredSignal* signal;
    FuriString* name;
} InfraredBruteForceDeviceItem;

static void ir_bf_device_item_init(InfraredBruteForceDeviceItem* item) {
    item->signal = infrared_signal_alloc();
    item->name = furi_string_alloc();
}

static void ir_bf_device_item_clear(InfraredBruteForceDeviceItem* item) {
    infrared_signal_free(item->signal);
    furi_string_free(item->name);
}

// m-array moves items by value on growth, so no INIT_SET/SET is provided: the
// collection only ever uses push_new()/reset(), which call INIT and CLEAR.
#define IR_BF_DEVICE_ITEM_OPLIST (INIT(API_2(ir_bf_device_item_init)), CLEAR(API_2(ir_bf_device_item_clear)))

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
    return furi_string_get_cstr(InfraredBruteForceDeviceItemArray_cget(device->items, index)->name);
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

    Storage* storage = furi_record_open(RECORD_STORAGE);
    FlipperFormat* ff = flipper_format_buffered_file_alloc(storage);
    InfraredBruteForceIndexArray_t index;
    InfraredBruteForceIndexArray_init(index);
    InfraredSignal* signal = infrared_signal_alloc();
    FuriString* name = furi_string_alloc();

    // seen[] holds copies of the names already in the run (capped at max_signals)
    FuriString** seen = malloc(sizeof(FuriString*) * max_signals);
    size_t seen_count = 0;
    size_t collected = 0;

    do {
        if(!flipper_format_buffered_file_open_existing(ff, brute_force->db_filename)) break;

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
    flipper_format_free(ff);
    furi_record_close(RECORD_STORAGE);

    if(collected == 0) infrared_brute_force_device_reset(device);
    return collected;
}
