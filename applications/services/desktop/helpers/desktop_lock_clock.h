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

/** The time as every desktop clock writes it (the status bar, the control centre, the lock
 * screen): "07:05" in 24 hours, "7:05" in 12 (AM/PM is the caller's to add, where there is room)
 *
 * @param      out      the text, 6 characters and the end
 * @param      size     out's size
 * @param      hour     0-23
 * @param      minute   0-59
 * @param      hour_12  the 12-hour format
 */
void desktop_clock_format(char* out, size_t size, uint8_t hour, uint8_t minute, bool hour_12);

#ifdef __cplusplus
}
#endif
