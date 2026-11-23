#include "crash_report.h"

#include <stddef.h>
#include <string.h>
#include <FreeRTOS.h>
#include <task.h>
#include <furi_hal_rtc.h>
#include <stm32wbxx.h>

#define CRASH_REPORT_MAGIC      (0x33524348U) /* "HCR3": the asset pack added */
// how far up the stack to look for return addresses
#define CRASH_REPORT_SCAN_WORDS (256U)

extern const uint32_t _etext;
extern size_t xPortGetFreeHeapSize(void);
extern size_t xPortGetMinimumEverFreeHeapSize(void);

#ifndef FURI_RAM_EXEC
__attribute__((section(".noinit"))) static FuriCrashReport crash_report;

static void crash_report_copy_text(char* to, size_t size, const char* from);

static char crash_report_app[24];
static char crash_report_pack[32];

void furi_crash_report_note_pack(const char* text) {
    crash_report_copy_text(crash_report_pack, sizeof(crash_report_pack), text ? text : "");
}
static uint32_t crash_report_app_heap;

void furi_crash_report_note_app(const char* app, uint32_t heap_free) {
    crash_report_copy_text(crash_report_app, sizeof(crash_report_app), app ? app : "");
    crash_report_app_heap = app ? heap_free : 0;
}

static uint32_t crash_report_check(const FuriCrashReport* report) {
    const uint8_t* bytes = (const uint8_t*)report;
    uint32_t hash = 2166136261U;
    for(size_t i = 0; i < offsetof(FuriCrashReport, check); i++) {
        hash ^= bytes[i];
        hash *= 16777619U;
    }
    return hash;
}

static bool crash_report_is_code(uint32_t address) {
    return address >= FLASH_BASE && address < (uint32_t)&_etext;
}

/** Constants in flash: a name written in the code ("Sub-GHz") is past the code, with the rest */
static bool crash_report_is_flash_text(uint32_t address) {
    // _sidata (stm32wb55_linker.h): the end of the constants, where .data's copy begins
    return address >= FLASH_BASE && address < (uint32_t)&_sidata;
}

static bool crash_report_is_ram(uint32_t address, uint32_t size) {
    return address >= SRAM1_BASE && address + size <= SRAM1_BASE + 0x30000U;
}

static void crash_report_copy_text(char* to, size_t size, const char* from) {
    size_t i = 0;
    const uint32_t address = (uint32_t)from;
    if(from && (crash_report_is_flash_text(address) || crash_report_is_ram(address, 1))) {
        for(; i + 1 < size && from[i]; i++) {
            to[i] = (from[i] >= ' ' && from[i] < 127) ? from[i] : '?';
        }
    }
    to[i] = '\0';
}

// Words on a stack that are return addresses into code (Thumb: odd), newest first
static void crash_report_backtrace(const uint32_t* stack) {
    size_t found = 0;
    for(size_t i = 0; i < CRASH_REPORT_SCAN_WORDS && found < FURI_CRASH_REPORT_BACKTRACE; i++) {
        if(!crash_report_is_ram((uint32_t)&stack[i], sizeof(uint32_t))) break;
        const uint32_t word = stack[i];
        if((word & 1U) && crash_report_is_code(word & ~1U)) {
            crash_report.backtrace[found++] = word & ~1U;
        }
    }
}

void furi_crash_report_capture(const char* message, const uint32_t* registers) {
    memset(&crash_report, 0, sizeof(crash_report));
    crash_report.magic = CRASH_REPORT_MAGIC;
    crash_report.timestamp = furi_hal_rtc_get_timestamp();
    crash_report.uptime_ms = xTaskGetTickCount() * (1000U / configTICK_RATE_HZ);
    memcpy(crash_report.registers, registers, sizeof(crash_report.registers));
    crash_report.heap_free = xPortGetFreeHeapSize();
    crash_report.heap_lowest = xPortGetMinimumEverFreeHeapSize();
    crash_report.exception = __get_IPSR();
    memcpy(crash_report.app, crash_report_app, sizeof(crash_report.app));
    crash_report.app_heap_free = crash_report_app_heap;
    memcpy(crash_report.pack, crash_report_pack, sizeof(crash_report.pack));
    crash_report_copy_text(crash_report.message, sizeof(crash_report.message), message);

    const uint32_t* stack;
    if(crash_report.exception) {
        // A fault in a thread: the thread's registers were pushed on its stack, pc and lr in it
        const uint32_t* frame = (const uint32_t*)__get_PSP();
        if(crash_report_is_ram((uint32_t)frame, 8 * sizeof(uint32_t)) &&
           crash_report_is_code(frame[6] & ~1U)) {
            crash_report.fault_lr = frame[5];
            crash_report.fault_pc = frame[6];
        }
        stack = frame + 8;
        crash_report_copy_text(crash_report.thread, sizeof(crash_report.thread), "(interrupt)");
    } else {
        stack = (const uint32_t*)__get_PSP();
        crash_report_copy_text(
            crash_report.thread, sizeof(crash_report.thread), pcTaskGetName(NULL));
    }
    crash_report_backtrace(stack);
    crash_report.check = crash_report_check(&crash_report);
}

const FuriCrashReport* furi_crash_report_get(void) {
    if(crash_report.magic != CRASH_REPORT_MAGIC) return NULL;
    if(crash_report.check != crash_report_check(&crash_report)) return NULL;
    return &crash_report;
}

void furi_crash_report_clear(void) {
    crash_report.magic = 0;
}
#else
void furi_crash_report_capture(const char* message, const uint32_t* registers) {
    (void)message;
    (void)registers;
}

const FuriCrashReport* furi_crash_report_get(void) {
    return NULL;
}

void furi_crash_report_clear(void) {
}
#endif
