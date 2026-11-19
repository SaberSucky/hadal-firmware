#pragma once

/**
 * @file update_log.h
 * A trail of what the updater did, kept on the SD card in update/hadal_update.log.
 *
 * The updater restarts the Flipper several times, and when something goes wrong in the radio steps
 * the screen can stay dark with nothing to say why. Every line written here is closed again
 * straight away, so the last line in the file is the last thing the updater got to, however it
 * stopped. Read it on a computer afterwards. Writing it is best effort: if the card cannot be
 * written the update carries on exactly as it would without a log.
 */

#ifdef __cplusplus
extern "C" {
#endif

#define UPDATE_LOG_PATH      "/ext/update/hadal_update.log"
/** A file this big is started again, so a card is never filled by a loop of failed attempts. */
#define UPDATE_LOG_MAX_BYTES (64 * 1024)

void update_log(const char* format, ...) __attribute__((format(printf, 1, 2)));

/* The usual log, and the same line in the file on the card */
#define UPDATE_LOG_W(tag, ...)    \
    do {                          \
        FURI_LOG_W(tag, __VA_ARGS__); \
        update_log(__VA_ARGS__);  \
    } while(0)
#define UPDATE_LOG_E(tag, ...)    \
    do {                          \
        FURI_LOG_E(tag, __VA_ARGS__); \
        update_log(__VA_ARGS__);  \
    } while(0)

#ifdef __cplusplus
}
#endif
