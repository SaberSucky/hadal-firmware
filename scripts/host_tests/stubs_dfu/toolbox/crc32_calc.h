/* Host stand-in for toolbox/crc32_calc.h: the same CRC-32 (zlib polynomial) over a whole file. */
#pragma once
#include <storage/storage.h>

typedef void (*FileCrcProgressCb)(const uint8_t progress, void* context);

static inline uint32_t crc32_calc_file(File* file, const FileCrcProgressCb progress_cb, void* context) {
    (void)progress_cb;
    (void)context;
    uint32_t crc = 0xFFFFFFFFU;
    uint8_t buffer[512];
    storage_file_seek(file, 0, true);
    uint16_t n;
    while((n = storage_file_read(file, buffer, sizeof(buffer))) > 0) {
        for(uint16_t i = 0; i < n; i++) {
            crc ^= buffer[i];
            for(int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320U & -(crc & 1U));
        }
    }
    return crc ^ 0xFFFFFFFFU;
}
