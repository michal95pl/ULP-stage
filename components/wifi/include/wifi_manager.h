#pragma once

#include "esp_err.h"

typedef enum {
    WIFI_APP_CONNECTED,
    WIFI_APP_DISCONNECTED,
    WIFI_TCP_CLIENT_CONNECTED,
    WIFI_TCP_CLIENT_DISCONNECTED
} wifi_app_status_t;

typedef void (*wifi_state_cb_t)(wifi_app_status_t status);
typedef void (*wifi_data_cb_t)(uint8_t buffer[], uint16_t length);

/**
 * @brief Initializes the Wi-Fi interface and starts the TCP server
 *
 * @param[in] wifi_state_callback    Callback invoked on connection state changes.
 * @param[in] wifi_data_rcv_callback Callback invoked when data frame is received.
 *
 * @return
 *      - ESP_OK: Initialization succeeded.
 *      - ESP_FAIL / other esp_err_t codes: Network stack or Wi-Fi initialization error.
 */
esp_err_t wifi_init(wifi_state_cb_t wifi_state_callback, wifi_data_cb_t wifi_data_rcv_callback);


/**
 * @brief Builds a protocol frame and sends it to the currently connected TCP client.
 *
 * This function is thread-safe (protected by a mutex).
 * @param[in] cmd         3-character command code (e.g., "ACK", "DAT")
 * @param[in] payload     Pointer to the payload data to send (may be NULL if payload_len == 0).
 * @param[in] payload_len Length of the payload in bytes.
 *
 * @return
 *      - ESP_OK: The entire frame was successfully queued for transmission.
 *      - ESP_ERR_INVALID_STATE: No TCP client is currently connected or the module is not initialized.
 *      - ESP_ERR_INVALID_SIZE: Total frame size (header + payload) exceeds the TX buffer capacity.
 *      - ESP_ERR_TIMEOUT: Timed out waiting to acquire the transmission mutex.
 *      - ESP_FAIL: Socket send error (e.g., peer closed the connection during transmission).
 */
esp_err_t wifi_tcp_send(const char cmd[3], const uint8_t *payload, uint16_t payload_len);