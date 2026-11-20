#pragma once

#include "update_task.h"

/**
 * @brief Check, before anything is changed, that the package cannot leave the Flipper unable
 * to start.
 *
 * Runs in the updater that runs from RAM, so it protects every install of a Hadal package,
 * whatever firmware the Flipper had. Checks the battery, both images' CRCs, that the firmware
 * fits (below the new radio stack, or in the flash that is writable now when the radio stays),
 * and that the option bytes cannot lock the chip. On a problem it stops the update with a short
 * reason on the screen; the Flipper keeps the firmware it had.
 *
 * @return true when the update may go ahead
 */
bool update_task_preflight(UpdateTask* update_task);
