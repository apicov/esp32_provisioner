#ifndef BLE_MESH_PROVISIONER_H
#define BLE_MESH_PROVISIONER_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ============================================================================
 * BLE MESH PROVISIONER - EDUCATIONAL OVERVIEW
 * ============================================================================
 *
 * WHAT IS BLE MESH?
 * -----------------
 * BLE Mesh is a networking protocol built on top of Bluetooth Low Energy (BLE)
 * that enables many-to-many (m:m) device communications. Unlike traditional
 * BLE which is point-to-point, BLE Mesh creates a mesh network where messages
 * can hop through multiple devices to reach their destination.
 *
 * KEY CONCEPTS:
 * -------------
 *
 * 1. NODES: Devices in the mesh network
 *    - Unprovisioned Node: A device that hasn't joined the network yet
 *    - Provisioned Node: A device that's part of the network
 *
 * 2. PROVISIONER: A special node that brings devices into the network
 *    - Scans for unprovisioned devices
 *    - Assigns network keys and addresses
 *    - Configures nodes with application keys
 *
 * 3. ELEMENTS: Addressable entities within a node
 *    - Each node has 1+ elements (usually 1)
 *    - Each element can have multiple models
 *
 * 4. MODELS: Define functionality (like "Light" or "Switch")
 *    - Server Models: Provide functionality (e.g., Light turns on/off)
 *    - Client Models: Control servers (e.g., Switch controls Light)
 *
 * 5. ADDRESSES:
 *    - Unicast: Unique address for each element (0x0001-0x7FFF)
 *    - Group: Address for multiple elements (0xC000-0xFEFF)
 *    - Virtual: 128-bit UUID-based address
 *
 * 6. KEYS (Security):
 *    - Network Key (NetKey): Encrypts all mesh messages at network layer
 *    - Application Key (AppKey): Encrypts messages at application layer
 *    - Device Key (DevKey): Unique per device, used for configuration
 *
 * 7. PROVISIONING PROCESS:
 *    Step 1: Provisioner scans for unprovisioned devices (via UUID)
 *    Step 2: Provisioner establishes secure channel (PB-ADV or PB-GATT)
 *    Step 3: Provisioner sends network key and assigns unicast address
 *    Step 4: Device becomes a provisioned node
 *    Step 5: Provisioner configures node (sends app keys, binds models)
 *
 * MESSAGE FLOW EXAMPLE (Light Control):
 *    Switch (Client) → "Turn On" message → Network (encrypted with AppKey)
 *    → Messages hop through intermediate nodes → Light (Server) receives
 *    → Light decrypts, processes, and turns on
 *
 * WHY USE BLE MESH?
 * -----------------
 * - Coverage: Messages can reach distant devices via multi-hop routing
 * - Reliability: Multiple paths mean messages still get through if one fails
 * - Scalability: Support for 1000s of devices in a single network
 * - Low Power: Optimized for battery-operated IoT devices
 * - Standardized: Based on Bluetooth SIG specification
 */

/**
 * @brief Provisioner configuration
 *
 * This structure defines the basic parameters for the provisioner's operation
 * within the BLE Mesh network.
 */
typedef struct {
    /**
     * @brief Provisioner's own unicast address (typically 0x0001)
     *
     * EDUCATIONAL NOTE:
     * - Unicast addresses are unique to each element in the mesh
     * - Valid range: 0x0001 to 0x7FFF (0x0000 is unassigned)
     * - The provisioner itself is a node and needs its own address
     * - Convention: Provisioner usually uses 0x0001 (lowest valid address)
     */
    uint16_t own_address;

    /**
     * @brief Starting address for newly provisioned nodes (e.g., 0x0005)
     *
     * EDUCATIONAL NOTE:
     * - Each new node gets assigned sequential addresses starting from this
     * - If a node has 3 elements, it gets 3 sequential addresses
     * - Example: Node with 3 elements starting at 0x0005 gets 0x0005, 0x0006, 0x0007
     * - Keep a gap from own_address to allow for provisioner's own elements
     */
    uint16_t node_start_address;

    /**
     * @brief UUID prefix to match when scanning (e.g., {0xdd, 0xdd})
     *
     * EDUCATIONAL NOTE:
     * - UUID = Universally Unique Identifier (16 bytes)
     * - Each unprovisioned device advertises its UUID
     * - Provisioner can filter devices by UUID prefix
     * - This allows provisioning only specific devices (e.g., same manufacturer)
     * - Full UUID example: [0xdd, 0xdd, MAC_ADDR (6 bytes), zeros (8 bytes)]
     */
    uint8_t  match_prefix[2];

    /**
     * @brief Network Key index (usually 0)
     *
     * EDUCATIONAL NOTE:
     * - NetKey encrypts all messages at the network layer
     * - A mesh can have multiple NetKeys (subnets)
     * - Index 0 is the "primary" network key
     * - All nodes in the same subnet share the same NetKey
     * - Allows network segmentation for security
     */
    uint16_t net_idx;

    /**
     * @brief Application Key index (usually 0)
     *
     * EDUCATIONAL NOTE:
     * - AppKey encrypts messages at application layer
     * - Multiple AppKeys can exist (different apps/functions)
     * - Example: AppKey[0] for lights, AppKey[1] for sensors
     * - Allows fine-grained access control within a network
     * - A node can have multiple AppKeys bound to different models
     */
    uint16_t app_idx;
} provisioner_config_t;

