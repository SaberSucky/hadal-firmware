/**
 * @file infrared_brute_force.h
 * @brief Infrared signal brute-forcing library.
 *
 * The BruteForce library is used to send large quantities of signals,
 * sorted by a category. It is used to implement the Universal Remote
 * feature.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "infrared_error_code.h"
#include "infrared_signal.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief InfraredBruteForce opaque type declaration.
 */
typedef struct InfraredBruteForce InfraredBruteForce;

/**
 * @brief Create a new InfraredBruteForce instance.
 *
 * @returns pointer to the created instance.
 */
InfraredBruteForce* infrared_brute_force_alloc(void);

/**
 * @brief Delete an InfraredBruteForce instance.
 *
 * @param[in,out] brute_force pointer to the instance to be deleted.
 */
void infrared_brute_force_free(InfraredBruteForce* brute_force);

/**
 * @brief Set an InfraredBruteForce instance to use a signal database contained in a file.
 *
 * @param[in,out] brute_force pointer to the instance to be configured.
 * @param[in] db_filename pointer to a zero-terminated string containing a full path to the database file.
 */
void infrared_brute_force_set_db_filename(InfraredBruteForce* brute_force, const char* db_filename);

/**
 * @brief Build a signal dictionary from a previously set database file.
 *
 * This function must be called each time after setting the database via
 * a infrared_brute_force_set_db_filename() call.
 *
 * Every signal in the database is read and validated, but only the ones whose name
 * was registered via infrared_brute_force_add_record() are added to the dictionary.
 * A database name with no matching record is skipped, which is not an error.
 *
 * A signal that sends the same code as one already in its category is left out (NEC and
 * NECext, or a raw capture of a known protocol, count as the code they send). If the database
 * has an order file next to it ("tv.ir" -> "tv.order", made for that exact file), each
 * category is sent in the order it lists; otherwise in file order.
 *
 * @param[in,out] brute_force pointer to the instance to be updated.
 * @returns InfraredErrorCodeNone on success, otherwise error code.
 */
InfraredErrorCode infrared_brute_force_calculate_messages(InfraredBruteForce* brute_force);

/**
 * @brief Start transmitting signals from a category stored in the dictionary.
 *
 * The function locates the category identified by @p index, reports the number of
 * records it contains via @p record_count, and prepares the brute-force instance
 * to transmit those signals. On failure @p record_count is set to zero.
 *
 * @param[in,out] brute_force pointer to the instance to be started.
 * @param[in] index index of the signal category in the dictionary.
 * @param[out] record_count pointer that receives the number of records in the category.
 * @returns true if the category is found and the backing database file is opened, false otherwise.
 */
bool infrared_brute_force_start(
    InfraredBruteForce* brute_force,
    uint32_t index,
    uint32_t* record_count);

/**
 * @brief Determine whether the transmission was started.
 *
 * @param[in] brute_force pointer to the instance to be tested.
 * @returns true if transmission was started, false otherwise.
 */
bool infrared_brute_force_is_started(const InfraredBruteForce* brute_force);

/**
 * @brief Stop transmitting the signals.
 *
 * @param[in] brute_force pointer to the instance to be stopped.
 */
void infrared_brute_force_stop(InfraredBruteForce* brute_force);

/**
 * @brief Send an arbitrary signal from the chosen category.
 * 
 * @param[in] brute_force pointer to the instance
 * @param signal_index the index of the signal within the category, must be
 *                     between 0 and `record_count` as told by
 *                     `infrared_brute_force_start`
 * 
 * @returns true on success, false otherwise
 */
bool infrared_brute_force_send(InfraredBruteForce* brute_force, uint32_t signal_index);

/**
 * @brief Choose how signals are sent by infrared_brute_force_send().
 *
 * Fast mode uses short gaps between codes and trims repeated raw frames
 * (see infrared_signal_transmit_fast()). Normal mode keeps the exact original timings.
 *
 * @param[in] brute_force pointer to the instance
 * @param[in] fast true for fast mode, false for normal mode
 */
void infrared_brute_force_set_fast(InfraredBruteForce* brute_force, bool fast);

/**
 * @brief Choose how signals are sent by infrared_brute_force_send() (see InfraredTxSpeed).
 *
 * @param[in] brute_force pointer to the instance
 * @param[in] speed the speed to send at
 */
void infrared_brute_force_set_speed(InfraredBruteForce* brute_force, InfraredTxSpeed speed);

/**
 * @brief Remember that a signal of the running category worked, so that it is sent first the
 * next time this category runs (kept in /ext/apps_data/infrared/learned.txt).
 *
 * @param[in] brute_force pointer to the instance
 * @param[in] signal_index the signal, as for infrared_brute_force_send()
 * @param[in] saved true when the user saved it (a strong sign), false when they only sent it
 *                  again by hand
 * @returns true if it was remembered
 */
bool infrared_brute_force_learn(InfraredBruteForce* brute_force, uint32_t signal_index, bool saved);

