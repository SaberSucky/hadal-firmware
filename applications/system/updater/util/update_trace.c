#include "update_trace.h"
#include "update_log.h"

#include <furi_hal_rtc.h>

// FuriHalRtcRegisterUpdateTrace: marker | starts | step | detail
#define TRACE_MARKER        (0x48U) /* 'H' */
#define TRACE_MARKER_SHIFT  (24U)
#define TRACE_STARTS_SHIFT  (16U)
#define TRACE_STEP_SHIFT    (8U)
// A start this soon after the update's first is the same update after a restart
#define TRACE_SAME_UPDATE_S (20U * 60U)

static uint32_t trace_pack(uint8_t starts, uint8_t step, uint8_t detail) {
    return (TRACE_MARKER << TRACE_MARKER_SHIFT) | ((uint32_t)starts << TRACE_STARTS_SHIFT) |
           ((uint32_t)step << TRACE_STEP_SHIFT) | detail;
}

static bool trace_valid(uint32_t value) {
    return (value >> TRACE_MARKER_SHIFT) == TRACE_MARKER;
}

static uint8_t trace_starts(uint32_t value) {
    return (value >> TRACE_STARTS_SHIFT) & 0xFF;
}

static uint8_t trace_step(uint32_t value) {
    return (value >> TRACE_STEP_SHIFT) & 0xFF;
}

void update_trace_begin(void) {
    const uint32_t now = furi_hal_rtc_get_timestamp();
    const uint32_t current = furi_hal_rtc_get_register(FuriHalRtcRegisterUpdateTrace);
    const uint32_t started = furi_hal_rtc_get_register(FuriHalRtcRegisterUpdateTraceStart);
    const bool unfinished = trace_valid(current) && trace_step(current) != UpdateTraceStepDone;

    if(unfinished && now >= started && now - started < TRACE_SAME_UPDATE_S) {
        // The same update, restarted by the radio core (or by a crash)
        const uint8_t starts = trace_starts(current);
        furi_hal_rtc_set_register(
            FuriHalRtcRegisterUpdateTrace,
            trace_pack(starts < 0xFF ? starts + 1 : starts, UpdateTraceStepStarted, 0));
        return;
    }
    if(unfinished) {
        // An earlier update never finished: keep where it stopped for the log
        furi_hal_rtc_set_register(FuriHalRtcRegisterUpdateTracePrevious, current);
    }
    furi_hal_rtc_set_register(FuriHalRtcRegisterUpdateTraceStart, now);
    furi_hal_rtc_set_register(
        FuriHalRtcRegisterUpdateTrace, trace_pack(1, UpdateTraceStepStarted, 0));
}

void update_trace_step(UpdateTraceStep step, uint8_t detail) {
    const uint32_t current = furi_hal_rtc_get_register(FuriHalRtcRegisterUpdateTrace);
    const uint8_t starts = trace_valid(current) ? trace_starts(current) : 1;
    furi_hal_rtc_set_register(
        FuriHalRtcRegisterUpdateTrace, trace_pack(starts, (uint8_t)step, detail));
}

static const char* const trace_step_names[UpdateTraceStepCount] = {
    [UpdateTraceStepNone] = "none",
    [UpdateTraceStepStarted] = "started",
    [UpdateTraceStepRadioWait] = "waiting for the radio core",
    [UpdateTraceStepRadioState] = "radio core state read",
    [UpdateTraceStepRadioCurrent] = "radio stack already current",
    [UpdateTraceStepFirmwareFirst] = "writing the firmware before the radio",
    [UpdateTraceStepFirmwareFirstDone] = "firmware written before the radio",
    [UpdateTraceStepRestartToFus] = "restarting into FUS",
    [UpdateTraceStepFusSettled] = "FUS settled",
    [UpdateTraceStepStackDelete] = "deleting the radio stack",
    [UpdateTraceStepStackWrite] = "writing the radio stack",
    [UpdateTraceStepStackInstall] = "installing the radio stack",
    [UpdateTraceStepStackInstalled] = "radio stack installed",
    [UpdateTraceStepStartStack] = "starting the radio stack",
    [UpdateTraceStepWaitRestart] = "waiting for the radio core to restart the Flipper",
    [UpdateTraceStepOptionBytes] = "checking option bytes",
    [UpdateTraceStepFirmwareWrite] = "writing the firmware",
    [UpdateTraceStepFirmwareDone] = "firmware written",
    [UpdateTraceStepFailed] = "failed",
    [UpdateTraceStepDone] = "done",
};

static void trace_log(const char* what, uint32_t value, uint32_t started) {
    const uint8_t step = trace_step(value);
    update_log(
        "%s: %u start(s), last step %u (%s), detail %u, began at RTC %lu",
        what,
        trace_starts(value),
        step,
        step < UpdateTraceStepCount ? trace_step_names[step] : "?",
        (unsigned)(value & 0xFF),
        (unsigned long)started);
}

void update_trace_report(void) {
    const uint32_t current = furi_hal_rtc_get_register(FuriHalRtcRegisterUpdateTrace);
    const uint32_t started = furi_hal_rtc_get_register(FuriHalRtcRegisterUpdateTraceStart);
    const uint32_t previous = furi_hal_rtc_get_register(FuriHalRtcRegisterUpdateTracePrevious);

    if(trace_valid(previous)) trace_log("an earlier update that did not finish", previous, 0);
    if(trace_valid(current)) {
        trace_log("radio and firmware update (from RAM)", current, started);
    } else {
        update_log("radio and firmware update (from RAM): left no steps");
    }
    furi_hal_rtc_set_register(FuriHalRtcRegisterUpdateTrace, 0);
    furi_hal_rtc_set_register(FuriHalRtcRegisterUpdateTraceStart, 0);
    furi_hal_rtc_set_register(FuriHalRtcRegisterUpdateTracePrevious, 0);
}
