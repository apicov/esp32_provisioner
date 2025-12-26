/*
 * ============================================================================
 *                      WiFi-MQTT COMPONENT - PUBLIC API
 * ============================================================================
 *
 * WHAT IS THIS COMPONENT?
 * =======================
 * A reusable ESP-IDF component that provides simple WiFi + MQTT functionality
 * with automatic reconnection, clean callback interface, and easy configuration.
 *
 * FEATURES:
 * =========
 * - Automatic WiFi connection with auto-reconnect
 * - MQTT client with QoS support
 * - Subscribe/Publish interface
 * - Event callbacks (connected, disconnected, message received)
 * - Thread-safe message publishing
 * - Connection state monitoring
 *
 * TYPICAL USAGE:
 * ==============
 * 1. Configure WiFi and MQTT credentials
 * 2. Set up callbacks for events
 * 3. Call wifi_mqtt_init()
 * 4. Call wifi_mqtt_start()
 * 5. Use wifi_mqtt_publish() and wifi_mqtt_subscribe()
 *
 * EXAMPLE:
 * ========
 * ```c
 * // Define callbacks
 * void on_connected(void) {
 *     ESP_LOGI(TAG, "Connected to MQTT broker");
 *     wifi_mqtt_subscribe("sensor/temperature");
 * }
 *
 * void on_message(const char *topic, const char *data, int data_len) {
 *     ESP_LOGI(TAG, "Message: %s = %.*s", topic, data_len, data);
 * }
 *
 * // Configure
 * wifi_mqtt_config_t config = {
 *     .wifi_ssid = "MyWiFi",
 *     .wifi_password = "password",
 *     .mqtt_broker_uri = "mqtt://broker.example.com",
 *     .callbacks = {
 *         .connected = on_connected,
 *         .message_received = on_message,
 *     }
 * };
 *
 * // Initialize and start
 * wifi_mqtt_init(&config);
 * wifi_mqtt_start();
 *
 * // Publish data
 * wifi_mqtt_publish("sensor/data", "{\"temp\":25.5}", 0);
 * ```
 *
 * USE CASES:
 * ==========
 * 1. Simple IoT sensor nodes (publish sensor data to cloud)
 * 2. BLE Mesh → MQTT bridge (forward mesh data to MQTT broker)
 * 3. Remote device control (subscribe to commands from cloud)
 * 4. Data logging and monitoring
 */

#ifndef WIFI_MQTT_H
#define WIFI_MQTT_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ============================================================================
 *                              CALLBACKS
 * ============================================================================
 */

/**
 * Called when WiFi is connected
 * This fires BEFORE MQTT connection
 */
typedef void (*wifi_connected_cb_t)(void);

/**
 * Called when WiFi is disconnected
 * MQTT will also disconnect when WiFi disconnects
 */
typedef void (*wifi_disconnected_cb_t)(void);

/**
 * Called when MQTT broker connection is established
 * This is the right time to subscribe to topics
 */
typedef void (*mqtt_connected_cb_t)(void);

/**
 * Called when MQTT broker connection is lost
 * Auto-reconnect will be attempted automatically
 */
typedef void (*mqtt_disconnected_cb_t)(void);

/**
 * Called when a subscribed message is received
 *
 * @param topic The MQTT topic the message was published to
 * @param data  Pointer to message payload (may NOT be null-terminated!)
 * @param data_len Length of the message payload in bytes
 *
 * IMPORTANT: The data pointer is only valid during this callback.
 * If you need to store the message, make a copy!
 *
 * Example:
 * ```c
 * void on_message(const char *topic, const char *data, int data_len) {
 *     char msg[256];
 *     snprintf(msg, sizeof(msg), "%.*s", data_len, data);
 *     ESP_LOGI(TAG, "%s: %s", topic, msg);
 * }
 * ```
 */
typedef void (*mqtt_message_cb_t)(const char *topic, const char *data, int data_len);

/**
 * Called when a published message is acknowledged by the broker (QoS 1/2 only)
 *
 * @param msg_id Message ID returned from wifi_mqtt_publish()
 */
typedef void (*mqtt_published_cb_t)(int msg_id);

/**
 * Callback structure - all callbacks are optional (can be NULL)
 */
