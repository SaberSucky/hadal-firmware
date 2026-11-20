/**
 * @file boot_guard_state.h
 * @brief Whether a new firmware starts, and when to put the previous one back.
 *
 * After an update the guard is armed. Every start counts until the firmware has run steadily
 * for a while (boot_guard_state_on_stable); the BOOT_GUARD_UNSTABLE_LIMIT-th start in a row that
 * never got there asks for the rollback package, if there is one. Kept in one RTC backup
 * register, which survives restarts and repairs. No dependencies, for the host tests.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BOOT_GUARD_UNSTABLE_LIMIT (4U)

typedef struct {
    bool armed; /**< watching the starts of a newly installed firmware */
    bool rollback_available; /**< a rollback package is in place */
    bool rollback_requested; /**< the boot stage should install it */
    uint8_t unstable_starts;
} BootGuardState;

typedef enum {
    BootGuardActionNone,
    BootGuardActionRollback, /**< restart into the boot stage to install the rollback package */
} BootGuardAction;

uint32_t boot_guard_state_pack(const BootGuardState* state);
/** A register that is not the guard's (another firmware, cleared) reads as all off */
void boot_guard_state_unpack(uint32_t raw, BootGuardState* state);

/** A normal start of the firmware */
BootGuardAction boot_guard_state_on_start(BootGuardState* state);
/** The firmware has run steadily: nothing to fear from it any more */
void boot_guard_state_on_stable(BootGuardState* state);
/** An update finished; was_rollback when it was the rollback being installed */
void boot_guard_state_on_update_done(
    BootGuardState* state,
    bool rollback_available,
    bool was_rollback);

#ifdef __cplusplus
}
#endif