/**
 * @brief Load a signal from the chosen category without transmitting it.
 *
 * @param[in] brute_force pointer to the instance
 * @param[in] signal_index the index of the signal within the category, same range as
 *                         accepted by infrared_brute_force_send()
 * @param[out] signal pointer to the signal to load into. Must be allocated.
 * @returns true on success, false otherwise
 */
bool infrared_brute_force_load_signal(
    InfraredBruteForce* brute_force,
    uint32_t signal_index,
    InfraredSignal* signal);

/**
 * @brief Get the name of the category currently being transmitted.
 *
 * The return value remains valid until the next infrared_brute_force_start() or
 * infrared_brute_force_stop() call.
 *
 * @param[in] brute_force pointer to the instance to be queried.
 * @returns pointer to a zero-terminated string containing the category name.
 */
const char* infrared_brute_force_get_current_record_name(const InfraredBruteForce* brute_force);

/**
 * @brief Add a signal category to an InfraredBruteForce instance's dictionary.
 *
 * @param[in,out] brute_force pointer to the instance to be updated.
 * @param[in] index index of the category to be added.
 * @param[in] name name of the category to be added.
 */
void infrared_brute_force_add_record(
    InfraredBruteForce* brute_force,
    uint32_t index,
    const char* name);

/**
 * @brief Reset an InfraredBruteForce instance.
 *
 * Discards the dictionary and the database file name alike, so the instance must be
 * configured via infrared_brute_force_set_db_filename() again before being reused.
 *
 * @param[in,out] brute_force pointer to the instance to be reset.
 */
void infrared_brute_force_reset(InfraredBruteForce* brute_force);

/*
 * Remote Finder support.
 *
 * When brute-forcing a universal category (e.g. TV Power), the signals of one
 * physical device are stored consecutively in the library and share a common
 * key: for parsed signals the protocol and address, for raw signals simply
 * "raw". infrared_brute_force_collect_device() takes the index of a signal the
 * user confirmed works and gathers that device's whole run of buttons into an
 * opaque, independently-owned collection, so a complete personal remote can be
 * saved from it.
 */

/**
 * @brief Opaque collection of one device's signals gathered by the Remote Finder.
 */
typedef struct InfraredBruteForceDevice InfraredBruteForceDevice;

/**
 * @brief Create an empty device collection.
 *
 * @returns pointer to the created instance.
 */
InfraredBruteForceDevice* infrared_brute_force_device_alloc(void);

/**
 * @brief Delete a device collection and all signals it owns.
 *
 * @param[in,out] device pointer to the instance to be deleted.
 */
void infrared_brute_force_device_free(InfraredBruteForceDevice* device);

/**
 * @brief Drop all signals held by a device collection, keeping the instance.
 *
 * @param[in,out] device pointer to the instance to be reset.
 */
void infrared_brute_force_device_reset(InfraredBruteForceDevice* device);

/**
 * @brief Number of signals held by a device collection.
 *
 * @param[in] device pointer to the instance to be queried.
 * @returns the number of signals.
 */
size_t infrared_brute_force_device_get_count(const InfraredBruteForceDevice* device);

/**
 * @brief Get a signal held by a device collection.
 *
 * @param[in] device pointer to the instance to be queried.
 * @param[in] index index of the signal, 0 to get_count()-1.
 * @returns pointer to the signal, owned by the collection, or NULL if out of range.
 */
const InfraredSignal*
    infrared_brute_force_device_get_signal(const InfraredBruteForceDevice* device, size_t index);

/**
 * @brief Get the button name of a signal held by a device collection.
 *
 * @param[in] device pointer to the instance to be queried.
 * @param[in] index index of the signal, 0 to get_count()-1.
 * @returns pointer to a zero-terminated name, owned by the collection, or NULL if out of range.
 */
const char*
    infrared_brute_force_device_get_name(const InfraredBruteForceDevice* device, size_t index);

/**
 * @brief Get a suggested remote name for a device collection.
 *
 * @param[in] device pointer to the instance to be queried.
 * @returns pointer to a zero-terminated name, owned by the collection (may be empty).
 */
const char* infrared_brute_force_device_get_suggested_name(const InfraredBruteForceDevice* device);

/**
 * @brief Gather the device that owns a given signal into a collection.
 *
 * Opens its own view of the database (the transmitting one is left untouched), so it
 * must be called while the instance is started. Grows a run of consecutive signals
 * around @p signal_index while they share the same key (parsed protocol+address, or
 * both raw) and no button name repeats, capped at @p max_signals. The collected
 * signals are deep-copied and remain valid after the instance is stopped or reset.
 *
 * @param[in,out] brute_force pointer to a started instance.
 * @param[in] signal_index index of the confirmed signal within the current category.
 * @param[in] max_signals maximum number of signals to collect.
 * @param[out] device collection to fill; its previous contents are discarded.
 * @returns the number of signals collected (at least 1 on success), or 0 on failure.
 */
size_t infrared_brute_force_collect_device(
    InfraredBruteForce* brute_force,
    uint32_t signal_index,
    size_t max_signals,
    InfraredBruteForceDevice* device);

#ifdef __cplusplus
}
#endif
