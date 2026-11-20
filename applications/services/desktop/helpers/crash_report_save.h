#pragma once

#include <storage/storage.h>

#define CRASH_REPORT_DIR EXT_PATH("crash")

/**
 * @brief Write the report of the last crash (furi/core/crash_report.h) to the SD card.
 * @return true when there is nothing left to do: written, or no report; false while the card
 *         is not ready, so the caller tries again later
 */
bool crash_report_save(Storage* storage);
