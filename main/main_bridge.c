/**
 * ===========================================================================
 *                      BLE MESH TO MQTT BRIDGE APPLICATION
 * ===========================================================================
 *
 * This application demonstrates the clean 3-component architecture:
 *
 *   1. ble_mesh_provisioner - Independent BLE Mesh component
 *   2. wifi_mqtt            - Independent WiFi/MQTT component
 *   3. mesh_mqtt_bridge     - Glue layer connecting them
 *
 * ARCHITECTURE BENEFITS:
 * - Each component can be tested independently
 * - wifi_mqtt is agnostic to BLE Mesh
 * - ble_mesh_provisioner is agnostic to MQTT
 * - Bridge can be added/removed without modifying other components
 *
 * WHAT THIS APPLICATION DOES:
 * - Provisions BLE Mesh nodes (M5Stick with IMU)
 * - Connects to WiFi and MQTT broker
 * - Forwards mesh vendor messages (IMU data) to MQTT topics
 */

#include <stdio.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "ble_mesh_provisioner.h"
#include "wifi_mqtt.h"
#include "mesh_mqtt_bridge.h"

#define TAG "MAIN"

// Use menuconfig values (run "idf.py menuconfig" to configure)
#define WIFI_SSID     CONFIG_WIFI_SSID
#define WIFI_PASSWORD CONFIG_WIFI_PASSWORD
#define MQTT_BROKER_URI CONFIG_MQTT_BROKER_URI
#define MQTT_TOPIC_PREFIX CONFIG_MQTT_TOPIC_PREFIX

/**
 * ===========================================================================
 *                              MQTT CALLBACKS
 * ===========================================================================
 */

static void on_mqtt_connected(void)
{
    ESP_LOGI(TAG, "✓ MQTT connected - bridge is operational");
}

static void on_mqtt_disconnected(void)
{
    ESP_LOGW(TAG, "✗ MQTT disconnected - messages will be queued");
}

static void on_mqtt_message(const char *topic, const char *data, int data_len)
{
    ESP_LOGI(TAG, "MQTT message: %s = %.*s", topic, data_len, data);
}

/**
 * ===========================================================================
 *                         PROVISIONER CALLBACKS
 * ===========================================================================
 */

static void on_node_added(const uint8_t uuid[16], uint16_t unicast, uint8_t elem_num)
{
    ESP_LOGI(TAG, "✓ Node provisioned: addr=0x%04x, elements=%d", unicast, elem_num);
    ESP_LOGI(TAG, "  Node is now ready to send IMU data!");
}

/**
 * ===========================================================================
 *                              MAIN APPLICATION
 * ===========================================================================
 */

void app_main(void)
{
    esp_err_t err;

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  BLE Mesh to MQTT Bridge");
    ESP_LOGI(TAG, "========================================");

    // ┌──────────────────────────────────────────────────────────────────┐
    // │ STEP 1: Initialize NVS (required for WiFi and BLE Mesh)         │
    // └──────────────────────────────────────────────────────────────────┘
    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    // ┌──────────────────────────────────────────────────────────────────┐
    // │ STEP 2: Initialize BLE Mesh Provisioner                         │
    // └──────────────────────────────────────────────────────────────────┘
    ESP_LOGI(TAG, "Initializing BLE Mesh Provisioner...");

    provisioner_config_t prov_config = {
        .own_address = 0x0001,
        .node_start_address = 0x0010,
        .match_prefix = {CONFIG_MESH_UUID_PREFIX_0, CONFIG_MESH_UUID_PREFIX_1},  // From menuconfig
        .net_idx = 0,
        .app_idx = 0,
    };

    provisioner_callbacks_t prov_callbacks = {
        .node_added = on_node_added,
    };

    err = provisioner_init(&prov_config, &prov_callbacks);
    ESP_ERROR_CHECK(err);

    err = provisioner_start();
    ESP_ERROR_CHECK(err);

    ESP_LOGI(TAG, "✓ BLE Mesh Provisioner started");

    // ┌──────────────────────────────────────────────────────────────────┐
    // │ STEP 3: Initialize WiFi and MQTT                                │
    // └──────────────────────────────────────────────────────────────────┘
    ESP_LOGI(TAG, "Initializing WiFi and MQTT...");

    wifi_mqtt_config_t mqtt_config = {
        .wifi_ssid = WIFI_SSID,
        .wifi_password = WIFI_PASSWORD,
        .mqtt_broker_uri = MQTT_BROKER_URI,
        .callbacks = {
            .mqtt_connected = on_mqtt_connected,
            .mqtt_disconnected = on_mqtt_disconnected,
            .message_received = on_mqtt_message,
        },
    };

    err = wifi_mqtt_init(&mqtt_config);
    ESP_ERROR_CHECK(err);

    err = wifi_mqtt_start();
    ESP_ERROR_CHECK(err);

    ESP_LOGI(TAG, "✓ WiFi/MQTT initialized (connecting...)");

    // ┌──────────────────────────────────────────────────────────────────┐
    // │ STEP 4: Initialize Mesh-MQTT Bridge (the glue layer!)           │
    // └──────────────────────────────────────────────────────────────────┘
    ESP_LOGI(TAG, "Initializing Mesh-MQTT Bridge...");

    bridge_config_t bridge_config = {
        .mqtt_topic_prefix = MQTT_TOPIC_PREFIX,  // From menuconfig
        .mesh_net_idx = 0,
        .mesh_app_idx = 0,
    };

    err = mesh_mqtt_bridge_init(&bridge_config);
    ESP_ERROR_CHECK(err);

    // ┌──────────────────────────────────────────────────────────────────┐
    // │ ALL DONE! The bridge is now running                             │
    // └──────────────────────────────────────────────────────────────────┘
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  Bridge is running!");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Waiting for:");
    ESP_LOGI(TAG, "  1. Unprovisioned mesh nodes");
    ESP_LOGI(TAG, "  2. Vendor messages from nodes");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "IMU data will be published to:");
    ESP_LOGI(TAG, "  Topic: mesh/imu/0x<node_addr>");
    ESP_LOGI(TAG, "  Format: JSON with accel & gyro data");
    ESP_LOGI(TAG, "========================================");
}
