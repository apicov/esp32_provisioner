/**
 * Phone Bridge Component
 *
 * Provides a BLE GATT service that forwards mesh sensor data to smartphone apps.
 * Allows phones to receive data from mesh nodes without implementing mesh protocol.
 *
 * Architecture:
 * Phone (GATT Client) <-> ESP32 Gateway (GATT Server + Mesh Proxy) <-> Mesh Nodes
 */

#ifndef PHONE_BRIDGE_H
#define PHONE_BRIDGE_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Sensor data structure sent to phone
 */
typedef struct {
    uint32_t timestamp;       // Unix timestamp (ms)

    // M5Stick IMU data
    int32_t m5_accel_x;      // mg (milli-g)
    int32_t m5_accel_y;
    int32_t m5_accel_z;
    int32_t m5_gyro_x;       // mdps (milli degrees/sec)
    int32_t m5_gyro_y;
    int32_t m5_gyro_z;

    // Heart rate
    uint8_t heart_rate;       // bpm

    uint8_t data_valid;       // 0=invalid, 1=valid
} __attribute__((packed)) sensor_data_packet_t;

/**
 * Initialize phone bridge GATT service
 *
 * Creates BLE GATT service with characteristic for sensor data notifications.
 * Must be called after BLE stack is initialized.
 *
 * @return ESP_OK on success
 */
esp_err_t phone_bridge_init(void);

/**
 * Update sensor data from mesh and notify phone
 *
 * Call this when new sensor data arrives from mesh network.
 * Automatically sends notification to connected phone if subscribed.
 *
 * @param data Sensor data packet to send
 * @return ESP_OK on success
 */
esp_err_t phone_bridge_update_data(const sensor_data_packet_t *data);

/**
 * Check if phone is connected
 *
 * @return true if phone is connected and subscribed to notifications
 */
bool phone_bridge_is_connected(void);

#ifdef __cplusplus
}
#endif

#endif // PHONE_BRIDGE_H
