/**
 * @file boot_guard.h
 * @brief Put the previous firmware back when a new one keeps failing to start.
 *
 * The state (boot_guard_state.h) lives in FuriHalRtcRegisterBootGuard. The rollback package is a
 * slim copy of the last package that started steadily (update_rollback.h).
 */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** First thing in a normal start, before any service: may restart into the rollback */
void boot_guard_on_start(void);
/** The firmware has run steadily since it started */
void boot_guard_on_stable(void);
/** Whether the boot stage should install the rollback package */
bool boot_guard_rollback_requested(void);
/** Whether a newly installed firmware is being watched */
bool boot_guard_armed(void);
/** An update finished installing */
void boot_guard_update_done(bool rollback_available, bool was_rollback);
/** The rollback package is in place or not */
void boot_guard_set_rollback_available(bool available);

#ifdef __cplusplus
}
#endif