/**
 * @brief Callback when a node is successfully provisioned
 *
 * EDUCATIONAL NOTE - THE PROVISIONING LIFECYCLE:
 * 1. DISCOVERY: Provisioner receives unprovisioned device advertisement
 * 2. INVITATION: Provisioner invites device to join
 * 3. EXCHANGE: Public keys exchanged, authentication happens
 * 4. PROVISIONING: NetKey and unicast address sent to device
 * 5. COMPLETE: This callback fires! Device is now a mesh node
 * 6. CONFIGURATION: Provisioner configures the node (happens after this)
 *
 * @param uuid Device's unique 16-byte identifier
 * @param unicast The unicast address assigned to the node
 * @param elem_num Number of elements the node has
 */
typedef void (*provisioner_node_added_cb_t)(const uint8_t uuid[16], uint16_t unicast, uint8_t elem_num);

/**
 * @brief Provisioner event callbacks structure
 */
typedef struct {
    provisioner_node_added_cb_t node_added;  /*!< Called when a node is successfully provisioned */
} provisioner_callbacks_t;

/**
 * @brief Initialize BLE Mesh Provisioner
 *
 * EDUCATIONAL NOTE - INITIALIZATION SEQUENCE:
 * This function sets up the complete BLE Mesh stack:
 *
 * 1. BLUETOOTH INITIALIZATION:
 *    - Releases Classic Bluetooth memory (we only need BLE)
 *    - Initializes BT controller (HCI layer)
 *    - Enables BLE mode
 *    - Starts Bluedroid stack (host layer)
 *
 * 2. MESH STACK INITIALIZATION:
 *    - Sets up composition data (what models/elements we have)
 *    - Configures provisioning parameters
 *    - Registers event callbacks
 *    - Initializes network layer, transport layer, access layer
 *
 * 3. PROVISIONER-SPECIFIC SETUP:
 *    - Generates unique device UUID based on MAC address
 *    - Sets up UUID matching filter
 *    - Configures network and application keys
 *
 * @param config Provisioner configuration parameters
 * @param callbacks Optional event callbacks (can be NULL)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t provisioner_init(const provisioner_config_t *config, const provisioner_callbacks_t *callbacks);

/**
 * @brief Start provisioning (begin scanning for unprovisioned devices)
 *
 * EDUCATIONAL NOTE - WHAT HAPPENS WHEN YOU START:
 *
 * 1. ENABLE PROVISIONING BEARERS:
 *    - PB-ADV: Provisioning over advertising (uses BLE advertisements)
 *    - PB-GATT: Provisioning over GATT (uses BLE connections)
 *    Both are enabled for maximum compatibility
 *
 * 2. START SCANNING:
 *    - Provisioner begins listening for "Unprovisioned Device Beacons"
 *    - These beacons contain the device's UUID and OOB info
 *    - Only devices matching the UUID prefix are reported
 *
 * 3. AUTOMATIC PROVISIONING:
 *    - When a matching device is found, provisioning starts automatically
 *    - Provisioner establishes secure channel
 *    - Sends network key and assigns address
 *    - Device joins the mesh network
 *
 * 4. CONFIGURATION:
 *    - After provisioning, device is automatically configured
 *    - Composition data is retrieved (what models it has)
 *    - Application keys are added
 *    - Models are bound to application keys
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t provisioner_start(void);

/**
 * @brief Stop provisioning (stop scanning)
 *
 * EDUCATIONAL NOTE:
 * - Disables provisioning bearers (PB-ADV and PB-GATT)
 * - Stops scanning for unprovisioned devices
 * - Existing provisioned nodes remain in the network
 * - Can be restarted later with provisioner_start()
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t provisioner_stop(void);

/**
 * @brief Send Generic OnOff Set command to a node
 *
 * EDUCATIONAL NOTE - MESSAGE FLOW:
 *
 * 1. MESSAGE CREATION:
 *    - Opcode: ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET (0x8202)
 *    - Parameters: onoff state (0=OFF, 1=ON), TID (transaction ID)
 *
 * 2. ADDRESSING:
 *    - Destination: Unicast address of target node
 *    - Source: Our provisioner address
 *
 * 3. ENCRYPTION:
 *    - Message encrypted with Application Key
 *    - Transport layer adds sequence number
 *    - Network layer adds network key encryption
 *
 * 4. TRANSMISSION:
 *    - Message broadcast as BLE advertisement
 *    - Intermediate nodes relay (if needed)
 *    - Target node receives and decrypts
 *
 * 5. RESPONSE:
 *    - Target node sends Generic OnOff Status back
 *    - Contains current state confirmation
 *
 * This demonstrates the CLIENT-SERVER model:
 * - We (client) send SET message
 * - Light node (server) processes and responds
 *
 * @param unicast Target node's unicast address
 * @param onoff true to turn ON, false to turn OFF
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t provisioner_send_onoff(uint16_t unicast, bool onoff);

/**
 * @brief Get number of provisioned nodes
 *
 * EDUCATIONAL NOTE:
 * - Returns count of nodes we've successfully provisioned
 * - Useful for monitoring network growth
 * - Does not include the provisioner itself
 *
 * @return Number of provisioned nodes
 */
uint16_t provisioner_get_node_count(void);

#ifdef __cplusplus
}
#endif

#endif // BLE_MESH_PROVISIONER_H
