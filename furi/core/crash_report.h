/**
 * @file crash_report.h
 * @brief What the firmware was doing when it crashed, kept across the restart.
 *
 * Filled in by the crash handler (check.c) in RAM that the startup code does not clear, and
 * written to the SD card by the next start (desktop). Not in the firmware that runs from RAM.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FURI_CRASH_REPORT_BACKTRACE (10U)

typedef struct {
    uint32_t magic;
    uint32_t timestamp; /**< RTC time of the crash */
    uint32_t uptime_ms;
    uint32_t registers[13]; /**< r0..r11 and lr where the crash was raised */
    uint32_t fault_pc; /**< for a fault in a thread: where it happened, else 0 */
    uint32_t fault_lr;
    uint32_t backtrace[FURI_CRASH_REPORT_BACKTRACE]; /**< likely return addresses, newest first */
    uint32_t heap_free;
    uint32_t heap_lowest;
    uint32_t exception; /**< IPSR: 0 in a thread, else the exception number */
    char message[64];
    char thread[24];
    char app[24]; /**< the app running, empty for none */
    uint32_t app_heap_free; /**< free heap as that app started */
    char pack[32]; /**< the asset pack: what it held, or that it was loading */
    uint32_t check;
} FuriCrashReport;

/** From the crash handler, interrupts off: gather what can be gathered */
void furi_crash_report_capture(const char* message, const uint32_t* registers);
/** The report of the last crash, or NULL */
const FuriCrashReport* furi_crash_report_get(void);

/** The app now running (NULL for none) and the free heap as it started, for a crash report */
void furi_crash_report_note_app(const char* app, uint32_t heap_free);

/** What the asset pack holds now (or that it is loading), for a crash report; NULL for none */
void furi_crash_report_note_pack(const char* text);
void furi_crash_report_clear(void);

#ifdef __cplusplus
}
#endif
