/*
 * Host furi for screen previews: enough for the GUI drawing code (canvas.c, elements.c, icons) and
 * for an app's draw callback to be compiled unchanged on a PC. Nothing here runs an app: there is
 * no scheduler, the records are empty and the time is whatever the preview sets.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <core/common_defines.h>
#include <core/string.h>

#define FuriWaitForever 0xFFFFFFFFU

#define furi_check(...)                                                              \
    do {                                                                             \
        if(!(__VA_ARGS__)) {                                                         \
            fprintf(stderr, "furi_check failed: %s:%d\n", __FILE__, __LINE__);      \
            abort();                                                                 \
        }                                                                            \
    } while(0)
#define furi_assert(...) furi_check(__VA_ARGS__)
#define furi_crash(...)  furi_check(0)

#define FURI_LOG_I(tag, ...) do { } while(0)
#define FURI_LOG_W(tag, ...) do { } while(0)
#define FURI_LOG_E(tag, ...) do { } while(0)
#define FURI_LOG_D(tag, ...) do { } while(0)
#define FURI_LOG_T(tag, ...) do { } while(0)

typedef enum {
    FuriStatusOk = 0,
    FuriStatusError = -1,
    FuriStatusErrorTimeout = -2,
} FuriStatus;

/* the time the preview is drawn at */
extern uint32_t preview_tick;
static inline uint32_t furi_get_tick(void) {
    return preview_tick;
}
static inline uint32_t furi_kernel_get_tick_frequency(void) {
    return 1000;
}
static inline uint32_t furi_ms_to_ticks(uint32_t milliseconds) {
    return milliseconds;
}
static inline void furi_delay_ms(uint32_t ms) {
    (void)ms;
}
static inline void furi_delay_us(uint32_t us) {
    (void)us;
}

typedef struct FuriMutex FuriMutex;
typedef enum { FuriMutexTypeNormal, FuriMutexTypeRecursive } FuriMutexType;
FuriMutex* furi_mutex_alloc(FuriMutexType type);
void furi_mutex_free(FuriMutex* mutex);
FuriStatus furi_mutex_acquire(FuriMutex* mutex, uint32_t timeout);
FuriStatus furi_mutex_release(FuriMutex* mutex);

typedef struct FuriMessageQueue FuriMessageQueue;
FuriMessageQueue* furi_message_queue_alloc(uint32_t count, uint32_t size);
void furi_message_queue_free(FuriMessageQueue* queue);
FuriStatus furi_message_queue_put(FuriMessageQueue* queue, const void* item, uint32_t timeout);
FuriStatus furi_message_queue_get(FuriMessageQueue* queue, void* item, uint32_t timeout);

typedef enum { FuriTimerTypeOnce, FuriTimerTypePeriodic } FuriTimerType;
typedef void (*FuriTimerCallback)(void* context);
typedef struct FuriTimer FuriTimer;
FuriTimer* furi_timer_alloc(FuriTimerCallback callback, FuriTimerType type, void* context);
void furi_timer_free(FuriTimer* timer);
FuriStatus furi_timer_start(FuriTimer* timer, uint32_t ticks);
FuriStatus furi_timer_stop(FuriTimer* timer);

void* furi_record_open(const char* name);
void furi_record_close(const char* name);
