/*
 * The rest of the host runtime for screen previews: no-op furi objects, empty records, a scanner
 * that never starts, and saving the canvas as a PBM image.
 */
#include "preview.h"

#include <gui/canvas_i.h>
#include <gui/gui.h>
#include <gui/view_port.h>
#include <furi_hal_bt.h>
#include <notification/notification.h>

uint32_t preview_tick = 0;

struct FuriMutex {
    int unused;
};
FuriMutex* furi_mutex_alloc(FuriMutexType type) {
    UNUSED(type);
    return calloc(1, sizeof(FuriMutex));
}
void furi_mutex_free(FuriMutex* mutex) {
    free(mutex);
}
FuriStatus furi_mutex_acquire(FuriMutex* mutex, uint32_t timeout) {
    UNUSED(mutex);
    UNUSED(timeout);
    return FuriStatusOk;
}
FuriStatus furi_mutex_release(FuriMutex* mutex) {
    UNUSED(mutex);
    return FuriStatusOk;
}

struct FuriMessageQueue {
    int unused;
};
FuriMessageQueue* furi_message_queue_alloc(uint32_t count, uint32_t size) {
    UNUSED(count);
    UNUSED(size);
    return calloc(1, sizeof(FuriMessageQueue));
}
void furi_message_queue_free(FuriMessageQueue* queue) {
    free(queue);
}
FuriStatus furi_message_queue_put(FuriMessageQueue* queue, const void* item, uint32_t timeout) {
    UNUSED(queue);
    UNUSED(item);
    UNUSED(timeout);
    return FuriStatusOk;
}
FuriStatus furi_message_queue_get(FuriMessageQueue* queue, void* item, uint32_t timeout) {
    UNUSED(queue);
    UNUSED(item);
    UNUSED(timeout);
    return FuriStatusErrorTimeout;
}

struct FuriTimer {
    int unused;
};
FuriTimer* furi_timer_alloc(FuriTimerCallback callback, FuriTimerType type, void* context) {
    UNUSED(callback);
    UNUSED(type);
    UNUSED(context);
    return calloc(1, sizeof(FuriTimer));
}
void furi_timer_free(FuriTimer* timer) {
    free(timer);
}
FuriStatus furi_timer_start(FuriTimer* timer, uint32_t ticks) {
    UNUSED(timer);
    UNUSED(ticks);
    return FuriStatusOk;
}
FuriStatus furi_timer_stop(FuriTimer* timer) {
    UNUSED(timer);
    return FuriStatusOk;
}

void* furi_record_open(const char* name) {
    UNUSED(name);
    return NULL;
}
void furi_record_close(const char* name) {
    UNUSED(name);
}

/* gui: apps add view ports, previews call the draw callbacks themselves */
ViewPort* view_port_alloc(void) {
    return calloc(1, 1);
}
void view_port_free(ViewPort* view_port) {
    free(view_port);
}
void view_port_draw_callback_set(ViewPort* view_port, ViewPortDrawCallback callback, void* context) {
    UNUSED(view_port);
    UNUSED(callback);
    UNUSED(context);
}
void view_port_input_callback_set(ViewPort* view_port, ViewPortInputCallback callback, void* context) {
    UNUSED(view_port);
    UNUSED(callback);
    UNUSED(context);
}
void view_port_update(ViewPort* view_port) {
    UNUSED(view_port);
}
void gui_add_view_port(Gui* gui, ViewPort* view_port, GuiLayer layer) {
    UNUSED(gui);
    UNUSED(view_port);
    UNUSED(layer);
}
void gui_remove_view_port(Gui* gui, ViewPort* view_port) {
    UNUSED(gui);
    UNUSED(view_port);
}

void notification_message(NotificationApp* app, const NotificationSequence* sequence) {
    UNUSED(app);
    UNUSED(sequence);
}

bool furi_hal_bt_scanner_is_supported(void) {
    return true;
}
bool furi_hal_bt_scanner_start(const GapScannerConfig* config, GapScannerCallback callback, void* context) {
    UNUSED(config);
    UNUSED(callback);
    UNUSED(context);
    return true;
}
bool furi_hal_bt_scanner_stop(void) {
    return true;
}
bool furi_hal_bt_scanner_is_active(void) {
    return false;
}
bool furi_hal_bt_resolve_address(const uint8_t* address, uint8_t* identity) {
    UNUSED(address);
    UNUSED(identity);
    return false; // nothing is paired on a PC
}

/* ---- the previews ---- */

Canvas* preview_canvas(void) {
    static Canvas* canvas = NULL;
    if(!canvas) canvas = canvas_init();
    // what the GUI does before a full screen view port draws
    canvas_frame_set(canvas, 0, 0, 128, 64);
    canvas_reset(canvas);
    return canvas;
}

static const char* preview_dir = ".";

void preview_set_dir(const char* dir) {
    preview_dir = dir;
}

void preview_save(Canvas* canvas, const char* name) {
    // the buffer: 8 pages of 128 columns, one byte a column of 8 pixels, the top one in bit 0
    const uint8_t* buffer = canvas_get_buffer(canvas);
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.pbm", preview_dir, name);
    FILE* file = fopen(path, "w");
    furi_check(file);
    fprintf(file, "P1\n128 64\n");
    for(int y = 0; y < 64; y++) {
        for(int x = 0; x < 128; x++) {
            const int lit = (buffer[(y / 8) * 128 + x] >> (y % 8)) & 1;
            fputc(lit ? '1' : '0', file);
        }
        fputc('\n', file);
    }
    fclose(file);
}

void preview_draw(const char* name, ViewPortDrawCallback draw, void* context) {
    Canvas* canvas = preview_canvas();
    draw(canvas, context);
    preview_save(canvas, name);
}
