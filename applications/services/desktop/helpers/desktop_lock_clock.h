/**
 * @file desktop_lock_clock.h
 * @brief The time and date on the lock screen: a small panel in the bottom left corner, over the
 * animation. Drawing only.
 */
#pragma once

#include <gui/canvas.h>
#include <datetime/datetime.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void desktop_lock_clock_draw(Canvas* canvas, const DateTime* datetime, bool hour_12);

#ifdef __cplusplus
}
#endif
