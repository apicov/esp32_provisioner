/**
 * ===========================================================================
 *                    MESH-MQTT BRIDGE IMPLEMENTATION
 * ===========================================================================
 *
 * This component bridges BLE Mesh vendor messages to MQTT topics.
 * It overrides the weak function from the provisioner component.
 */

#include "mesh_mqtt_bridge.h"
#include "wifi_mqtt.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>
#include <stdio.h>
#include <inttypes.h>

static const char *TAG = "MESH_MQTT_BRIDGE";

// Bridge configuration
static bridge_config_t g_bridge_config;
static bool g_bridge_initialized = false;

// Vendor opcodes (must match node definitions)
#define VENDOR_OP_IMU_DATA  0xC00001

// Sensor property IDs (from BLE Mesh spec)
#define SENSOR_PROPERTY_HEART_RATE  0x2A37

/**
 * ===========================================================================
 *                           MESSAGE HANDLERS
 * ===========================================================================
 * Each handler processes a specific vendor opcode and publishes to MQTT
 */

/**
 * Handle heart rate sensor messages
 */
static void handle_heartrate_message(uint16_t src_addr, int32_t heart_rate)
{
    // Get current timestamp in milliseconds
    uint32_t timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000);

    // Format JSON payload with timestamp
    char payload[128];
    snprintf(payload, sizeof(payload),
             "{\"node\":\"0x%04x\",\"heartrate\":%d,\"timestamp\":%" PRIu32 "}",
             src_addr, (int)heart_rate, timestamp_ms);

    // Publish to MQTT
    char topic[64];
    snprintf(topic, sizeof(topic), "%s/heartrate/0x%04x", g_bridge_config.mqtt_topic_prefix, src_addr);

    ESP_LOGI(TAG, "Publishing HR from 0x%04x: %d bpm to %s", src_addr, (int)heart_rate, topic);
    wifi_mqtt_publish(topic, payload, 0);
}

/**
 * Handle IMU data messages
 */
static void handle_imu_message(uint16_t src_addr, uint8_t *data, uint16_t length)
{
    if (length != 8) {
        ESP_LOGW(TAG, "Invalid IMU message length: %d (expected 8)", length);
        return;
    }

    // Unpack compact IMU data
    typedef struct {
        uint16_t timestamp_ms;
        int8_t accel_x;  // 0.1g units
        int8_t accel_y;
        int8_t accel_z;
        int8_t gyro_x;   // 10 dps units
        int8_t gyro_y;
        int8_t gyro_z;
    } __attribute__((packed)) imu_compact_msg_t;

    imu_compact_msg_t *imu = (imu_compact_msg_t*)data;

    // Decode: multiply back to original units
    float ax = imu->accel_x * 0.1f;  // 0.1g → g
    float ay = imu->accel_y * 0.1f;
    float az = imu->accel_z * 0.1f;
    int gx = imu->gyro_x * 10;       // 10dps → dps
    int gy = imu->gyro_y * 10;
    int gz = imu->gyro_z * 10;

    // Format JSON payload
    char payload[256];
    snprintf(payload, sizeof(payload),
             "{\"node\":\"0x%04x\",\"time\":%u,"
             "\"accel\":{\"x\":%.1f,\"y\":%.1f,\"z\":%.1f},"
             "\"gyro\":{\"x\":%d,\"y\":%d,\"z\":%d}}",
             src_addr, imu->timestamp_ms,
             ax, ay, az,
             gx, gy, gz);

    // Publish to MQTT
    char topic[64];
    snprintf(topic, sizeof(topic), "%s/imu/0x%04x", g_bridge_config.mqtt_topic_prefix, src_addr);

    ESP_LOGI(TAG, "Publishing IMU from 0x%04x to %s", src_addr, topic);
    wifi_mqtt_publish(topic, payload, 0);
}

/**
 * ===========================================================================
 *                           MESSAGE ROUTING TABLE
 * ===========================================================================
 * This table maps vendor opcodes to handler functions.
 * To add new message types, just add a new entry here!
 */

typedef void (*message_handler_t)(uint16_t src_addr, uint8_t *data, uint16_t length);

typedef struct {
    uint32_t opcode;
    message_handler_t handler;
    const char *name;
} message_route_t;

static const message_route_t message_router[] = {
    { VENDOR_OP_IMU_DATA, handle_imu_message, "IMU Data" },
    // Add new message types here:
    // { VENDOR_OP_SENSOR_DATA, handle_sensor_message, "Sensor Data" },
    // { VENDOR_OP_STATUS, handle_status_message, "Status" },
};

