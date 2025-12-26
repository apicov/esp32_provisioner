/**
 * MQTT Bridge Component
 *
 * Bridges BLE Mesh sensor data to MQTT for smartphone access
 *
 * Architecture:
 * BLE Mesh Nodes -> ESP32 Gateway -> MQTT Broker <- Phone App
 */

#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * MQTT Configuration
 */
typedef struct {
    const char *broker_uri;        // MQTT broker URI (e.g., "mqtt://192.168.1.100:1883")
    const char *client_id;         // MQTT client ID
    const char *username;          // Optional username (can be NULL)
    const char *password;          // Optional password (can be NULL)
} mqtt_bridge_config_t;

/**
 * Sensor data packet structure (same as phone_bridge)
 */
typedef struct {
    uint32_t timestamp;            // Milliseconds since boot
    int32_t m5_accel_x;           // Accelerometer X (mg)
    int32_t m5_accel_y;           // Accelerometer Y (mg)
    int32_t m5_accel_z;           // Accelerometer Z (mg)
    int32_t m5_gyro_x;            // Gyroscope X (mdps)
    int32_t m5_gyro_y;            // Gyroscope Y (mdps)
    int32_t m5_gyro_z;            // Gyroscope Z (mdps)
    uint8_t heart_rate;           // Heart rate (bpm)
    uint8_t data_valid;           // Data validity flag
} __attribute__((packed)) mqtt_sensor_data_t;

/**
 * Initialize MQTT bridge
 *
 * @param config MQTT configuration
 * @return ESP_OK on success
 */
esp_err_t mqtt_bridge_init(const mqtt_bridge_config_t *config);

/**
 * Publish sensor data to MQTT
 *
 * Topic: "esp32/sensor/data"
 * Payload: JSON with all sensor values
 *
 * @param data Sensor data to publish
 * @return ESP_OK on success
 */
esp_err_t mqtt_bridge_publish_data(const mqtt_sensor_data_t *data);

/**
 * Check if MQTT is connected
 *
 * @return true if connected to broker
 */
bool mqtt_bridge_is_connected(void);

#ifdef __cplusplus
}
#endif
