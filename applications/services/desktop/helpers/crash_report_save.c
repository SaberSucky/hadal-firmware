#include "crash_report_save.h"

#include <furi.h>
#include <furi_hal_version.h>
#include <core/crash_report.h>
#include <datetime/datetime.h>

#define TAG "CrashReport"

static void crash_report_format(const FuriCrashReport* report, FuriString* text) {
    const Version* version = furi_hal_version_get_firmware_version();
    DateTime when;
    datetime_timestamp_to_datetime(report->timestamp, &when);

    furi_string_printf(
        text,
        "Hadal crash report\n"
        "Firmware: %s (commit %s, built %s)\n"
        "Crashed: %04u-%02u-%02u %02u:%02u:%02u, %lu.%lu s after the start\n",
        version_get_version(version),
        version_get_githash(version),
        version_get_builddate(version),
        when.year,
        when.month,
        when.day,
        when.hour,
        when.minute,
        when.second,
        (unsigned long)(report->uptime_ms / 1000),
        (unsigned long)(report->uptime_ms % 1000 / 100));

    if(report->exception) {
        furi_string_cat_printf(
            text, "In: an interrupt or fault handler (exception %lu)\n", report->exception);
    } else {
        furi_string_cat_printf(text, "In: the thread \"%s\"\n", report->thread);
    }
    furi_string_cat_printf(text, "Message: %s\n", report->message);
    if(report->fault_pc) {
        furi_string_cat_printf(
            text,
            "Fault at: 0x%08lX (in a function called from 0x%08lX)\n",
            report->fault_pc,
            report->fault_lr & ~1UL);
    }
    furi_string_cat_printf(text, "Raised from: 0x%08lX\n", report->registers[12] & ~1UL);

    furi_string_cat(text, "Call chain:");
    for(size_t i = 0; i < FURI_CRASH_REPORT_BACKTRACE && report->backtrace[i]; i++) {
        furi_string_cat_printf(text, " 0x%08lX", report->backtrace[i]);
    }
    furi_string_cat(text, "\nRegisters:");
    for(size_t i = 0; i < 12; i++) {
        furi_string_cat_printf(text, "%s r%u=%08lX", i % 4 ? "" : "\n ", i, report->registers[i]);
    }
    furi_string_cat_printf(
        text,
        "\nMemory: %lu bytes free, %lu at the lowest\n"
        "\nTo see the functions: python3 scripts/crash_report.py <this file>\n",
        report->heap_free,
        report->heap_lowest);
}

bool crash_report_save(Storage* storage) {
    const FuriCrashReport* report = furi_crash_report_get();
    if(!report) return true;
    if(storage_sd_status(storage) != FSE_OK) return false;

    DateTime when;
    datetime_timestamp_to_datetime(report->timestamp, &when);
    FuriString* path = furi_string_alloc_printf(
        "%s/crash_%04u%02u%02u_%02u%02u%02u.txt",
        CRASH_REPORT_DIR,
        when.year,
        when.month,
        when.day,
        when.hour,
        when.minute,
        when.second);
    FuriString* text = furi_string_alloc();
    crash_report_format(report, text);

    storage_simply_mkdir(storage, CRASH_REPORT_DIR);
    File* file = storage_file_alloc(storage);
    bool saved = false;
    if(storage_file_open(file, furi_string_get_cstr(path), FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        const size_t size = furi_string_size(text);
        saved = storage_file_write(file, furi_string_get_cstr(text), size) == size;
    }
    storage_file_free(file);
    FURI_LOG_I(TAG, "%s %s", saved ? "Saved" : "Could not save", furi_string_get_cstr(path));

    furi_string_free(text);
    furi_string_free(path);
    // once only, even if the card refused it: a report must not keep the next starts busy
    furi_crash_report_clear();
    return true;
}
