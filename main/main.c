#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "wifi_manager.h"
#include "esp_netif.h"
#include "esp_event.h"

void on_wifi_state_changed(wifi_app_status_t status) 
{
    if (status == WIFI_APP_CONNECTED) {
        printf("Connected");
    } else if (status == WIFI_APP_DISCONNECTED) {
        printf("Disconnected");
    }
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

    // ESP_ERROR_CHECK(wifi_init(on_wifi_state_changed));

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}