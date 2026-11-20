/**
 * @file boot_profile.h
 * A timeline of start-up, for finding what takes the time.
 *
 * Services call boot_profile_mark() at the points that matter ("SD card mounted", "desktop ready"),
 * and the `boot_profile` CLI command prints them with the time since the previous one. Marks only
 * store a tick count and a pointer, so they cost next to nothing. Not exported to apps.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BOOT_PROFILE_MAX_MARKS 40u

/** Record that `name` happened now. `name` must stay valid for ever (use a string literal). */
void boot_profile_mark(const char* name);

/** Number of marks recorded so far. */
size_t boot_profile_count(void);

/** Mark `index` (0 is the first): its time in ms since the kernel started, and its name. */
bool boot_profile_get(size_t index, uint32_t* tick_ms, const char** name);

#ifdef __cplusplus
}
#endif
