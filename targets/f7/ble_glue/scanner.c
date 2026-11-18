#include "scanner.h"
#include "ble_glue.h"

#include <ble/ble.h>
#include <interface/patterns/ble_thread/shci/shci.h>
#include <furi.h>

#define TAG "BleScanner"

#define GAP_MS_TO_SCAN_UNITS(x) ((uint16_t)(((uint32_t)(x) * 1000U) / 625U))

// HCI LE Advertising Report: per report header is
// Event_Type(1) + Address_Type(1) + Address(6) + Length_Data(1), then Data, then RSSI(1)
#define GAP_SCANNER_REPORT_HDR_SIZE (9)

typedef struct {
    FuriMutex* mutex;
    GapScannerConfig config;
    GapScannerCallback callback;
    void* context;
    bool active;
} GapScanner;

static GapScanner scanner = {0};

void gap_scanner_init(void) {
    if(!scanner.mutex) {
        scanner.mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    }
}

bool gap_scanner_is_supported(void) {
    const BleGlueC2Info* c2_info = ble_glue_get_c2_info();
    return c2_info && c2_info->mode == BleGlueC2ModeStack &&
           c2_info->StackType == INFO_STACK_TYPE_BLE_FULL;
}

static bool gap_scanner_issue_start(const GapScannerConfig* config) {
    uint16_t interval = GAP_MS_TO_SCAN_UNITS(config->interval_ms);
    uint16_t window = GAP_MS_TO_SCAN_UNITS(config->window_ms);
    tBleStatus status = aci_gap_start_observation_proc(
        interval,
        window,
        config->active ? HCI_SCAN_TYPE_ACTIVE : HCI_SCAN_TYPE_PASSIVE,
        // Scan requests (active mode) use the static random address, not the public one
        GAP_STATIC_RANDOM_ADDR,
        0, // No duplicate filtering: we want every advertisement for RSSI tracking
        0); // Accept all advertisers
    if(status != BLE_STATUS_SUCCESS) {
        FURI_LOG_E(TAG, "Start failed: 0x%02X", status);
        return false;
    }
    return true;
}

bool gap_scanner_start(const GapScannerConfig* config, GapScannerCallback callback, void* context) {
    furi_check(scanner.mutex);
    furi_check(config);
    furi_check(callback);
    furi_check(config->interval_ms >= 3 && config->interval_ms <= 10240);
    furi_check(config->window_ms >= 3 && config->window_ms <= config->interval_ms);

    if(!gap_scanner_is_supported()) {
        FURI_LOG_W(TAG, "Radio stack does not support scanning");
        return false;
    }

    furi_mutex_acquire(scanner.mutex, FuriWaitForever);
    if(scanner.active) {
        furi_mutex_release(scanner.mutex);
        return false;
    }
    scanner.config = *config;
    scanner.callback = callback;
    scanner.context = context;
    furi_mutex_release(scanner.mutex);

    // Never hold our mutex across HCI commands: the report path takes it too
    bool started = gap_scanner_issue_start(config);

    furi_mutex_acquire(scanner.mutex, FuriWaitForever);
    scanner.active = started;
    if(!started) {
        scanner.callback = NULL;
        scanner.context = NULL;
    }
    furi_mutex_release(scanner.mutex);

    FURI_LOG_I(TAG, started ? "Started" : "Not started");
    return started;
}

bool gap_scanner_stop(void) {
    furi_check(scanner.mutex);

    furi_mutex_acquire(scanner.mutex, FuriWaitForever);
    bool was_active = scanner.active;
    scanner.active = false;
    furi_mutex_release(scanner.mutex);

    if(!was_active) return false;

    tBleStatus status = aci_gap_terminate_gap_proc(GAP_OBSERVATION_PROC);
    if(status != BLE_STATUS_SUCCESS) {
        // Procedure may already be gone (e.g. radio was reset), that's fine
        FURI_LOG_W(TAG, "Terminate status: 0x%02X", status);
    }

    furi_mutex_acquire(scanner.mutex, FuriWaitForever);
    scanner.callback = NULL;
    scanner.context = NULL;
    furi_mutex_release(scanner.mutex);

    FURI_LOG_I(TAG, "Stopped");
    return true;
}

bool gap_scanner_is_active(void) {
    return scanner.active;
}

void gap_scanner_on_gap_init(void) {
    if(!scanner.mutex) return;

    furi_mutex_acquire(scanner.mutex, FuriWaitForever);
    bool restart = scanner.active;
    GapScannerConfig config = scanner.config;
    furi_mutex_release(scanner.mutex);

    if(restart) {
        FURI_LOG_I(TAG, "Restoring scan after GAP init");
        if(!gap_scanner_issue_start(&config)) {
            furi_mutex_acquire(scanner.mutex, FuriWaitForever);
            scanner.active = false;
            furi_mutex_release(scanner.mutex);
        }
    }
}

void gap_scanner_process_reports(const uint8_t* payload, uint8_t payload_len) {
    if(!scanner.mutex || !scanner.active || payload_len < 1) return;

    furi_mutex_acquire(scanner.mutex, FuriWaitForever);
    GapScannerCallback callback = scanner.callback;
    void* context = scanner.context;

    if(callback) {
        uint8_t num_reports = payload[0];
        size_t offset = 1;
        GapScannerReport report;

        for(uint8_t i = 0; i < num_reports; i++) {
            if(offset + GAP_SCANNER_REPORT_HDR_SIZE > payload_len) break;
            const uint8_t* r = &payload[offset];
            uint8_t data_len = r[8];
            if(offset + GAP_SCANNER_REPORT_HDR_SIZE + data_len + 1 > payload_len) break;

            report.adv_type = r[0];
            report.address_type = r[1];
            memcpy(report.address, &r[2], GAP_SCANNER_ADDR_SIZE);
            report.data_len = MIN(data_len, GAP_SCANNER_MAX_DATA_SIZE);
            memcpy(report.data, &r[GAP_SCANNER_REPORT_HDR_SIZE], report.data_len);
            report.rssi = (int8_t)r[GAP_SCANNER_REPORT_HDR_SIZE + data_len];

            callback(&report, context);
            offset += GAP_SCANNER_REPORT_HDR_SIZE + data_len + 1;
        }
    }
    furi_mutex_release(scanner.mutex);
}
