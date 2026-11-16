#pragma once

#include <stdint.h>
#include "driver/rmt_tx.h"
#include "driver/gpio.h"
#include "esp_err.h"

typedef struct {
    rmt_encoder_t base;
    rmt_encoder_t *bytes_encoder;
    rmt_encoder_t *copy_encoder;
    int state;
    rmt_symbol_word_t reset_code;
} rmt_led_strip_encoder_t;

typedef struct {
    rmt_channel_handle_t channel;
    rmt_led_strip_encoder_t encoder;
} ws2812_strip_t;


/**
 * @brief Initializes the WS2812B LED strip driver using the RMT peripheral.
 *
 * @param[out] strip    Pointer to the WS2812B strip handle structure.
 * @param[in]  gpio_num GPIO pin number connected to the LED strip data line (DIN).
 */
esp_err_t ws2812_init(ws2812_strip_t *strip, gpio_num_t gpio_num);

/**
 * @brief Transmits pixel color data to the WS2812B LED strip.
 *
 * @param[in] strip      Pointer to the initialized WS2812B strip handle.
 * @param[in] pixel_data Pointer to the color buffer in GRB byte order (3 bytes per LED).
 * @param[in] data_size  Size of the pixel_data buffer in bytes.
 */
esp_err_t ws2812_send(ws2812_strip_t *strip, const uint8_t *pixel_data, size_t data_size);