/**
 * @file update_trace.h
 * @brief Steps of the radio and firmware update, kept across restarts.
 *
 * The part of the update that writes the radio stack and the firmware runs from RAM with the SD
 * card read-only, and the radio chip restarts the Flipper several times in the middle of it.
 * So it leaves its steps in RTC backup registers instead (one register write each, safe at any
 * moment), and the firmware writes them to the SD card log after the update.
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UpdateTraceStepNone = 0,
    UpdateTraceStepStarted, /**< read the manifest */
    UpdateTraceStepRadioWait, /**< waiting for the radio core to start */
    UpdateTraceStepRadioState, /**< detail: core mode << 4 | stack type */
    UpdateTraceStepRadioCurrent, /**< the stack is already the one in the package */
    UpdateTraceStepFirmwareFirst, /**< writing the new firmware before touching the radio */
    UpdateTraceStepFirmwareFirstDone,
    UpdateTraceStepRestartToFus, /**< asked the radio core to restart into FUS */
    UpdateTraceStepFusSettled, /**< detail: result */
    UpdateTraceStepStackDelete, /**< detail: result */
    UpdateTraceStepStackWrite, /**< detail: tenths written */
    UpdateTraceStepStackInstall, /**< detail: result */
    UpdateTraceStepStackInstalled, /**< detail: result */
    UpdateTraceStepStartStack, /**< asked the radio core to start the stack */
    UpdateTraceStepWaitRestart, /**< waiting for the radio core to restart the Flipper */
    UpdateTraceStepOptionBytes,
    UpdateTraceStepFirmwareWrite,
    UpdateTraceStepFirmwareDone,
    UpdateTraceStepFailed, /**< detail: the stage it failed in */
    UpdateTraceStepDone,
    UpdateTraceStepCount,
} UpdateTraceStep;

/** Called once when the RAM updater starts: a new update, or the same one after a restart */
void update_trace_begin(void);

/** Remember the step the RAM updater is at */
void update_trace_step(UpdateTraceStep step, uint8_t detail);

/** In the firmware after an update: write what the RAM updater left to the SD log, and forget it */
void update_trace_report(void);

#ifdef __cplusplus
}
#endif