#define ROUTER_SIZE (sizeof(message_router) / sizeof(message_route_t))

/**
 * ===========================================================================
 *                      SENSOR MESSAGE HANDLER (OVERRIDE)
 * ===========================================================================
 *
 * This function OVERRIDES the weak function in ble_mesh_callbacks.c
 * It gets called for every sensor message received from the mesh network.
 */

void provisioner_sensor_msg_handler(uint16_t src_addr, uint16_t property_id, int32_t value)
{
    if (!g_bridge_initialized) {
        return;  // Bridge not initialized, ignore messages
    }

    ESP_LOGD(TAG, "Received sensor message: property=0x%04x, src=0x%04x, value=%d",
             property_id, src_addr, (int)value);

    // Route sensor messages based on property ID
    if (property_id == SENSOR_PROPERTY_HEART_RATE) {
        handle_heartrate_message(src_addr, value);
    } else {
        ESP_LOGD(TAG, "Unhandled sensor property: 0x%04x", property_id);
    }
}

/**
 * ===========================================================================
 *                      VENDOR MESSAGE HANDLER (OVERRIDE)
 * ===========================================================================
 *
 * This function OVERRIDES the weak function in ble_mesh_callbacks.c
 * It gets called for every vendor message received from the mesh network.
 */

void provisioner_vendor_msg_handler(uint16_t src_addr, uint32_t opcode,
                                    uint8_t *data, uint16_t length)
{
    if (!g_bridge_initialized) {
        return;  // Bridge not initialized, ignore messages
    }

    ESP_LOGD(TAG, "Received vendor message: opcode=0x%06lx, src=0x%04x, len=%d",
             opcode, src_addr, length);

    // Route message to appropriate handler
    for (int i = 0; i < ROUTER_SIZE; i++) {
        if (message_router[i].opcode == opcode) {
            ESP_LOGI(TAG, "Routing %s message from 0x%04x", message_router[i].name, src_addr);
            message_router[i].handler(src_addr, data, length);
            return;
        }
    }

    // Unknown opcode
    ESP_LOGW(TAG, "Unknown vendor opcode: 0x%06lx from 0x%04x", opcode, src_addr);
}

/**
 * ===========================================================================
 *                      MQTT MESSAGE HANDLER (MESH → MQTT)
 * ===========================================================================
 *
 * This function handles MQTT messages that should be sent to the mesh network.
 * Currently not implemented (unidirectional: mesh → MQTT only)
 */

static void mqtt_message_received(const char *topic, const char *data, int data_len)
{
    ESP_LOGI(TAG, "MQTT message on topic '%s': %.*s", topic, data_len, data);
    // TODO: Implement MQTT → Mesh routing if needed
}

/**
 * ===========================================================================
 *                         BRIDGE INITIALIZATION
 * ===========================================================================
 */

esp_err_t mesh_mqtt_bridge_init(const bridge_config_t *config)
{
    if (!config) {
        ESP_LOGE(TAG, "Invalid configuration (NULL)");
        return ESP_ERR_INVALID_ARG;
    }

    if (!config->mqtt_topic_prefix) {
        ESP_LOGE(TAG, "MQTT topic prefix is required");
        return ESP_ERR_INVALID_ARG;
    }

    // Store configuration
    memcpy(&g_bridge_config, config, sizeof(bridge_config_t));

    ESP_LOGI(TAG, "Initializing Mesh-MQTT Bridge");
    ESP_LOGI(TAG, "  MQTT topic prefix: %s", config->mqtt_topic_prefix);
    ESP_LOGI(TAG, "  Mesh net_idx: %d", config->mesh_net_idx);
    ESP_LOGI(TAG, "  Mesh app_idx: %d", config->mesh_app_idx);
    ESP_LOGI(TAG, "  Message routes: %d", ROUTER_SIZE);

    // Subscribe to MQTT control topics (optional, for bi-directional communication)
    // char control_topic[64];
    // snprintf(control_topic, sizeof(control_topic), "%s/control/#", config->mqtt_topic_prefix);
    // wifi_mqtt_subscribe(control_topic, 0);

    g_bridge_initialized = true;
    ESP_LOGI(TAG, "✓ Mesh-MQTT Bridge initialized successfully");
    ESP_LOGI(TAG, "  Vendor messages will be forwarded to MQTT");

    return ESP_OK;
}
