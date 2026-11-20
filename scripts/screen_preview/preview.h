/* Screen previews: an app's own drawing code, run on a PC, saved as images. */
#pragma once

#include <furi.h>
#include <gui/canvas.h>
#include <gui/view_port.h>

extern uint32_t preview_tick;

/** Where preview_save() writes */
void preview_set_dir(const char* dir);
/** A cleared canvas with the defaults a view port gets */
Canvas* preview_canvas(void);
/** The canvas as <dir>/<name>.pbm */
void preview_save(Canvas* canvas, const char* name);
/** Draw with a view port's draw callback and save it */
void preview_draw(const char* name, ViewPortDrawCallback draw, void* context);
