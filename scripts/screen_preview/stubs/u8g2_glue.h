#pragma once
/* the Flipper's display, as a 128x64 buffer with no display behind it */
#include <u8g2.h>
#include <stdbool.h>

static inline uint8_t u8g2_gpio_and_delay_stm32(u8x8_t* u8x8, uint8_t msg, uint8_t arg_int, void* arg_ptr) {
    (void)u8x8; (void)msg; (void)arg_int; (void)arg_ptr;
    return 1;
}
static inline uint8_t u8x8_hw_spi_stm32(u8x8_t* u8x8, uint8_t msg, uint8_t arg_int, void* arg_ptr) {
    (void)u8x8; (void)msg; (void)arg_int; (void)arg_ptr;
    return 1;
}
static inline void u8g2_Setup_st756x_flipper(
    u8g2_t* u8g2,
    const u8g2_cb_t* rotation,
    u8x8_msg_cb byte_cb,
    u8x8_msg_cb gpio_and_delay_cb) {
    uint8_t tile_buf_height;
    u8g2_SetupDisplay(u8g2, u8x8_d_null_cb, u8x8_cad_empty, byte_cb, gpio_and_delay_cb);
    // the same buffer and layout as the real display, which the null device cannot describe
    static const u8x8_display_info_t info = {
        .tile_width = 16, .tile_height = 8, .pixel_width = 128, .pixel_height = 64,
    };
    u8g2->u8x8.display_info = &info;
    uint8_t* buf = u8g2_m_16_8_f(&tile_buf_height);
    u8g2_SetupBuffer(u8g2, buf, tile_buf_height, u8g2_ll_hvline_vertical_top_lsb, rotation);
}
