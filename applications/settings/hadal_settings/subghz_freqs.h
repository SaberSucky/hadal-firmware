/**
 * @file subghz_freqs.h
 * @brief The user's Sub-GHz frequencies, in /ext/subghz/assets/setting_user: listing, adding and
 * removing "Frequency:" and "Hopper_frequency:" lines, and the "Add_standard_frequencies" switch,
 * leaving every other line (comments, custom presets) as it was. No dependencies, for the host
 * tests.
 *
 * Sub-GHz keeps the built-in lists unless the file leaves them out, then adds the file's. With
 * them left out and no hopper frequency, it ignores the whole file and uses its own lists.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SUBGHZ_FREQS_PATH "/ext/subghz/assets/setting_user"

/** The frequencies listed (Hz), in file order, at most max; the number found */
size_t subghz_freqs_list(const char* text, uint32_t* out, size_t max);

/** Whether the firmware's own frequencies come too (true when the file does not say) */
bool subghz_freqs_standard(const char* text);

/** The hopper frequencies listed (Hz), in file order, at most max; the number found */
size_t subghz_freqs_list_hopper(const char* text, uint32_t* out, size_t max);

/** New texts (free() them), NULL when out of memory */
char* subghz_freqs_with(const char* text, uint32_t hz);
char* subghz_freqs_without(const char* text, uint32_t hz);
char* subghz_freqs_with_hopper(const char* text, uint32_t hz);
char* subghz_freqs_without_hopper(const char* text, uint32_t hz);
char* subghz_freqs_set_standard(const char* text, bool on);

#ifdef __cplusplus
}
#endif
