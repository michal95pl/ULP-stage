#include "ws2812.h"
#include "driver/rmt_tx.h"
#include <stdlib.h>
#include <app_utils.h>
#include <string.h>

#define RMT_RESOLUTION_HZ 10000000 // 10 MHz -> 1 tik = 100 ns

static size_t rmt_encode_led_strip(rmt_encoder_t *encoder, rmt_channel_handle_t channel,
                                   const void *primary_data, size_t data_size,
                                   rmt_encode_state_t *ret_state)
{
    rmt_led_strip_encoder_t *led_encoder = __containerof(encoder, rmt_led_strip_encoder_t, base);
    rmt_encode_state_t session_state = RMT_ENCODING_RESET;
    rmt_encode_state_t state = RMT_ENCODING_RESET;
    size_t encoded_symbols = 0;

    switch (led_encoder->state) {
    // color
    case 0:
        encoded_symbols += led_encoder->bytes_encoder->encode(
            led_encoder->bytes_encoder, channel, primary_data, data_size, &session_state
        );
        if (session_state & RMT_ENCODING_COMPLETE) {
            led_encoder->state = 1; // set reset state
        }
        if (session_state & RMT_ENCODING_MEM_FULL) {
            state |= RMT_ENCODING_MEM_FULL;
            goto out;
        }
    // reset
    /* fall through */
    case 1:
        encoded_symbols += led_encoder->copy_encoder->encode(
            led_encoder->copy_encoder, channel, &led_encoder->reset_code, sizeof(led_encoder->reset_code), &session_state
        );
        if (session_state & RMT_ENCODING_COMPLETE) {
            led_encoder->state = 0;
            state |= RMT_ENCODING_COMPLETE;
        }
        if (session_state & RMT_ENCODING_MEM_FULL) {
            state |= RMT_ENCODING_MEM_FULL;
            goto out;
        }
    }
out:
    *ret_state = state;
    return encoded_symbols;
}

static esp_err_t rmt_del_led_strip_encoder(rmt_encoder_t *encoder)
{
    rmt_led_strip_encoder_t *led_encoder = __containerof(encoder, rmt_led_strip_encoder_t, base);
    rmt_del_encoder(led_encoder->bytes_encoder);
    rmt_del_encoder(led_encoder->copy_encoder);
    return ESP_OK;
}

static esp_err_t rmt_led_strip_encoder_reset(rmt_encoder_t *encoder)
{
    rmt_led_strip_encoder_t *led_encoder = __containerof(encoder, rmt_led_strip_encoder_t, base);
    rmt_encoder_reset(led_encoder->bytes_encoder);
    rmt_encoder_reset(led_encoder->copy_encoder);
    led_encoder->state = 0;
    return ESP_OK;
}

esp_err_t ws2812_init(ws2812_strip_t *strip, gpio_num_t gpio_num)
{
    if (!strip) return ESP_ERR_INVALID_ARG;
    memset(strip, 0, sizeof(ws2812_strip_t));

    rmt_tx_channel_config_t tx_chan_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .gpio_num = gpio_num,
        .mem_block_symbols = 96,
        .resolution_hz = RMT_RESOLUTION_HZ,
        .trans_queue_depth = 1,
    };
    CHECK_ERR(rmt_new_tx_channel(&tx_chan_config, &strip->channel));

    strip->encoder.base.encode = rmt_encode_led_strip;
    strip->encoder.base.del = rmt_del_led_strip_encoder;
    strip->encoder.base.reset = rmt_led_strip_encoder_reset;

    rmt_bytes_encoder_config_t bytes_encoder_config = {
        .bit0 = { .level0 = 1, .duration0 = 4, .level1 = 0, .duration1 = 8 }, // 400 ns H, 800 ns L
        .bit1 = { .level0 = 1, .duration0 = 8, .level1 = 0, .duration1 = 4 }, // 800 ns H, 400 ns L
        .flags.msb_first = 1,
    };
    CHECK_ERR(rmt_new_bytes_encoder(&bytes_encoder_config, &strip->encoder.bytes_encoder));

    rmt_copy_encoder_config_t copy_encoder_config = {};
    CHECK_ERR(rmt_new_copy_encoder(&copy_encoder_config, &strip->encoder.copy_encoder));

    strip->encoder.reset_code = (rmt_symbol_word_t){
        .level0 = 0, .duration0 = 250,
        .level1 = 0, .duration1 = 250,
    };

    return rmt_enable(strip->channel);
}

esp_err_t ws2812_send(ws2812_strip_t *strip, const uint8_t *pixel_data, size_t data_size)
{
    if (!strip || !pixel_data) {
        return ESP_ERR_INVALID_ARG;
    }

    rmt_transmit_config_t tx_config = {
        .loop_count = 0,
    };

    CHECK_ERR(rmt_transmit(strip->channel, &strip->encoder.base, pixel_data, data_size, &tx_config));

    return rmt_tx_wait_all_done(strip->channel, 1000);
}