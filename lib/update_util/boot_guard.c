#include "boot_guard.h"
#include "boot_guard_state.h"

#include <furi_hal_rtc.h>
#include <furi_hal_power.h>

static BootGuardState boot_guard_read(void) {
    BootGuardState state;
    boot_guard_state_unpack(furi_hal_rtc_get_register(FuriHalRtcRegisterBootGuard), &state);
    return state;
}

static void boot_guard_write(const BootGuardState* state) {
    furi_hal_rtc_set_register(FuriHalRtcRegisterBootGuard, boot_guard_state_pack(state));
}

void boot_guard_on_start(void) {
    BootGuardState state = boot_guard_read();
    if(!state.armed) return;
    const BootGuardAction action = boot_guard_state_on_start(&state);
    boot_guard_write(&state);
    if(action == BootGuardActionRollback) {
        // the boot stage installs the rollback package (targets/f7/src/update.c)
        furi_hal_rtc_set_boot_mode(FuriHalRtcBootModeUpdate);
        furi_hal_power_reset();
    }
}

void boot_guard_on_stable(void) {
    BootGuardState state = boot_guard_read();
    boot_guard_state_on_stable(&state);
    boot_guard_write(&state);
}

bool boot_guard_rollback_requested(void) {
    return boot_guard_read().rollback_requested;
}

bool boot_guard_armed(void) {
    return boot_guard_read().armed;
}

void boot_guard_update_done(bool rollback_available, bool was_rollback) {
    BootGuardState state = boot_guard_read();
    boot_guard_state_on_update_done(&state, rollback_available, was_rollback);
    boot_guard_write(&state);
}

void boot_guard_set_rollback_available(bool available) {
    BootGuardState state = boot_guard_read();
    state.rollback_available = available;
    boot_guard_write(&state);
}
