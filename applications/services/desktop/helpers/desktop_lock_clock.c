#include "desktop_lock_clock.h"

#include <stdio.h>

static const char* const weekdays[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
static const char* const months[] =
    {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

void desktop_clock_format(char* out, size_t size, uint8_t hour, uint8_t minute, bool hour_12) {
    if(hour_12) {
        hour %= 12;
        snprintf(out, size, "%u:%02u", hour ? hour : 12u, minute % 60u);
    } else {
        snprintf(out, size, "%02u:%02u", hour % 24u, minute % 60u);
    }
}

void desktop_lock_clock_draw(Canvas* canvas, const DateTime* datetime, bool hour_12) {
    char time[8];
    char date[20];
    const char* half = NULL;
    if(hour_12) half = datetime->hour < 12 ? "AM" : "PM";
    desktop_clock_format(time, sizeof(time), datetime->hour, datetime->minute, hour_12);
    snprintf(
        date,
        sizeof(date),
        "%s %u %s",
        weekdays[(datetime->weekday + 6) % 7],
        datetime->day % 100,
        months[(datetime->month + 11) % 12]);

    canvas_set_font(canvas, FontBigNumbers);
    const int32_t time_width = canvas_string_width(canvas, time);
    canvas_set_font(canvas, FontSecondary);
    const int32_t half_width = half ? canvas_string_width(canvas, half) + 2 : 0;
    const int32_t date_width = canvas_string_width(canvas, date);
    int32_t width = time_width + half_width;
    if(date_width > width) width = date_width;
    width += 8;

    // a pale panel with a dark border, so it reads over any animation: two clear columns and a
    // row from the screen's edges, and inside it the digits three rows under the top and the
    // date's descenders a row above the bottom
    const int32_t x = 2, y = 31, height = 32;
    canvas_set_color(canvas, ColorWhite);
    canvas_draw_rbox(canvas, x, y, width, height, 3);
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_rframe(canvas, x, y, width, height, 3);

    canvas_set_font(canvas, FontBigNumbers);
    canvas_draw_str(canvas, x + 4, y + 18, time);
    if(half) {
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, x + 6 + time_width, y + 18, half);
    }
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, x + 4, y + 28, date);
}
