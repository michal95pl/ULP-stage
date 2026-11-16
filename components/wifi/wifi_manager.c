#include "wifi_manager.h"

#include <app_utils.h>

#include "esp_wifi.h"
#include "esp_netif.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>

#include "freertos/FreeRTOS.h"
#include <freertos/task.h>
#include "freertos/semphr.h"

#include "esp_system.h"
#include "esp_event.h"
#include <arpa/inet.h>
#include <sys/time.h>
#include <string.h>
#include <sys/param.h>

#define HEADER_SIZE 6
#define RX_BUFFER_SIZE    1024
#define TX_BUFFER_SIZE    1024

static TaskHandle_t s_tcp_server_task_handle = NULL;
static SemaphoreHandle_t s_tx_mutex = NULL;

static wifi_state_cb_t s_app_callback = NULL;
static wifi_data_cb_t s_data_callback = NULL;

static int s_client_sock = -1;
static uint8_t rx_buffer[RX_BUFFER_SIZE];
static uint8_t tx_buffer[TX_BUFFER_SIZE];

static void tcp_server_task(void *pvParameters);

static void internal_wifi_event_handler(void* arg, esp_event_base_t event_base,
                                        int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    }
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_app_callback) s_app_callback(WIFI_APP_DISCONNECTED);
        if (s_tcp_server_task_handle != NULL) {
            vTaskDelete(s_tcp_server_task_handle);
            s_tcp_server_task_handle = NULL;
        }
    } 
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        if (s_app_callback) s_app_callback(WIFI_APP_CONNECTED);

        if (s_tcp_server_task_handle == NULL) {
            xTaskCreate(
                tcp_server_task,
                "tcp_server",
                4096,
                NULL,
                5,
                &s_tcp_server_task_handle
            );
        }
    }
}

esp_err_t wifi_init(wifi_state_cb_t wifi_state_callback, wifi_data_cb_t wifi_data_rcv_callback)
{
    s_app_callback = wifi_state_callback;
    s_data_callback = wifi_data_rcv_callback;

    s_tx_mutex = xSemaphoreCreateMutex();

    CHECK_ERR(esp_netif_init());
    
    if (esp_netif_create_default_wifi_sta() == NULL) return ESP_FAIL;

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    CHECK_ERR(esp_wifi_init(&cfg));

    CHECK_ERR(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, 
                                                  &internal_wifi_event_handler, NULL, NULL));
    CHECK_ERR(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, 
                                                  &internal_wifi_event_handler, NULL, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = "Kinga",
            .password = "Krajzega2462",
        },
    };

    CHECK_ERR(esp_wifi_set_mode(WIFI_MODE_STA));
    CHECK_ERR(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    
    CHECK_ERR(esp_wifi_start());

    return ESP_OK;
}


static bool recv_exact_timeout(int sock, uint8_t *buf, size_t target_len, int32_t timeout_ms)
{
    size_t total_received = 0;

    while (total_received < target_len) {
        if (timeout_ms >= 0) {
            fd_set read_fds;
            FD_ZERO(&read_fds);
            FD_SET(sock, &read_fds);

            struct timeval tv;
            tv.tv_sec = timeout_ms / 1000;
            tv.tv_usec = (timeout_ms % 1000) * 1000;

            int sel = select(sock + 1, &read_fds, NULL, NULL, &tv);
            if (sel <= 0) {
                return false;
            }
        }

        int r = recv(sock, buf + total_received, target_len - total_received, 0);
        if (r <= 0) {
            return false;
        }
        total_received += r;
    }
    return true;
}

static bool recv_exact(int sock, uint8_t *buf, size_t target_len)
{
    return recv_exact_timeout(sock, buf, target_len, -1);
}

static void close_client_session()
{
    if (s_client_sock >= 0) {
        close(s_client_sock);
        s_client_sock = -1;
        if (s_app_callback) {
            s_app_callback(WIFI_TCP_CLIENT_DISCONNECTED);
        }
    }
}

static void tcp_server_task(void *pvParameters)
{
    struct sockaddr_in dest_addr;
    dest_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(1234);

    int listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (listen_sock < 0) {
        vTaskDelete(NULL);
        return;
    }

    bind(listen_sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    listen(listen_sock, 1);

    while (1) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(listen_sock, &read_fds);
        int max_fd = listen_sock;

        if (s_client_sock >= 0) {
            FD_SET(s_client_sock, &read_fds);
            if (s_client_sock > max_fd) {
                max_fd = s_client_sock;
            }
        }

        if (select(max_fd + 1, &read_fds, NULL, NULL, NULL) < 0) {
            vTaskDelay(10 / portTICK_PERIOD_MS);
            continue;
        }

        // second client connecting
        if (FD_ISSET(listen_sock, &read_fds)) {
            struct sockaddr_in source_addr;
            socklen_t addr_len = sizeof(source_addr);
            int new_sock = accept(listen_sock, (struct sockaddr *)&source_addr, &addr_len);
            
            if (new_sock >= 0) {
                if (s_client_sock >= 0) {
                    close(new_sock);
                } else {
                    s_client_sock = new_sock;
                }
            }
        }

        if (s_client_sock >= 0 && FD_ISSET(s_client_sock, &read_fds)) {
            if (!recv_exact(s_client_sock, rx_buffer, HEADER_SIZE)) {
                close_client_session();
                continue;
            }

            uint16_t net_len;
            memcpy(&net_len, &rx_buffer[4], sizeof(uint16_t));
            uint16_t payload_len = ntohs(net_len);
            
            size_t total_frame_len = HEADER_SIZE + payload_len;
            if (total_frame_len > sizeof(rx_buffer)) {
                close_client_session();
                continue;
            }

            if (payload_len > 0) {
                if (!recv_exact_timeout(s_client_sock, rx_buffer + HEADER_SIZE, payload_len, 5000)) {
                    close_client_session();
                    continue;
                }
            }

            if (s_data_callback) {
                s_data_callback(rx_buffer, total_frame_len);
            }

            wifi_tcp_send("WAC", NULL, 0);
        }
    }
    
    close(listen_sock);
    vTaskDelete(NULL);
}

static bool send_exact(int sock, const uint8_t *buf, size_t len)
{
    size_t total_sent = 0;
    while (total_sent < len) {
        int sent = send(sock, buf + total_sent, len - total_sent, 0);
        if (sent <= 0) {
            return false;
        }
        total_sent += sent;
    }
    return true;
}

esp_err_t wifi_tcp_send(const char cmd[3], const uint8_t *payload, uint16_t payload_len)
{
    if (s_tx_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    size_t total_len = HEADER_SIZE + payload_len;
    if (total_len > TX_BUFFER_SIZE) {
        return ESP_ERR_INVALID_SIZE;
    }

    if (xSemaphoreTake(s_tx_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t result = ESP_OK;

    int sock = s_client_sock;
    if (sock < 0) {
        result = ESP_ERR_INVALID_STATE;
        goto cleanup;
    }

    tx_buffer[0] = (uint8_t)cmd[0];
    tx_buffer[1] = (uint8_t)cmd[1];
    tx_buffer[2] = (uint8_t)cmd[2];
    tx_buffer[3] = 0x00; // Reserved

    uint16_t net_len = htons(payload_len);
    memcpy(&tx_buffer[4], &net_len, sizeof(uint16_t));

    if (payload != NULL && payload_len > 0) {
        memcpy(&tx_buffer[HEADER_SIZE], payload, payload_len);
    }

    if (!send_exact(sock, tx_buffer, total_len)) {
        result = ESP_FAIL;
    }

cleanup:
    xSemaphoreGive(s_tx_mutex);
    return result;
}