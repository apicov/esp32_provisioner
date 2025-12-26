/**
 * ===========================================================================
 *                         MESH-MQTT BRIDGE COMPONENT
 * ===========================================================================
 *
 * This component is the GLUE LAYER between BLE Mesh and MQTT.
 * It connects two independent components:
 * - ble_mesh_provisioner (handles BLE Mesh network)
 * - wifi_mqtt (handles WiFi and MQTT)
 *
 * ARCHITECTURE:
 * =============
 *
 *   ┌──────────────────┐
 *   │ BLE Mesh Network │
 *   └────────┬─────────┘
 *            │ (vendor messages)
 *            ↓
 *   ┌──────────────────┐
 *   │ Mesh-MQTT Bridge │ ← This component
 *   │  (glue layer)    │
 *   └────────┬─────────┘
 *            │ (MQTT pub/sub)
 *            ↓
 *   ┌──────────────────┐
 *   │   WiFi + MQTT    │
 *   └──────────────────┘
 *
 * WHY THIS DESIGN?
 * ================
 * - wifi_mqtt component remains agnostic to BLE Mesh
 * - ble_mesh_provisioner remains agnostic to MQTT
 * - Bridge can be used or not used (optional)
 * - Easy to test components independently
 * - Clean separation of concerns
 *
 * WHAT THIS COMPONENT DOES:
 * =========================
 * 1. Hooks into BLE Mesh vendor message events
 * 2. Routes mesh messages to appropriate MQTT topics
 * 3. Subscribes to MQTT topics and routes to mesh
 * 4. Provides extensible message routing table
 *
 * BASIC USAGE:
 * ============
 * ```c
 * // 1. Initialize mesh (as normal)
 * provisioner_init(&provisioner_config, NULL);
 * provisioner_start();
 *
 * // 2. Initialize WiFi-MQTT (as normal)
 * wifi_mqtt_init(&mqtt_config);
 * wifi_mqtt_start();
 *
 * // 3. Start the bridge (connects them)
 * bridge_config_t bridge_cfg = {
 *     .mqtt_topic_prefix = "mesh",
 *     .mesh_net_idx = 0,
 *     .mesh_app_idx = 0,
 * };
 * mesh_mqtt_bridge_init(&bridge_cfg);
 * ```
 *
 * EXTENDING THE BRIDGE:
 * =====================
 * To add new message types, just add to the routing table in the .c file.
 * No need to modify ble_mesh_provisioner or wifi_mqtt components!
 */

#ifndef MESH_MQTT_BRIDGE_H
#define MESH_MQTT_BRIDGE_H

#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Bridge configuration
 */
typedef struct {
    /**
     * MQTT topic prefix for all mesh messages
     * Example: "mesh" → topics like "mesh/imu", "mesh/sensor", etc.
     */
    const char *mqtt_topic_prefix;

    /**
     * BLE Mesh network index (usually 0)
     */
    uint16_t mesh_net_idx;

    /**
     * BLE Mesh application index (usually 0)
     */
    uint16_t mesh_app_idx;
} bridge_config_t;

/**
 * INITIALIZE MESH-MQTT BRIDGE
 * ============================
 *
 * Sets up the glue layer between mesh and MQTT.
 * Call this AFTER both provisioner and wifi_mqtt are initialized.
 *
 * WHAT THIS DOES:
 * ---------------
 * 1. Hooks into BLE Mesh vendor message callbacks
 * 2. Sets up MQTT subscriptions for mesh control
 * 3. Initializes message routing tables
 *
 * @param config Bridge configuration
 * @return ESP_OK on success, error code otherwise
 *
 * IMPORTANT: Must be called AFTER:
 * - provisioner_init() and provisioner_start()
 * - wifi_mqtt_init() and wifi_mqtt_start()
 */
esp_err_t mesh_mqtt_bridge_init(const bridge_config_t *config);

#ifdef __cplusplus
}
#endif

#endif // MESH_MQTT_BRIDGE_H
