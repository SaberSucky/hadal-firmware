/**
 * @file subghz_ignore.h
 * @brief Signals the user asked never to show again, and autosaving what Read receives.
 *
 * The ignore list is /ext/subghz/assets/ignored.txt: a line a signal, its hash
 * (helpers/subghz_signal_hash.h) in hex and then, for the reader, what it was.
 */
#pragma once

#include <furi.h>
#include <storage/storage.h>

#define SUBGHZ_IGNORE_PATH     EXT_PATH("subghz/assets/ignored.txt")
#define SUBGHZ_IGNORE_MAX      (64U)
#define SUBGHZ_AUTOSAVE_FOLDER EXT_PATH("subghz/autosave")

/** The ignored signals' hashes, at most max; the number read */
size_t subghz_ignore_load(Storage* storage, uint32_t* hashes, size_t max);

/** Add a signal to the list, with a label for whoever reads the file */
bool subghz_ignore_add(Storage* storage, uint32_t hash, const char* label);

/** Forget every ignored signal */
bool subghz_ignore_clear(Storage* storage);
