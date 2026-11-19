/*
 * Minimal host implementation of the parts of furi that lib/asset_packs and lib/toolbox/compress.c
 * use, so that code can be compiled unchanged on a PC and run under sanitizers.
 */
#pragma once

#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define FuriWaitForever 0xFFFFFFFFU
#define UNUSED(x)       (void)(x)
#define MIN(a, b)       ((a) < (b) ? (a) : (b))
#define MAX(a, b)       ((a) > (b) ? (a) : (b))

#define furi_check(...) \
    do {                \
        if(!(__VA_ARGS__)) { fprintf(stderr, "furi_check failed: %s:%d\n", __FILE__, __LINE__); abort(); } \
    } while(0)
#define furi_assert(...) furi_check(__VA_ARGS__)

extern int host_log_enabled;
#define FURI_LOG_I(tag, ...) do { if(host_log_enabled) { fprintf(stderr, "[I][%s] ", tag); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr);} } while(0)
#define FURI_LOG_W(tag, ...) do { if(host_log_enabled) { fprintf(stderr, "[W][%s] ", tag); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr);} } while(0)
#define FURI_LOG_E(tag, ...) do { fprintf(stderr, "[E][%s] ", tag); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } while(0)
#define FURI_LOG_D(tag, ...) do { } while(0)

static inline uint32_t furi_get_tick(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
}

/* ---- mutex ---- */
typedef struct FuriMutex {
    pthread_mutex_t mutex;
} FuriMutex;
typedef enum { FuriMutexTypeNormal, FuriMutexTypeRecursive } FuriMutexType;

static inline FuriMutex* furi_mutex_alloc(FuriMutexType type) {
    (void)type;
    FuriMutex* m = malloc(sizeof(FuriMutex));
    pthread_mutex_init(&m->mutex, NULL);
    return m;
}
static inline int furi_mutex_acquire(FuriMutex* m, uint32_t timeout) {
    (void)timeout;
    return pthread_mutex_lock(&m->mutex);
}
static inline int furi_mutex_release(FuriMutex* m) {
    return pthread_mutex_unlock(&m->mutex);
}

/* ---- thread ---- */
typedef int32_t (*FuriThreadCallback)(void* context);
typedef enum { FuriThreadPriorityLow = 15 } FuriThreadPriority;
typedef struct FuriThread {
    pthread_t thread;
    FuriThreadCallback callback;
    void* context;
    bool started;
} FuriThread;

static void* furi_thread_trampoline(void* arg) {
    FuriThread* t = arg;
    t->callback(t->context);
    return NULL;
}
static inline FuriThread*
    furi_thread_alloc_ex(const char* name, uint32_t stack, FuriThreadCallback callback, void* context) {
    (void)name;
    (void)stack;
    FuriThread* t = calloc(1, sizeof(FuriThread));
    t->callback = callback;
    t->context = context;
    return t;
}
static inline void furi_thread_set_priority(FuriThread* t, FuriThreadPriority p) {
    (void)t;
    (void)p;
}
static inline void furi_thread_start(FuriThread* t) {
    t->started = true;
    pthread_create(&t->thread, NULL, furi_thread_trampoline, t);
}
static inline bool furi_thread_join(FuriThread* t) {
    if(t->started) {
        pthread_join(t->thread, NULL);
        t->started = false;
    }
    return true;
}
static inline void furi_thread_free(FuriThread* t) {
    free(t);
}

/* ---- timer: one-shot, backed by a thread; restart re-arms it, firing in the timer thread ---- */
typedef enum { FuriStatusOk = 0, FuriStatusError = -1 } FuriStatus;
typedef enum { FuriTimerTypeOnce = 0, FuriTimerTypePeriodic = 1 } FuriTimerType;
typedef void (*FuriTimerCallback)(void* context);

static inline uint32_t furi_ms_to_ticks(uint32_t ms) {
    return ms; // the firmware's tick is 1 ms
}

typedef struct FuriTimer {
    pthread_t thread;
    pthread_mutex_t lock;
    pthread_cond_t cond;
    FuriTimerCallback callback;
    void* context;
    struct timespec deadline;
    bool armed, quit, started;
} FuriTimer;

static inline bool furi_timer_deadline_passed(const FuriTimer* t) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return now.tv_sec > t->deadline.tv_sec ||
           (now.tv_sec == t->deadline.tv_sec && now.tv_nsec >= t->deadline.tv_nsec);
}

static void* furi_timer_thread(void* arg) {
    FuriTimer* t = arg;
    pthread_mutex_lock(&t->lock);
    while(!t->quit) {
        if(!t->armed) {
            pthread_cond_wait(&t->cond, &t->lock);
        } else if(furi_timer_deadline_passed(t)) {
            t->armed = false;
            pthread_mutex_unlock(&t->lock);
            t->callback(t->context);
            pthread_mutex_lock(&t->lock);
        } else {
            pthread_cond_timedwait(&t->cond, &t->lock, &t->deadline);
        }
    }
    pthread_mutex_unlock(&t->lock);
    return NULL;
}

static inline FuriTimer*
    furi_timer_alloc(FuriTimerCallback callback, FuriTimerType type, void* context) {
    (void)type;
    FuriTimer* t = calloc(1, sizeof(FuriTimer));
    pthread_condattr_t attr;
    pthread_condattr_init(&attr);
    pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
    pthread_cond_init(&t->cond, &attr);
    pthread_condattr_destroy(&attr);
    pthread_mutex_init(&t->lock, NULL);
    t->callback = callback;
    t->context = context;
    return t;
}

static inline FuriStatus furi_timer_restart(FuriTimer* t, uint32_t ticks) {
    pthread_mutex_lock(&t->lock);
    clock_gettime(CLOCK_MONOTONIC, &t->deadline);
    t->deadline.tv_sec += ticks / 1000u;
    t->deadline.tv_nsec += (long)(ticks % 1000u) * 1000000L;
    if(t->deadline.tv_nsec >= 1000000000L) {
        t->deadline.tv_nsec -= 1000000000L;
        t->deadline.tv_sec++;
    }
    t->armed = true;
    if(!t->started) {
        t->started = true;
        pthread_create(&t->thread, NULL, furi_timer_thread, t);
    }
    pthread_cond_signal(&t->cond);
    pthread_mutex_unlock(&t->lock);
    return FuriStatusOk;
}

static inline FuriStatus furi_timer_stop(FuriTimer* t) {
    pthread_mutex_lock(&t->lock);
    t->armed = false;
    pthread_mutex_unlock(&t->lock);
    return FuriStatusOk;
}

static inline void furi_timer_free(FuriTimer* t) {
    pthread_mutex_lock(&t->lock);
    t->quit = true;
    pthread_cond_signal(&t->cond);
    pthread_mutex_unlock(&t->lock);
    if(t->started) pthread_join(t->thread, NULL);
    pthread_mutex_destroy(&t->lock);
    pthread_cond_destroy(&t->cond);
    free(t);
}

/* ---- records ---- */
#define RECORD_STORAGE "storage"
static inline void* furi_record_open(const char* name) {
    (void)name;
    static int storage_token;
    return &storage_token;
}
static inline void furi_record_close(const char* name) {
    (void)name;
}

/* glibc has strlcpy since 2.38, the firmware's libc always has it */
