/**
 * @file momentum/settings.h
 * @brief For community apps written for Momentum, which read a few of its settings: Hadal's
 * values, fixed, so they build and run unchanged (scripts/bundle_apps.py bundles some of them).
 * Header only: nothing of it is in the firmware.
 */
#pragma once

#include <stdbool.h>
#include <furi_hal_serial_types.h>

/** Which of the two external SPI handles an app with two SPI devices wired at once should use
 * for the second one (furi_hal_spi_bus_handle_external, or _external_extra on its own CS pin) */
typedef enum {
    SpiDefault,
    SpiExtra,
} MomentumSpiChoice;

typedef struct {
    FuriHalSerialId uart_esp_channel; /**< an ESP32/ESP8266 board: pins 13 and 14, as Hadal's apps */
    FuriHalSerialId uart_nmea_channel; /**< a GPS module: the same pins */
    bool midnight_format_00; /**< 12-hour clocks show midnight as 00, not 12 */
    MomentumSpiChoice spi_nrf24_handle; /**< an NRF24 module alongside another external device */
} MomentumSettings;

static const MomentumSettings momentum_settings = {
    .uart_esp_channel = FuriHalSerialIdUsart,
    .uart_nmea_channel = FuriHalSerialIdUsart,
    .midnight_format_00 = false,
    .spi_nrf24_handle = SpiDefault,
};
