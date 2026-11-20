#include "boot_guard_state.h"

#define GUARD_MAGIC       (0xB6U)
#define GUARD_MAGIC_SHIFT (24U)
#define GUARD_ARMED       (1U << 0)
#define GUARD_AVAILABLE   (1U << 1)
#define GUARD_REQUESTED   (1U << 2)
#define GUARD_FLAGS_SHIFT (8U)

uint32_t boot_guard_state_pack(const BootGuardState* state) {
    const uint32_t flags = (state->armed ? GUARD_ARMED : 0) |
                           (state->rollback_available ? GUARD_AVAILABLE : 0) |
                           (state->rollback_requested ? GUARD_REQUESTED : 0);
    return (GUARD_MAGIC << GUARD_MAGIC_SHIFT) | (flags << GUARD_FLAGS_SHIFT) |
           state->unstable_starts;
}

void boot_guard_state_unpack(uint32_t raw, BootGuardState* state) {
    const bool ours = (raw >> GUARD_MAGIC_SHIFT) == GUARD_MAGIC;
    const uint32_t flags = ours ? (raw >> GUARD_FLAGS_SHIFT) & 0xFF : 0;
    state->armed = flags & GUARD_ARMED;
    state->rollback_available = flags & GUARD_AVAILABLE;
    state->rollback_requested = flags & GUARD_REQUESTED;
    state->unstable_starts = ours ? raw & 0xFF : 0;
}

BootGuardAction boot_guard_state_on_start(BootGuardState* state) {
    if(!state->armed) return BootGuardActionNone;
    if(state->unstable_starts < 0xFF) state->unstable_starts++;
    if(state->unstable_starts >= BOOT_GUARD_UNSTABLE_LIMIT && state->rollback_available &&
       !state->rollback_requested) {
        // once: if the rollback cannot start either, the guard stays out of the way
        state->armed = false;
        state->rollback_requested = true;
        return BootGuardActionRollback;
    }
    return BootGuardActionNone;
}

void boot_guard_state_on_stable(BootGuardState* state) {
    state->armed = false;
    state->unstable_starts = 0;
}

void boot_guard_state_on_update_done(
    BootGuardState* state,
    bool rollback_available,
    bool was_rollback) {
    state->rollback_requested = false;
    state->unstable_starts = 0;
    state->rollback_available = rollback_available;
    // the firmware put back is the one that worked: nothing to watch
    state->armed = !was_rollback && rollback_available;
}
