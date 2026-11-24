/**
 * @file elements.h
 * GUI: Elements API
 * 
 * Canvas helpers and UI building blocks.
 * 
 */

#pragma once

#include <stdint.h>
#include <furi.h>
#include "canvas.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ELEMENTS_MAX_LINES_NUM  (7)
#define ELEMENTS_BOLD_MARKER    '#'
#define ELEMENTS_MONO_MARKER    '*'
#define ELEMENTS_INVERSE_MARKER '!'

/** Draw progress bar.
 *
 * @param   canvas      Canvas instance
 * @param   x           progress bar position on X axis
 * @param   y           progress bar position on Y axis
 * @param   width       progress bar width
 * @param   progress    progress (0.0 - 1.0)
 */
void elements_progress_bar(Canvas* canvas, int32_t x, int32_t y, size_t width, float progress);

/** Draw progress bar with text.
 *
 * @param   canvas      Canvas instance
 * @param   x           progress bar position on X axis
 * @param   y           progress bar position on Y axis
 * @param   width       progress bar width
 * @param   progress    progress (0.0 - 1.0)
 * @param   text        text to draw
 */
void elements_progress_bar_with_text(
    Canvas* canvas,
    int32_t x,
    int32_t y,
    size_t width,
    float progress,
    const char* text);

/** Draw scrollbar on canvas at specific position.
 *
 * @param   canvas  Canvas instance
 * @param   x       scrollbar position on X axis
 * @param   y       scrollbar position on Y axis
 * @param   height  scrollbar height
 * @param   pos     current element
 * @param   total   total elements
 */
void elements_scrollbar_pos(
    Canvas* canvas,
    int32_t x,
    int32_t y,
    size_t height,
    size_t pos,
    size_t total);

/** Draw scrollbar on canvas.
 * @note    width 3px, height equal to canvas height
 *
 * @param   canvas  Canvas instance
 * @param   pos     current element of total elements
 * @param   total   total elements
 */
void elements_scrollbar(Canvas* canvas, size_t pos, size_t total);

/** Draw rounded frame
 *
 * @param   canvas          Canvas instance
 * @param   x, y            top left corner coordinates
 * @param   width, height   frame width and height
 */
void elements_frame(Canvas* canvas, int32_t x, int32_t y, size_t width, size_t height);

/** Draw button in left corner
 *
 * @param   canvas  Canvas instance
 * @param   str     button text
 */
void elements_button_left(Canvas* canvas, const char* str);

/** Draw button in right corner
 *
 * @param   canvas  Canvas instance
 * @param   str     button text
 */
void elements_button_right(Canvas* canvas, const char* str);

/**
 * @brief This function draws a button in the top left corner of the canvas with icon and string.
 *
 * The design and layout of the button is defined within this function.
 *
 * @param[in] canvas This is a pointer to the @c Canvas structure where the button will be drawn.
 * @param[in] str This is a pointer to the character string that will be drawn within the button.
 *
 */
void elements_button_up(Canvas* canvas, const char* str);

/**
 * @brief This function draws a button in the top right corner of the canvas with icon and string.
 *
 * The design and layout of the button is defined within this function.
 *
 * @param[in] canvas This is a pointer to the @c Canvas structure where the button will be drawn.
 * @param[in] str This is a pointer to the character string that will be drawn within the button.
 *
 */
void elements_button_down(Canvas* canvas, const char* str);

/** Draw button in center
 *
 * @param   canvas  Canvas instance
 * @param   str     button text
 */
void elements_button_center(Canvas* canvas, const char* str);

/** Draw aligned multiline text
 *
 * @param   canvas                  Canvas instance
 * @param   x, y                    coordinates based on align param
 * @param   horizontal, vertical    alignment of multiline text
 * @param   text                    string (possible multiline)
 */
void elements_multiline_text_aligned(
    Canvas* canvas,
    int32_t x,
    int32_t y,
    Align horizontal,
    Align vertical,
    const char* text);