typedef struct {
    wifi_connected_cb_t wifi_connected;         /*!< WiFi connected */
    wifi_disconnected_cb_t wifi_disconnected;   /*!< WiFi disconnected */
    mqtt_connected_cb_t mqtt_connected;         /*!< MQTT connected */
    mqtt_disconnected_cb_t mqtt_disconnected;   /*!< MQTT disconnected */
    mqtt_message_cb_t message_received;         /*!< MQTT message received */
    mqtt_published_cb_t message_published;      /*!< MQTT publish acknowledged */
} wifi_mqtt_callbacks_t;

/*
 * ============================================================================
 *                            CONFIGURATION
 * ============================================================================
 */

/**
 * WiFi-MQTT configuration
 */
typedef struct {
    /*
     * WiFi Configuration
     * ==================
     */
    const char *wifi_ssid;              /*!< WiFi SSID (network name) */
    const char *wifi_password;          /*!< WiFi password */
    uint8_t max_wifi_retry;             /*!< Max WiFi connection retries (0 = infinite, default: 5) */

    /*
     * MQTT Configuration
     * ==================
     */
    const char *mqtt_broker_uri;        /*!< MQTT broker URI (e.g., "mqtt://broker.hivemq.com") */
    const char *mqtt_username;          /*!< MQTT username (NULL if no auth) */
    const char *mqtt_password;          /*!< MQTT password (NULL if no auth) */
    const char *mqtt_client_id;         /*!< MQTT client ID (NULL for auto-generated) */

    uint16_t mqtt_port;                 /*!< MQTT broker port (0 = default: 1883 for mqtt://, 8883 for mqtts://) */
    uint16_t mqtt_keepalive;            /*!< MQTT keepalive interval in seconds (default: 120) */

    /*
     * Optional Features
     * =================
     */
    bool auto_reconnect;                /*!< Auto-reconnect on disconnect (default: true) */
    uint32_t reconnect_timeout_ms;      /*!< Reconnect timeout in ms (default: 10000) */

    /*
     * Callbacks
     * =========
     */
    wifi_mqtt_callbacks_t callbacks;    /*!< Event callbacks (all optional) */
} wifi_mqtt_config_t;

/*
 * ============================================================================
 *                          INITIALIZATION & CONTROL
 * ============================================================================
 */

/**
 * Initialize WiFi-MQTT component
 *
 * WHAT THIS DOES:
 * 1. Initializes NVS (for WiFi credentials storage)
 * 2. Initializes TCP/IP stack (netif)
 * 3. Initializes WiFi driver
 * 4. Initializes MQTT client
 * 5. Registers event callbacks
 *
 * NOTE: This does NOT connect to WiFi or MQTT yet.
 * Call wifi_mqtt_start() to begin connection.
 *
 * @param config Configuration structure
 * @return ESP_OK on success, error code otherwise
 *
 * EXAMPLE:
 * ```c
 * wifi_mqtt_config_t config = {
 *     .wifi_ssid = "MyWiFi",
 *     .wifi_password = "password123",
 *     .mqtt_broker_uri = "mqtt://broker.hivemq.com",
 *     .callbacks.mqtt_connected = on_mqtt_connected,
 *     .callbacks.message_received = on_message,
 * };
 * ESP_ERROR_CHECK(wifi_mqtt_init(&config));
 * ```
 */
esp_err_t wifi_mqtt_init(const wifi_mqtt_config_t *config);

/**
 * Start WiFi and MQTT connections
 *
 * WHAT HAPPENS:
 * 1. WiFi connection initiated
 * 2. Once WiFi connected → wifi_connected callback fires
 * 3. MQTT connection initiated
 * 4. Once MQTT connected → mqtt_connected callback fires
 * 5. Auto-reconnect enabled (if configured)
 *
 * @return ESP_OK on success, error code otherwise
 *
 * NOTE: This function returns immediately (non-blocking).
 * Use callbacks to know when connections are established.
 */
esp_err_t wifi_mqtt_start(void);

/**
 * Stop WiFi and MQTT connections
 *
 * WHAT HAPPENS:
 * 1. MQTT client disconnects gracefully
 * 2. WiFi disconnects
 * 3. Auto-reconnect disabled
 *
 * @return ESP_OK on success, error code otherwise
 *
 * USE CASE: Going into deep sleep, shutting down
 */
esp_err_t wifi_mqtt_stop(void);

/**
 * Check if WiFi is connected
 *
 * @return true if WiFi is connected, false otherwise
 */
bool wifi_mqtt_is_wifi_connected(void);

/**
 * Check if MQTT is connected
 *
 * @return true if MQTT broker is connected, false otherwise
 */
