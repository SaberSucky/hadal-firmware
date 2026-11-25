/**
 * @file ra_atlas_scan.h
 * @brief Which band the atlas listens to, and what each band has been doing.
 *
 * One radio, many bands: the receiver parks on one band at a time and moves on, so every band
 * gets a reading once a round rather than a continuous one. A whole sweep of the sub-GHz range
 * every tick is not on: each retune costs the CC1101 a calibration, which is why the Frequency
 * Analyzer only ever hunts for one frequency.
 *
 * What counts as a signal is the Read screen's (ra_sweep.c): RSSI at or above the trigger,
 * held over the gaps a remote leaves between frames. A band that has one keeps the receiver
 * - so the decoders hear the whole press - up to a limit, so a band whose noise sits above the
 * trigger cannot starve the others. Held on one band (ra_atlas_scan_hold()), it never moves.
 *
 * Pure state machine like ra_sweep: it is told the time and the RSSI and says when to retune.
 * It touches no radio and no GUI, and the host tests build it as it is.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RA_ATLAS_SCAN_BANDS_MAX 32
#define RA_ATLAS_PROTOCOL_SIZE  24

/** How long a band is listened to when there is nothing on it */
#define RA_ATLAS_DWELL_MS       200
/** A remote keys the carrier off between frames, so the signal is held over the gaps */
#define RA_ATLAS_SIGNAL_HOLD_MS 500
/** The longest a busy band keeps the receiver while scanning */
#define RA_ATLAS_PARK_MAX_MS    3000
/** A band reads as busy this long after its last carrier: about one round of a long plan */
#define RA_ATLAS_ACTIVE_MS      4000

typedef enum {
    RaAtlasStatusOutOfRange, /**< the radio cannot tune it */
    RaAtlasStatusQuiet, /**< listened to, nothing heard yet */
    RaAtlasStatusActive, /**< a carrier within RA_ATLAS_ACTIVE_MS */
    RaAtlasStatusSeen, /**< heard before, quiet now */
} RaAtlasStatus;

typedef enum {
    RaAtlasActionNone,
    /** Tune to the band ra_atlas_scan_current() names before the next tick. */
    RaAtlasActionRetune,
} RaAtlasAction;

typedef struct {
    bool tunable;
    bool seen;
    bool decoded;
    uint32_t t_seen; /**< last carrier, kernel ticks */
    uint32_t t_decoded;
    float rssi_last; /**< the last reading while listening to it */
    float rssi_peak;
    char protocol[RA_ATLAS_PROTOCOL_SIZE]; /**< the last decode heard on it */
    const char* note; /**< where SubGHz stands on that protocol; static text */
} RaAtlasLive;

typedef struct {
    RaAtlasLive live[RA_ATLAS_SCAN_BANDS_MAX];
    size_t count;
    size_t current; /**< the band being listened to */
    bool hold;
    float trigger;

    uint32_t t_park; /**< when the receiver came to the current band */
    uint32_t t_carrier; /**< last carrier on the current band */
    bool have_carrier;

    uint32_t dwell;
    uint32_t signal_hold;
    uint32_t park_max;
    uint32_t active;
    uint32_t ticks_per_second;
} RaAtlasScan;

/**
 * @param tunable one per band: whether the radio can tune its listen frequency
 * @param ticks_per_second the kernel tick rate, used to scale the periods once
 * @return true when there is a tunable band to start on (ra_atlas_scan_current())
 */
bool ra_atlas_scan_init(
    RaAtlasScan* scan,
    const bool* tunable,
    size_t count,
    float trigger,
    uint32_t ticks_per_second,
    uint32_t now);

/** Call every tick with the RSSI on the band being listened to. */
RaAtlasAction ra_atlas_scan_tick(RaAtlasScan* scan, float rssi, uint32_t now);

/** A frame decoded on the band being listened to. @p note must be static text. */
void ra_atlas_scan_decoded(RaAtlasScan* scan, const char* protocol, const char* note, uint32_t now);

/**
 * Listen to band @p index alone. Refused, returning false, for a band out of range.
 * @param[out] retune set when the receiver has to move to it
 */
bool ra_atlas_scan_hold(RaAtlasScan* scan, size_t index, uint32_t now, bool* retune);

/** Back to going round the bands, from the one held. */
void ra_atlas_scan_release(RaAtlasScan* scan, uint32_t now);

size_t ra_atlas_scan_current(const RaAtlasScan* scan);

bool ra_atlas_scan_is_held(const RaAtlasScan* scan);

/** True while the band being listened to has a carrier, by the Read screen's measure. */
bool ra_atlas_scan_is_signal(const RaAtlasScan* scan, uint32_t now);

RaAtlasStatus ra_atlas_scan_status(const RaAtlasScan* scan, size_t index, uint32_t now);

/** Seconds since band @p index last had a carrier; 0 when it never had one. */
uint32_t ra_atlas_scan_seen_ago(const RaAtlasScan* scan, size_t index, uint32_t now);

const RaAtlasLive* ra_atlas_scan_live(const RaAtlasScan* scan, size_t index);
