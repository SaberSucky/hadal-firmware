#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Passive/active BLE observer (scanner).
 * Requires the BLE "Full" radio stack on Core2; the "Light" stack is peripheral-only.
 * Not to be used directly, but through furi_hal_bt_scanner_* APIs.
 */

#define GAP_SCANNER_MAX_DATA_SIZE (31)
#define GAP_SCANNER_ADDR_SIZE     (6)
#define GAP_SCANNER_RSSI_INVALID  (127)

typedef enum {
    GapScannerAdvTypeInd = 0x00, /**< Connectable undirected advertising */
    GapScannerAdvTypeDirectInd = 0x01, /**< Connectable directed advertising */
    GapScannerAdvTypeScanInd = 0x02, /**< Scannable undirected advertising */
    GapScannerAdvTypeNonconnInd = 0x03, /**< Non-connectable undirected advertising */
    GapScannerAdvTypeScanRsp = 0x04, /**< Scan response (active scanning only) */
} GapScannerAdvType;

typedef enum {
    GapScannerAddrTypePublic = 0x00,
    GapScannerAddrTypeRandom = 0x01,
    GapScannerAddrTypePublicIdentity = 0x02,
    GapScannerAddrTypeRandomIdentity = 0x03,
} GapScannerAddrType;

typedef struct {
    uint8_t address[GAP_SCANNER_ADDR_SIZE]; /**< Little-endian, as received over the air */
    uint8_t address_type; /**< GapScannerAddrType */
    uint8_t adv_type; /**< GapScannerAdvType */
    int8_t rssi; /**< dBm, GAP_SCANNER_RSSI_INVALID if not available */
    uint8_t data_len;
    uint8_t data[GAP_SCANNER_MAX_DATA_SIZE]; /**< Raw AD structures */
} GapScannerReport;

/**
 * Called for every received advertisement, from the BLE event thread.
 * Must be fast and must not call BLE APIs: copy the report and post it to your own queue.
 */
typedef void (*GapScannerCallback)(const GapScannerReport* report, void* context);

typedef struct {
    uint16_t interval_ms; /**< Time between scan windows, 3..10240 ms */
    uint16_t window_ms; /**< Listening time per interval, <= interval_ms */
    bool active; /**< Send scan requests to receive scan responses (device names etc.) */
} GapScannerConfig;

void gap_scanner_init(void);

bool gap_scanner_is_supported(void);

bool gap_scanner_start(const GapScannerConfig* config, GapScannerCallback callback, void* context);

bool gap_scanner_stop(void);

bool gap_scanner_is_active(void);

/** Internal: restart scanning after GAP has been (re)initialized */
void gap_scanner_on_gap_init(void);

/** Internal: parse HCI LE Advertising Report payload (after subevent code) */
void gap_scanner_process_reports(const uint8_t* payload, uint8_t payload_len);

#ifdef __cplusplus
}
#endif
