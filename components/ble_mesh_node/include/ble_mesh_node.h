/*
 * BLE MESH NODE COMPONENT
 * =======================
 *
 * This is a reusable component that turns your ESP32 into a BLE Mesh node.
 * A node is a device that:
 *   1. Broadcasts "Unprovisioned Device Beacons" until provisioned
 *   2. Gets provisioned by a provisioner (receives NetKey and unicast address)
 *   3. Gets configured by the provisioner (receives AppKey, model bindings)
 *   4. Responds to mesh commands (e.g., Generic OnOff to control an LED)
 *
 * WHAT THIS COMPONENT PROVIDES:
 * =============================
 * - Simple API to initialize and start a BLE Mesh node
 * - Generic OnOff Server model (for controlling LEDs or other on/off devices)
 * - Configuration Server model (allows provisioner to configure the node)
 * - Automatic handling of provisioning process
 * - Callbacks for application-specific behavior
 *
 * TYPICAL USAGE:
 * ==============
 * 1. Define your node configuration (UUID prefix, device name)
 * 2. Implement callbacks (optional - for LED control, status updates)
 * 3. Call node_init() to initialize the mesh stack
 * 4. Call node_start() to begin broadcasting as unprovisioned device
 * 5. Wait for provisioner to discover and provision your node
 * 6. Respond to OnOff commands from provisioner
 *
 * EXAMPLE:
 * ========
 * ```c
 * // Define callbacks (optional)
 * void onoff_changed(uint8_t onoff) {
 *     gpio_set_level(LED_PIN, onoff);  // Control LED
 * }
 *
 * // Configure the node
 * node_config_t config = {
 *     .device_uuid_prefix = {0xdd, 0xdd},  // Must match provisioner's filter
 *     .callbacks = {
 *         .onoff_changed = onoff_changed
 *     }
 * };
 *
 * // Initialize and start
 * node_init(&config);
 * node_start();
 * ```
 *
 * ADDRESSING IN BLE MESH:
 * =======================
 * Before provisioning: Node has no address, broadcasts beacons
 * After provisioning: Provisioner assigns a unicast address (e.g., 0x0005)
 *
 * MODELS INCLUDED:
 * ================
 * 1. Configuration Server (Mandatory):
 *    - Allows provisioner to configure the node
 *    - Handles AppKey addition, model binding, etc.
 *
 * 2. Generic OnOff Server:
 *    - Provides on/off state (0 = off, 1 = on)
 *    - Responds to Get/Set/SetUnack commands
 *    - Publishes state changes
 *
 * SECURITY:
 * =========
 * - All mesh communication is encrypted
 * - NetKey: Network layer encryption (shared by all nodes in network)
 * - AppKey: Application layer encryption (shared by nodes in same app)
 * - DevKey: Device-specific key (used for node configuration)
 */

#ifndef BLE_MESH_NODE_H
#define BLE_MESH_NODE_H

#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * NODE CALLBACKS
 * ==============
 * Optional callbacks to integrate mesh functionality with your application
 */
typedef struct {
    /**
     * Called when provisioning is complete
     * @param unicast_addr The unicast address assigned to this node
     */
    void (*provisioned)(uint16_t unicast_addr);

    /**
     * Called when Generic OnOff state changes
     * @param onoff New state (0 = OFF, 1 = ON)
     *
     * Use this to control your LED, relay, or other on/off device
     * Example: gpio_set_level(LED_PIN, onoff);
     */
    void (*onoff_changed)(uint8_t onoff);

    /**
     * Called when node is reset (unprovisioned)
     * You should clear any stored state and restart
     */
    void (*reset)(void);
} node_callbacks_t;

/*
 * NODE CONFIGURATION
 * ==================
 * Configuration parameters for initializing the BLE Mesh node
 */
typedef struct {
    /**
     * First 2 bytes of device UUID
     * The provisioner uses this to filter which devices to provision
     * MUST match the provisioner's match_prefix configuration
     *
     * Example: {0xdd, 0xdd} matches provisioner filter
     */
    uint8_t device_uuid_prefix[2];

    /**
     * Optional callbacks for application integration
     * Set to NULL if you don't need callbacks
     */
    node_callbacks_t callbacks;
} node_config_t;

/**
 * INITIALIZE BLE MESH NODE
 * =========================
 *
 * Initializes the Bluetooth stack and BLE Mesh node functionality.
 * Must be called once before node_start().
 *
 * WHAT THIS DOES:
 * 1. Initializes NVS (for storing mesh configuration)
 * 2. Initializes Bluetooth controller and host
 * 3. Initializes BLE Mesh stack with node role
 * 4. Sets up models (Configuration Server, Generic OnOff Server)
 * 5. Registers callbacks
 *
 * @param config Node configuration (UUID prefix, callbacks)
 * @return ESP_OK on success, error code otherwise
 *
 * EXAMPLE:
 * node_config_t config = {
 *     .device_uuid_prefix = {0xdd, 0xdd},
 *     .callbacks = { .onoff_changed = led_control }
 * };
 * esp_err_t err = node_init(&config);
 */
esp_err_t node_init(const node_config_t *config);

/**
 * START BLE MESH NODE
 * ===================
 *
 * Starts broadcasting as an unprovisioned device.
 * The node will send "Unprovisioned Device Beacons" that provisioners
 * can discover.
 *
 * WHAT HAPPENS NEXT:
 * 1. Node broadcasts beacons containing its UUID
 * 2. Provisioner discovers the node (if UUID matches filter)
 * 3. Provisioner initiates provisioning
 * 4. Node receives NetKey and unicast address
 * 5. Provisioner configures node (AppKey, model binding)
 * 6. Node is ready to receive commands
 *
 * @return ESP_OK on success, error code otherwise
 *
 * NOTE: If already provisioned (stored in NVS), the node will rejoin
 * the network with its existing credentials instead of broadcasting beacons.
 */
esp_err_t node_start(void);

/**
 * GET CURRENT ONOFF STATE
 * ========================
 *
 * Returns the current Generic OnOff state of the node.
 *
 * @return Current state (0 = OFF, 1 = ON)
 *
 * USES:
 * - Display current state on screen
 * - Synchronize hardware state with mesh state
 * - Implement toggle functionality
 */
uint8_t node_get_onoff_state(void);

/**
 * SET ONOFF STATE LOCALLY
 * ========================
 *
 * Changes the Generic OnOff state and publishes it to the mesh network.
 * Use this when you want to change state locally (e.g., button press)
 * and notify the provisioner.
 *
 * @param onoff New state (0 = OFF, 1 = ON)
 * @return ESP_OK on success, error code otherwise
 *
 * EXAMPLE USE CASE:
 * User presses button on device → call node_set_onoff_state(1) →
 * LED turns on AND provisioner is notified via mesh publish
 */
esp_err_t node_set_onoff_state(uint8_t onoff);

#ifdef __cplusplus
}
#endif

#endif // BLE_MESH_NODE_H
