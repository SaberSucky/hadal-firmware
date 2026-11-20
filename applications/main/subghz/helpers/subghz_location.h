/**
 * @file subghz_location.h
 * @brief Where a signal was received, from the phone: the Flipper app on a paired phone shares its
 * location (the firmware's GPS service), and Read writes it into each new signal while Location
 * is on (Read > Config). No GPS module needed.
 */
#pragma once

#include <furi.h>

typedef struct SubGhzLocation SubGhzLocation;

SubGhzLocation* subghz_location_alloc(void);
void subghz_location_free(SubGhzLocation* location);

/** Ask the phone for its location, about once a second, until stopped */
void subghz_location_start(SubGhzLocation* location);
/** Called now and then: asks again when the phone was not there or went quiet */
void subghz_location_poll(SubGhzLocation* location);
void subghz_location_stop(SubGhzLocation* location);

/** The last location if it is recent, degrees x 1e7 */
bool subghz_location_get(SubGhzLocation* location, int32_t* latitude, int32_t* longitude);
