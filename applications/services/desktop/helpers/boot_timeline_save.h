#pragma once

#include <storage/storage.h>

#define BOOT_TIMELINE_DIR  EXT_PATH("apps_data/boot")
#define BOOT_TIMELINE_PATH BOOT_TIMELINE_DIR "/timeline.txt"

/**
 * @brief Write this start's timeline (toolbox/boot_profile.h) to the SD card, replacing the last
 * one: each step, when it ended and how long it took.
 * @return true if written
 */
bool boot_timeline_save(Storage* storage);
