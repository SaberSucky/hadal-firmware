/* Host stand-in for the parts of furi_hal that lib/update_util/dfu_file.c uses. */
#pragma once
#include <furi.h>

#define HOST_FLASH_BASE      0x08000000U
#define HOST_FLASH_PAGE_SIZE 4096U

static inline size_t furi_hal_flash_get_page_size(void) {
    return HOST_FLASH_PAGE_SIZE;
}

static inline int16_t furi_hal_flash_get_page_number(size_t address) {
    if(address < HOST_FLASH_BASE || address >= HOST_FLASH_BASE + 256 * HOST_FLASH_PAGE_SIZE) return -1;
    return (int16_t)((address - HOST_FLASH_BASE) / HOST_FLASH_PAGE_SIZE);
}