bool wifi_mqtt_is_mqtt_connected(void);

/*
 * ============================================================================
 *                         MQTT PUBLISH & SUBSCRIBE
 * ============================================================================
 */

/**
 * Publish MQTT message
 *
 * THREAD-SAFE: Can be called from any task
 *
 * @param topic MQTT topic to publish to (e.g., "sensor/temperature")
 * @param data  Message payload (can be binary data)
 * @param qos   Quality of Service (0, 1, or 2)
 *              - QoS 0: At most once (fire and forget)
 *              - QoS 1: At least once (acknowledged)
 *              - QoS 2: Exactly once (assured delivery)
 * @return Message ID (>0) on success, -1 on error
 *
 * NOTES:
 * - For QoS 0: Message ID is always -1
 * - For QoS 1/2: Message ID can be used to track acknowledgment
 * - Max message size depends on MQTT broker (typically 256 KB)
 *
 * EXAMPLES:
 * ```c
 * // Simple publish (QoS 0)
 * wifi_mqtt_publish("sensor/temp", "25.5", 0);
 *
 * // JSON payload
 * char json[128];
 * snprintf(json, sizeof(json), "{\"temp\":%.1f,\"hum\":%.1f}", temp, hum);
 * wifi_mqtt_publish("sensor/data", json, 1);
 *
 * // Binary data
 * uint8_t binary[8] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
 * wifi_mqtt_publish_binary("sensor/imu", binary, sizeof(binary), 0);
 * ```
 */
int wifi_mqtt_publish(const char *topic, const char *data, int qos);

/**
 * Publish binary MQTT message
 *
 * Same as wifi_mqtt_publish() but for binary data
 *
 * @param topic MQTT topic
 * @param data  Binary data buffer
 * @param len   Length of data in bytes
 * @param qos   Quality of Service (0, 1, or 2)
 * @return Message ID (>0) on success, -1 on error
 */
int wifi_mqtt_publish_binary(const char *topic, const void *data, int len, int qos);

/**
 * Subscribe to MQTT topic
 *
 * When a message is received on this topic, the message_received callback fires.
 *
 * @param topic MQTT topic pattern (e.g., "sensor/#" for wildcard)
 * @param qos   Quality of Service to request (0, 1, or 2)
 * @return Message ID (>0) on success, -1 on error
 *
 * TOPIC WILDCARDS:
 * - "#" matches multiple levels (e.g., "sensor/#" matches "sensor/temp", "sensor/room/temp")
 * - "+" matches single level (e.g., "sensor/+/temp" matches "sensor/room1/temp")
 *
 * EXAMPLES:
 * ```c
 * // Subscribe to specific topic
 * wifi_mqtt_subscribe("command/led", 0);
 *
 * // Subscribe to all sensors
 * wifi_mqtt_subscribe("sensor/#", 0);
 *
 * // Subscribe to all rooms' temperature
 * wifi_mqtt_subscribe("home/+/temperature", 1);
 * ```
 */
int wifi_mqtt_subscribe(const char *topic, int qos);

/**
 * Unsubscribe from MQTT topic
 *
 * @param topic MQTT topic to unsubscribe from
 * @return Message ID (>0) on success, -1 on error
 */
int wifi_mqtt_unsubscribe(const char *topic);

/*
 * ============================================================================
 *                            UTILITY FUNCTIONS
 * ============================================================================
 */

/**
 * Get WiFi IP address
 *
 * @param ip_str Buffer to store IP address string (min 16 bytes)
 * @param len    Length of buffer
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if not connected
 *
 * EXAMPLE:
 * ```c
 * char ip[16];
 * if (wifi_mqtt_get_ip_address(ip, sizeof(ip)) == ESP_OK) {
 *     ESP_LOGI(TAG, "IP: %s", ip);
 * }
 * ```
 */
esp_err_t wifi_mqtt_get_ip_address(char *ip_str, size_t len);

/**
 * Get WiFi RSSI (signal strength)
 *
 * @return RSSI in dBm (typically -30 to -90)
 *         Returns 0 if WiFi not connected
 *
 * INTERPRETATION:
 * - RSSI > -50 dBm: Excellent
 * - RSSI -50 to -60: Good
 * - RSSI -60 to -70: Fair
 * - RSSI < -70: Weak
 */
int8_t wifi_mqtt_get_rssi(void);

#ifdef __cplusplus
}
#endif

#endif // WIFI_MQTT_H