/** Draw multiline text
 *
 * @param   canvas  Canvas instance
 * @param   x       top left corner coordinates
 * @param   y       top left corner coordinates
 * @param   text    string (possible multiline)
 */
void elements_multiline_text(Canvas* canvas, int32_t x, int32_t y, const char* text);

/** Draw framed multiline text
 *
 * @param   canvas  Canvas instance
 * @param   x       top left corner coordinates
 * @param   y       top left corner coordinates
 * @param   text    string (possible multiline)
 */
void elements_multiline_text_framed(Canvas* canvas, int32_t x, int32_t y, const char* text);

/** Draw slightly rounded frame
 *
 * @param   canvas          Canvas instance
 * @param   x               top left corner coordinates
 * @param   y               top left corner coordinates
 * @param   width           width of frame
 * @param   height          height of frame
 */
void elements_slightly_rounded_frame(
    Canvas* canvas,
    int32_t x,
    int32_t y,
    size_t width,
    size_t height);

/** Draw slightly rounded box
 *
 * @param   canvas          Canvas instance
 * @param   x               top left corner coordinates
 * @param   y               top left corner coordinates
 * @param   width           height of box
 * @param   height          height of box
 */
void elements_slightly_rounded_box(
    Canvas* canvas,
    int32_t x,
    int32_t y,
    size_t width,
    size_t height);

/** Draw bold rounded frame
 *
 * @param   canvas          Canvas instance
 * @param   x               top left corner coordinates
 * @param   y               top left corner coordinates
 * @param   width           width of frame
 * @param   height          height of frame
 */
void elements_bold_rounded_frame(Canvas* canvas, int32_t x, int32_t y, size_t width, size_t height);

/** Draw bubble frame for text
 *
 * @param   canvas  Canvas instance
 * @param   x       left x coordinates
 * @param   y       top y coordinate
 * @param   width   bubble width
 * @param   height  bubble height
 */
void elements_bubble(Canvas* canvas, int32_t x, int32_t y, size_t width, size_t height);

/** Draw bubble frame for text with corner
 *
 * @param   canvas      Canvas instance
 * @param   x           left x coordinates
 * @param   y           top y coordinate
 * @param   text        text to display
 * @param   horizontal  horizontal aligning
 * @param   vertical    aligning
 */
void elements_bubble_str(
    Canvas* canvas,
    int32_t x,
    int32_t y,
    const char* text,
    Align horizontal,
    Align vertical);

/** Trim string buffer to fit width in pixels
 *
 * @param   canvas  Canvas instance
 * @param   string  string to trim
 * @param   width   max width
 */
void elements_string_fit_width(Canvas* canvas, FuriString* string, size_t width);

/** Draw scrollable text line
 *
 * @param      canvas    The canvas
 * @param[in]  x         X coordinate
 * @param[in]  y         Y coordinate
 * @param[in]  width     The width
 * @param      string    The string
 * @param[in]  scroll    The scroll counter: 0 - no scroll, any other number - scroll. Just count up, everything else will be calculated on the inside.
 * @param[in]  ellipsis  The ellipsis flag: true to add ellipse
 */
void elements_scrollable_text_line(
    Canvas* canvas,
    int32_t x,
    int32_t y,
    size_t width,
    FuriString* string,
    size_t scroll,
    bool ellipsis);

void elements_scrollable_text_line_str(
    Canvas* canvas,
    uint8_t x,
    uint8_t y,
    uint8_t width,
    const char* string,
    size_t scroll,
    bool ellipsis,
    bool centered);

/** Steps a list's long selected name scrolls for after the last key (about 20 s at the lists'
 * three a second), before it comes to rest at its start: a list left open drew three times a
 * second for as long as it stayed open, the screen dark or not */
#define ELEMENTS_SCROLL_IDLE_STEPS (60U)

/** The scroll counter to draw a list's selected name with: at rest, its start */
static inline size_t elements_scroll_position(size_t counter) {
    return counter >= ELEMENTS_SCROLL_IDLE_STEPS ? 0 : counter;
}

