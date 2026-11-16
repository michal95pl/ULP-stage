#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "wifi_manager.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "ws2812.h"

#define LED_COUNT 3
#define LED_GPIO GPIO_NUM_46

void on_wifi_state_changed(wifi_app_status_t status) 
{
    if (status == WIFI_APP_CONNECTED) {
        printf("Connected");
    } else if (status == WIFI_APP_DISCONNECTED) {
        printf("Disconnected");
    }
}

void on_wifi_data_received(uint8_t buffer[], uint16_t length)
{
}

static ws2812_strip_t strip;
static uint8_t led_buffer[LED_COUNT * 3];

static void set_pixel_rgb(uint8_t *buf, size_t index, uint8_t r, uint8_t g, uint8_t b)
{
    buf[index * 3 + 0] = g;
    buf[index * 3 + 1] = r;
    buf[index * 3 + 2] = b;
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_ERROR_CHECK(wifi_init(on_wifi_state_changed, on_wifi_data_received));

    ESP_ERROR_CHECK(ws2812_init(&strip, LED_GPIO));

    while (1) {
        set_pixel_rgb(led_buffer, 0, 50, 0, 0);
        ESP_ERROR_CHECK(ws2812_send(&strip, led_buffer, sizeof(led_buffer)));
        vTaskDelay(pdMS_TO_TICKS(500));
        set_pixel_rgb(led_buffer, 0, 0, 50, 0);
        ESP_ERROR_CHECK(ws2812_send(&strip, led_buffer, sizeof(led_buffer)));
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}