#pragma once
/* the scanner part of furi_hal_bt, with the target's own types */
#include <scanner.h>
bool furi_hal_bt_scanner_is_supported(void);
bool furi_hal_bt_scanner_start(const GapScannerConfig* config, GapScannerCallback callback, void* context);
bool furi_hal_bt_scanner_stop(void);
bool furi_hal_bt_scanner_is_active(void);
bool furi_hal_bt_resolve_address(const uint8_t* address, uint8_t* identity);
