/**
 * @file updater_descent.h
 * @brief The update screen: a bathyscaphe sinking towards the bottom of the Challenger Deep as the
 * update goes on, with a depth gauge, marine snow rising past it and the seabed coming up at the
 * end. Drawing only, no state: safe to call with any values.
 */
#pragma once

#include <gui/canvas.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** The deepest point: 100% */
#define UPDATER_DESCENT_DEPTH_M (10935U)

/**
 * @param progress 0..100 (more is drawn as 100)
 * @param status what the updater is doing, or NULL
 * @param frame counts up while the screen is shown, for the snow, the propeller and the light
 */
void updater_descent_draw(Canvas* canvas, uint8_t progress, const char* status, uint32_t frame);

#ifdef __cplusplus
}
#endif