/** A list's scroll step, from its timer: false once at rest, when nothing needs drawing again
 * (a key sets the counter back to 0, and it scrolls again) */
static inline bool elements_scroll_step(size_t* counter) {
    if(*counter >= ELEMENTS_SCROLL_IDLE_STEPS) return false;
    (*counter)++;
    return true;
}

/** Draw text box element
 *
 * @param       canvas          Canvas instance
 * @param       x               x coordinate
 * @param       y               y coordinate
 * @param       width           width to fit text
 * @param       height          height to fit text
 * @param       horizontal      Align instance
 * @param       vertical        Align instance
 * @param[in]   text            Formatted text. The following formats are available:
 *                              "\e#Bold text\e#" - bold font is used
 *                              "\e*Monospaced text\e*" - monospaced font is used
 *                              "\e!Inverted text\e!" - white text on black background
 * @param      strip_to_dots    Strip text to ... if does not fit to width
 */
void elements_text_box(
    Canvas* canvas,
    int32_t x,
    int32_t y,
    size_t width,
    size_t height,
    Align horizontal,
    Align vertical,
    const char* text,
    bool strip_to_dots);

/** Height of the hint bar along the bottom of the screen, its dotted rule included: the rule,
 * a clear row, the hints, a clear row */
#define ELEMENTS_HINT_BAR_HEIGHT 12

/** Draw a line of button hints: each hint is a button's glyph and what that button does.
 *
 * The hints are written "button=what it does", one after another with ';' between them, e.g.
 * "lr=Browse;ok=Choose;back=Cancel". The buttons are ok, back, lr (left and right), ud (up and
 * down), left, right, up and down; a hint with nothing before '=' is its text alone.
 *
 * @param      canvas      Canvas instance
 * @param[in]  x           the line's left end, centre or right end, as horizontal says
 * @param[in]  y           the text's baseline
 * @param[in]  horizontal  where x is on the line
 * @param[in]  hints       the hints, as above
 *
 * @return     the line's width in pixels
 */
uint16_t elements_hints(Canvas* canvas, int32_t x, int32_t y, Align horizontal, const char* hints);

/** Draw the hint bar: a dotted rule and a line of hints, centred, along the screen's bottom
 * ELEMENTS_HINT_BAR_HEIGHT pixels, which it clears first. Hints are written as for
 * elements_hints(). This is where a screen says which buttons do what, so that its title and
 * its items do not have to.
 *
 * @param      canvas  Canvas instance
 * @param[in]  hints   the hints
 */
void elements_hint_bar(Canvas* canvas, const char* hints);

/** How every page's header is drawn, as Settings > Hadal > Interface > Header Style sets it */
typedef enum {
    ElementsHeaderStyleFade, /**< a solid line after the title, fading out to the right */
    ElementsHeaderStyleRule, /**< a fine rule under the title, the width of the page */
    ElementsHeaderStyleDots, /**< a dotted line after the title, its gaps widening */
    ElementsHeaderStylePlain, /**< the title alone */
    ElementsHeaderStyleLine, /**< a straight solid line after the title, to the right edge */
    ElementsHeaderStyleCount,
} ElementsHeaderStyle;

/** Set how elements_header() draws (the desktop applies the setting; a page may preview one)
 *
 * @param      style  the style; one out of range is the fading line
 */
void elements_set_header_style(ElementsHeaderStyle style);

/** Height of the header elements_header() draws: the rows of a page start under it */
#define ELEMENTS_HEADER_HEIGHT 16

/** Draw Hadal's header across the top ELEMENTS_HEADER_HEIGHT pixels: the page's title in the
 * bold font, then a fine line out to Abyss's light, short of the scrollbar's column. Every page
 * that has a title draws it so (submenus and settings lists with one, and screens of their own).
 *
 * @param      canvas  Canvas instance
 * @param[in]  title   the page's title, in the case it is written (it is shortened to fit)
 */
void elements_header(Canvas* canvas, const char* title);

#ifdef __cplusplus
}
#endif
