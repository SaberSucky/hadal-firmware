/* Host driver for lib/update_util/boot_guard_state.c: reads events, one per line, and prints the
 * state after each: armed available requested starts action
 *   U <available> <was_rollback>   an update finished
 *   B                              a normal start
 *   S                              the firmware ran steadily
 *   R <raw hex>                    replace the register (another firmware, cleared)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "boot_guard_state.h"

int main(void) {
    uint32_t reg = 0;
    char line[128];
    while(fgets(line, sizeof(line), stdin)) {
        BootGuardState state;
        boot_guard_state_unpack(reg, &state);
        const char* action = "-";
        int a = 0, b = 0;
        unsigned raw = 0;
        if(line[0] == 'U' && sscanf(line + 1, "%d %d", &a, &b) == 2) {
            boot_guard_state_on_update_done(&state, a, b);
        } else if(line[0] == 'B') {
            action = boot_guard_state_on_start(&state) == BootGuardActionRollback ? "ROLLBACK" : "-";
        } else if(line[0] == 'S') {
            boot_guard_state_on_stable(&state);
        } else if(line[0] == 'R' && sscanf(line + 1, "%x", &raw) == 1) {
            reg = raw;
            boot_guard_state_unpack(reg, &state);
        } else {
            continue;
        }
        reg = boot_guard_state_pack(&state);
        BootGuardState check;
        boot_guard_state_unpack(reg, &check);
        if(memcmp(&check, &state, sizeof(state)) != 0) abort(); /* round trip */
        printf("%d %d %d %u %s\n", state.armed, state.rollback_available, state.rollback_requested, state.unstable_starts, action);
    }
    return 0;
}
