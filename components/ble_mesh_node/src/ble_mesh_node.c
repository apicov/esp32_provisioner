/*
 * BLE MESH NODE - MAIN IMPLEMENTATION
 * ====================================
 *
 * This file implements a BLE Mesh node that can be provisioned and controlled.
 * It demonstrates the "server" side of BLE Mesh - waiting to be discovered,
 * provisioned, configured, and then responding to commands.
 *
 * KEY DIFFERENCES FROM PROVISIONER:
 * ==================================
 * PROVISIONER (network admin):          | NODE (network device):
 * - Creates and manages the network     | - Joins an existing network
 * - Assigns addresses to nodes          | - Receives address from provisioner
 * - Sends configuration commands        | - Receives configuration commands
 * - Uses Client models (OnOff Client)   | - Uses Server models (OnOff Server)
 * - Sends Get/Set commands              | - Responds to Get/Set commands
 *
 * NODE LIFECYCLE:
 * ===============
 * 1. UNPROVISIONED STATE:
 *    - Node broadcasts "Unprovisioned Device Beacons"
 *    - Beacon contains UUID for identification
 *    - No encryption, no mesh communication yet
 *
 * 2. PROVISIONING:
 *    - Provisioner discovers node via beacon
 *    - Secure provisioning protocol executed
 *    - Node receives: NetKey, Unicast Address, IV Index
 *    - Node is now part of the mesh network
 *
 * 3. CONFIGURATION:
 *    - Provisioner sends AppKey to node
 *    - Provisioner binds AppKey to models
 *    - Node can now receive application messages
 *
 * 4. OPERATIONAL:
 *    - Node responds to Get/Set commands
 *    - Node publishes state changes
 *    - Full mesh communication enabled
 *
 * MODELS IN THIS NODE:
 * ====================
 * 1. Configuration Server (MANDATORY for all nodes):
 *    - Allows remote configuration
 *    - Handles AppKey add, model binding, etc.
 *
 * 2. Generic OnOff Server:
 *    - Maintains on/off state (0 or 1)
 *    - Responds to Generic OnOff Get (query state)
 *    - Responds to Generic OnOff Set (change state)
 *    - Can publish state changes to subscribed nodes
 */

#include "ble_mesh_node.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_ble_mesh_defs.h"
#include "esp_ble_mesh_common_api.h"
#include "esp_ble_mesh_provisioning_api.h"
#include "esp_ble_mesh_networking_api.h"
#include "esp_ble_mesh_config_model_api.h"
#include "esp_ble_mesh_generic_model_api.h"
#include <string.h>

#define TAG "BLE_MESH_NODE"

/*
 * DEVICE UUID GENERATION
 * ======================
 * The 16-byte UUID uniquely identifies this device before provisioning.
 * Structure: [2-byte prefix][6-byte MAC][8 bytes padding/random]
 *
 * The prefix (e.g., 0xdddd) is used by the provisioner to filter which
 * devices to provision. Only nodes with matching prefix will be provisioned.
 */
static uint8_t dev_uuid[16];

/*
 * NODE STATE
 * ==========
 * Current Generic OnOff state (0 = OFF, 1 = ON)
 * This represents the state of whatever this node controls (LED, relay, etc.)
 */
static uint8_t onoff_state = 0;

/*
 * APPLICATION CALLBACKS
 * =====================
 * Callbacks registered by the application to handle mesh events
 */
static node_callbacks_t app_callbacks = {0};

/*
 * FORWARD DECLARATIONS
 * ====================
 * Callback functions defined later in this file
 */
static void mesh_prov_cb(esp_ble_mesh_prov_cb_event_t event,
                        esp_ble_mesh_prov_cb_param_t *param);
static void mesh_config_server_cb(esp_ble_mesh_cfg_server_cb_event_t event,
                                   esp_ble_mesh_cfg_server_cb_param_t *param);
static void mesh_generic_server_cb(esp_ble_mesh_generic_server_cb_event_t event,
                                    esp_ble_mesh_generic_server_cb_param_t *param);

/*
 * CONFIGURATION SERVER SETTINGS
 * ==============================
 * These settings control how this node behaves in the mesh network:
 *
 * - relay: Whether this node forwards messages for other nodes
 *   DISABLED = don't relay (saves power, simpler)
 *   ENABLED = act as relay (extends network range)
 *
 * - beacon: Whether to broadcast secure network beacons
 *   ENABLED = broadcast beacons (helps other nodes discover network)
 *   DISABLED = silent (saves power)
 *
 * - friend: Whether to help low-power nodes (Friendship feature)
 *   NOT_SUPPORTED = don't act as Friend (simpler, saves power)
 *   SUPPORTED = can befriend Low Power Nodes
 *
 * - gatt_proxy: Whether to provide GATT proxy service
 *   NOT_SUPPORTED = mesh-only communication
 *   SUPPORTED = allows GATT devices to access mesh
 *
 * - default_ttl: Default Time-To-Live for messages (max hops)
 *   7 = messages can hop through up to 7 relay nodes
 *
 * - net_transmit: How to transmit network layer messages
 *   ESP_BLE_MESH_TRANSMIT(count, interval_steps)
 *   count = 2: Send each message 3 times (original + 2 retransmits)
 *   interval = 20ms: Wait 20ms between transmissions
 */
static esp_ble_mesh_cfg_srv_t config_server = {
    .relay = ESP_BLE_MESH_RELAY_DISABLED,           // Don't relay messages
    .beacon = ESP_BLE_MESH_BEACON_ENABLED,          // Broadcast beacons
    .friend_state = ESP_BLE_MESH_FRIEND_NOT_SUPPORTED,  // Not a Friend node
    .gatt_proxy = ESP_BLE_MESH_GATT_PROXY_NOT_SUPPORTED, // No GATT proxy
    .default_ttl = 7,                                // Max 7 hops
    .net_transmit = ESP_BLE_MESH_TRANSMIT(2, 20),   // 3 transmissions, 20ms apart
    .relay_retransmit = ESP_BLE_MESH_TRANSMIT(2, 20),
};

/*
 * GENERIC ONOFF SERVER STATE
 * ===========================
 * This structure holds the state for the Generic OnOff Server model.
 * The model spec requires tracking both current and target state for
 * transitions (gradual changes from one state to another).
 *
 * For a simple on/off device, current and target are usually the same.
 */
static esp_ble_mesh_gen_onoff_srv_t onoff_server = {
    .rsp_ctrl = {
        .get_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,  // Automatically respond to Get
        .set_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,  // Automatically respond to Set
    },
    .state = {
        .onoff = 0,          // Current state (0 = OFF)
        .target_onoff = 0,   // Target state (for transitions)
    },
};

/*
 * MODEL DEFINITIONS
 * =================
 * Models define what functionality this node provides.
 * Each model is either a Server (responds to commands) or Client (sends commands).
 *
 * This node has:
 * 1. Configuration Server - Mandatory for all nodes, handles configuration
 * 2. Generic OnOff Server - Provides on/off functionality
 *
 * MODEL MACROS EXPLAINED:
 * - ESP_BLE_MESH_MODEL_CFG_SRV: Configuration Server model
 *   Parameter: pointer to config_server struct
 *
 * - ESP_BLE_MESH_MODEL_GEN_ONOFF_SRV: Generic OnOff Server model
 *   Parameter 1: publication context (NULL = no periodic publishing)
 *   Parameter 2: pointer to onoff_server struct
 */
static esp_ble_mesh_model_t root_models[] = {
    ESP_BLE_MESH_MODEL_CFG_SRV(&config_server),
    ESP_BLE_MESH_MODEL_GEN_ONOFF_SRV(NULL, &onoff_server),
};

/*
 * ELEMENT DEFINITION
 * ==================
 * An element is an addressable entity within a node.
 * Simple devices have 1 element, complex devices can have multiple
 * (e.g., a dual-switch has 2 elements, one per switch).
 *
 * Each element has:
 * - Location: Physical location descriptor (0x0000 = unknown/not used)
 * - Models: Array of models this element supports
 * - Model count: Number of models in the array
 *
 * ELEMENT vs NODE:
 * - A node is the physical device (e.g., ESP32 board)
 * - An element is an addressable part of that device
 * - Each element gets its own unicast address
 * - For a node with 1 element: node address = element address
 */
static esp_ble_mesh_elem_t elements[] = {
    {
        .location = 0x0000,                             // No specific location
        .sig_model_count = ARRAY_SIZE(root_models),     // Number of SIG models
        .sig_models = root_models,                      // SIG models array
        .vnd_model_count = 0,                           // No vendor models
        .vnd_models = NULL,                             // No vendor models
    },
};

/*
 * COMPOSITION DATA
 * ================
 * Composition data describes the node's capabilities. The provisioner
 * requests this after provisioning to understand what the node can do.
 *
 * Contains:
 * - cid: Company ID (0xFFFF = not assigned, for prototypes/testing)
 * - pid: Product ID (identifies product type, 0 = not specified)
 * - vid: Version ID (firmware version, 0 = not specified)
 * - elements: Array of elements this node has
 * - element_count: Number of elements (usually 1 for simple devices)
 *
 * WHY THIS MATTERS:
 * The provisioner uses composition data to know:
 * - How many addresses to allocate (one per element)
 * - Which models to configure (Generic OnOff Server in this case)
 * - What configuration is needed (AppKey binding, etc.)
 */
static esp_ble_mesh_comp_t composition = {
    .cid = 0xFFFF,                          // Company ID: unassigned (test/prototype)
    .pid = 0x0000,                          // Product ID: not specified
    .vid = 0x0000,                          // Version ID: not specified
    .elements = elements,                    // Elements array
    .element_count = ARRAY_SIZE(elements),   // Number of elements (1)
};

/*
 * PROVISIONING DATA
 * =================
 * Configuration for the provisioning process.
 *
 * For a node (not provisioner), we use the same structure as the provisioner
 * but with node-specific values. This struct supports both node and provisioner roles.
 *
 * The struct is not initialized statically because we need to set dev_uuid first.
 * We'll initialize it dynamically in node_init() using the same pattern as the provisioner.
 */
static esp_ble_mesh_prov_t provision;

/*
 * GENERATE DEVICE UUID
 * ====================
 *
 * Creates a unique 16-byte UUID for this device.
 *
 * UUID STRUCTURE:
 * Bytes 0-1:   User-defined prefix (e.g., 0xdddd)
 *              Used by provisioner to filter devices
 * Bytes 2-7:   ESP32 Bluetooth MAC address (unique per device)
 * Bytes 8-15:  Padding (zeros)
 *
 * WHY THIS STRUCTURE:
 * - Prefix allows selective provisioning (only provision devices with matching prefix)
 * - MAC ensures uniqueness (no two ESP32s have same MAC)
 * - Simple and deterministic (same device always has same UUID)
 *
 * PRODUCTION NOTE:
 * For production, consider adding:
 * - Product ID in bytes 8-9
 * - Firmware version in bytes 10-11
 * - Random or sequential serial number in bytes 12-15
 */
static void generate_dev_uuid(const uint8_t prefix[2])
{
    const uint8_t *mac = esp_bt_dev_get_address();

    // Clear the entire UUID first
    memset(dev_uuid, 0, 16);

    // Bytes 0-1: User-defined prefix for filtering
    dev_uuid[0] = prefix[0];
    dev_uuid[1] = prefix[1];

    // Bytes 2-7: Bluetooth MAC address (6 bytes)
    memcpy(dev_uuid + 2, mac, 6);

    // Bytes 8-15: Already zero from memset (padding)

    ESP_LOGI(TAG, "Generated UUID with prefix [0x%02x 0x%02x]", prefix[0], prefix[1]);
}

/*
 * INITIALIZE BLUETOOTH STACK
 * ===========================
 *
 * Sets up the ESP32 Bluetooth subsystem for BLE Mesh.
 *
 * BLUETOOTH STACK ARCHITECTURE:
 * ┌─────────────────────────────┐
 * │  BLE Mesh Stack             │  ← We're initializing this
 * ├─────────────────────────────┤
 * │  Bluedroid Host             │  ← Bluetooth host stack
 * ├─────────────────────────────┤
 * │  Controller (HCI)           │  ← Low-level BLE radio
 * └─────────────────────────────┘
 *
 * STEPS:
 * 1. Release classic Bluetooth memory (we only need BLE)
 * 2. Initialize Bluedroid host stack
 * 3. Enable Bluedroid stack
 *
 * WHY RELEASE CLASSIC BT MEMORY:
 * ESP32 supports both Classic Bluetooth and BLE. Since we only need BLE,
 * we release Classic BT memory to save RAM (~60KB saved).
 */
static esp_err_t bluetooth_init(void)
{
    esp_err_t ret;

    // Release Classic Bluetooth controller memory (we only use BLE)
    ret = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bluetooth controller release classic bt memory failed");
        return ret;
    }

    // Initialize Bluetooth controller with default configuration
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bluetooth controller initialize failed");
        return ret;
    }

    // Enable Bluetooth controller in BLE mode only
    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bluetooth controller enable failed");
        return ret;
    }

    // Initialize Bluedroid host stack
    ret = esp_bluedroid_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bluetooth bluedroid init failed");
        return ret;
    }

    // Enable Bluedroid stack
    ret = esp_bluedroid_enable();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bluetooth bluedroid enable failed");
        return ret;
    }

    ESP_LOGI(TAG, "Bluetooth initialized");
    return ESP_OK;
}

/*
 * INITIALIZE BLE MESH NODE
 * =========================
 *
 * Main initialization function - sets up everything needed for mesh operation.
 *
 * INITIALIZATION SEQUENCE:
 * 1. Initialize NVS (Non-Volatile Storage)
 *    - BLE Mesh stores provisioning data in NVS
 *    - Allows node to remember it's provisioned after reboot
 *
 * 2. Generate Device UUID
 *    - Creates unique identifier with user's prefix
 *    - Used during provisioning discovery
 *
 * 3. Initialize Bluetooth Stack
 *    - Sets up controller and host layers
 *
 * 4. Initialize BLE Mesh
 *    - Registers provisioning callback
 *    - Sets composition data
 *    - Initializes mesh stack
 *
 * 5. Register Model Callbacks
 *    - Config Server callback (handles configuration commands)
 *    - Generic Server callback (handles OnOff Get/Set)
 *
 * 6. Store Application Callbacks
 *    - Saves user's callbacks for LED control, etc.
 */
esp_err_t node_init(const node_config_t *config)
{
    esp_err_t ret;

    if (!config) {
        ESP_LOGE(TAG, "Config is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    // Step 1: Initialize NVS for persistent storage
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // NVS partition was truncated or has wrong version
        // Erase and reinitialize
        ESP_LOGW(TAG, "NVS partition needs erase, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS flash init failed");
        return ret;
    }

    // Step 2: Generate device UUID with user's prefix
    generate_dev_uuid(config->device_uuid_prefix);

    // Step 3: Initialize Bluetooth stack
    ret = bluetooth_init();
    if (ret != ESP_OK) {
        return ret;
    }

    // Step 3.5: Initialize provision structure
    // The struct fields depend on whether PROVISIONER role is also enabled
    esp_ble_mesh_prov_t temp_prov = {
        .uuid = dev_uuid,                 // Our UUID (for being discovered/provisioned)
#if CONFIG_BLE_MESH_PROVISIONER
        // Dual role (node + provisioner): use provisioner-style fields
        .prov_uuid = dev_uuid,            // Same UUID (for dual role support)
        .prov_unicast_addr = 0,           // Will be assigned during provisioning
        .prov_start_address = 0,          // Not used by pure node
        .prov_attention = 0x00,           // Attention timer
        .prov_algorithm = 0x00,           // FIPS P-256 Elliptic Curve
        .prov_pub_key_oob = 0x00,         // No OOB public key
        .prov_static_oob_val = NULL,      // No static OOB
        .prov_static_oob_len = 0x00,      // No static OOB length
        .flags = 0x00,                    // No special flags
        .iv_index = 0x00,                 // Will be set by provisioner
#else
        // Node-only mode: use node-specific OOB fields
        .output_size = 0,                 // No output OOB capability
        .output_actions = 0,              // No output actions
#endif
    };
    memcpy(&provision, &temp_prov, sizeof(provision));

    // Step 4: Initialize BLE Mesh
    ret = esp_ble_mesh_init(&provision, &composition);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "BLE Mesh init failed (err %d)", ret);
        return ret;
    }

    // Step 5: Register provisioning callback
    ret = esp_ble_mesh_register_prov_callback(mesh_prov_cb);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register provisioning callback");
        return ret;
    }

    // Step 6: Register Configuration Server callback
    ret = esp_ble_mesh_register_config_server_callback(mesh_config_server_cb);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register config server callback");
        return ret;
    }

    // Step 7: Register Generic OnOff Server callback
    ret = esp_ble_mesh_register_generic_server_callback(mesh_generic_server_cb);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register generic server callback");
        return ret;
    }

    // Step 8: Store application callbacks
    memcpy(&app_callbacks, &config->callbacks, sizeof(node_callbacks_t));

    ESP_LOGI(TAG, "BLE Mesh Node initialized successfully");
    return ESP_OK;
}

/*
 * START BLE MESH NODE
 * ===================
 *
 * Begins mesh operation. What happens depends on provisioning state:
 *
 * IF NOT PROVISIONED (first boot):
 * - Starts broadcasting Unprovisioned Device Beacons
 * - Beacons contain the device UUID
 * - Provisioner can discover and provision the node
 *
 * IF ALREADY PROVISIONED (stored in NVS):
 * - Rejoins the mesh network with stored credentials
 * - Uses stored NetKey and unicast address
 * - Ready to receive commands immediately
 *
 * This is why NVS is important - it provides persistent storage
 * so the node doesn't need to be reprovisioned after every reboot.
 */
esp_err_t node_start(void)
{
    esp_err_t ret;

    // Enable BLE Mesh node functionality
    // If node is already provisioned (stored in NVS), it will rejoin the network
    // If not provisioned, it will start broadcasting beacons
    ret = esp_ble_mesh_node_prov_enable(ESP_BLE_MESH_PROV_ADV | ESP_BLE_MESH_PROV_GATT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable mesh node provisioning");
        return ret;
    }

    ESP_LOGI(TAG, "BLE Mesh Node started - broadcasting beacons");
    ESP_LOGI(TAG, "Waiting to be provisioned...");
    return ESP_OK;
}

/*
 * GET ONOFF STATE
 * ===============
 *
 * Returns the current Generic OnOff state.
 * Useful for:
 * - Displaying state on LCD/OLED
 * - Syncing hardware with mesh state
 * - Implementing toggle functionality
 */
uint8_t node_get_onoff_state(void)
{
    return onoff_state;
}

/*
 * SET ONOFF STATE LOCALLY
 * ========================
 *
 * Changes the OnOff state and publishes it to the network.
 *
 * USE CASES:
 * - User presses physical button on device
 * - Timer expires and changes state
 * - Sensor triggers state change
 *
 * WHAT THIS DOES:
 * 1. Updates local state
 * 2. Updates model state
 * 3. Calls application callback (to control LED)
 * 4. Publishes state change to network (if publication configured)
 *
 * NOTE: Publishing requires the provisioner to configure publication
 * (destination address, publish period, etc.)
 */
esp_err_t node_set_onoff_state(uint8_t onoff)
{
    onoff_state = onoff;
    onoff_server.state.onoff = onoff;
    onoff_server.state.target_onoff = onoff;

    // Notify application
    if (app_callbacks.onoff_changed) {
        app_callbacks.onoff_changed(onoff);
    }

    ESP_LOGI(TAG, "OnOff state changed to: %d", onoff);

    // TODO: Publish state change to network
    // Requires: esp_ble_mesh_server_model_send_msg()

    return ESP_OK;
}

/*
 * PROVISIONING CALLBACK
 * =====================
 *
 * Handles provisioning-related events.
 *
 * KEY EVENTS FOR NODE:
 * - PROV_REGISTER_COMP: BLE Mesh stack initialized successfully
 * - NODE_PROV_LINK_OPEN: Provisioning started (bearer established)
 * - NODE_PROV_LINK_CLOSE: Provisioning ended (success or failure)
 * - NODE_PROV_COMPLETE: Provisioning successful! Node is now part of network
 * - NODE_PROV_RESET: Node unprovisioned (factory reset)
 */
static void mesh_prov_cb(esp_ble_mesh_prov_cb_event_t event,
                        esp_ble_mesh_prov_cb_param_t *param)
{
    switch (event) {
    case ESP_BLE_MESH_PROV_REGISTER_COMP_EVT:
        ESP_LOGI(TAG, "BLE Mesh provisioning registered, err_code %d", param->prov_register_comp.err_code);
        break;

    case ESP_BLE_MESH_NODE_PROV_ENABLE_COMP_EVT:
        ESP_LOGI(TAG, "Node provisioning enabled, err_code %d", param->node_prov_enable_comp.err_code);
        break;

    case ESP_BLE_MESH_NODE_PROV_LINK_OPEN_EVT:
        ESP_LOGI(TAG, "Provisioning link opened with bearer: %s",
                 param->node_prov_link_open.bearer == ESP_BLE_MESH_PROV_ADV ? "PB-ADV" : "PB-GATT");
        break;

    case ESP_BLE_MESH_NODE_PROV_LINK_CLOSE_EVT:
        ESP_LOGI(TAG, "Provisioning link closed with bearer: %s",
                 param->node_prov_link_close.bearer == ESP_BLE_MESH_PROV_ADV ? "PB-ADV" : "PB-GATT");
        break;

    case ESP_BLE_MESH_NODE_PROV_COMPLETE_EVT:
        ESP_LOGI(TAG, "Provisioning complete!");
        ESP_LOGI(TAG, "  Unicast address: 0x%04x", param->node_prov_complete.addr);
        ESP_LOGI(TAG, "  NetKey index: 0x%04x", param->node_prov_complete.net_idx);
        // Note: element count is in our composition data, not in the event

        // Notify application
        if (app_callbacks.provisioned) {
            app_callbacks.provisioned(param->node_prov_complete.addr);
        }
        break;

    case ESP_BLE_MESH_NODE_PROV_RESET_EVT:
        ESP_LOGI(TAG, "Node reset - returning to unprovisioned state");

        // Notify application to clear state
        if (app_callbacks.reset) {
            app_callbacks.reset();
        }
        break;

    default:
        break;
    }
}

/*
 * CONFIGURATION SERVER CALLBACK
 * ==============================
 *
 * Handles configuration-related events.
 * The provisioner uses Configuration Client to send configuration commands,
 * and this Configuration Server handles them.
 *
 * KEY EVENTS:
 * - APP_KEY_ADD: Provisioner added an application key
 * - MODEL_APP_BIND: Provisioner bound AppKey to a model
 * - MODEL_SUB_ADD: Provisioner added subscription (for group messages)
 * - MODEL_PUB_SET: Provisioner configured publication settings
 */
static void mesh_config_server_cb(esp_ble_mesh_cfg_server_cb_event_t event,
                                   esp_ble_mesh_cfg_server_cb_param_t *param)
{
    switch (event) {
    case ESP_BLE_MESH_CFG_SERVER_STATE_CHANGE_EVT:
        switch (param->ctx.recv_op) {
        case ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD:
            ESP_LOGI(TAG, "AppKey added: NetKeyIndex=0x%04x, AppKeyIndex=0x%04x",
                     param->value.state_change.appkey_add.net_idx,
                     param->value.state_change.appkey_add.app_idx);
            break;

        case ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND:
            ESP_LOGI(TAG, "Model app bind: ElementAddr=0x%04x, AppKeyIndex=0x%04x, ModelID=0x%04x",
                     param->value.state_change.mod_app_bind.element_addr,
                     param->value.state_change.mod_app_bind.app_idx,
                     param->value.state_change.mod_app_bind.model_id);
            break;

        default:
            break;
        }
        break;

    default:
        break;
    }
}

/*
 * GENERIC SERVER CALLBACK
 * ========================
 *
 * Handles Generic OnOff Server events.
 *
 * KEY EVENTS:
 * - GEN_ONOFF_GET: Provisioner/client queries current state
 *   Response is automatic (we configured ESP_BLE_MESH_SERVER_AUTO_RSP)
 *
 * - GEN_ONOFF_SET/SET_UNACK: Provisioner/client changes state
 *   We update our state and notify the application (to control LED)
 *
 * SET vs SET_UNACK:
 * - SET: Requires acknowledgment (more reliable)
 * - SET_UNACK: No acknowledgment (faster, less overhead)
 */
static void mesh_generic_server_cb(esp_ble_mesh_generic_server_cb_event_t event,
                                    esp_ble_mesh_generic_server_cb_param_t *param)
{
    switch (event) {
    case ESP_BLE_MESH_GENERIC_SERVER_STATE_CHANGE_EVT:
        switch (param->ctx.recv_op) {
        case ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET:
        case ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET_UNACK:
            onoff_state = param->value.state_change.onoff_set.onoff;
            ESP_LOGI(TAG, "OnOff state changed to: %d", onoff_state);

            // Notify application (e.g., to control LED)
            if (app_callbacks.onoff_changed) {
                app_callbacks.onoff_changed(onoff_state);
            }
            break;

        default:
            break;
        }
        break;

    case ESP_BLE_MESH_GENERIC_SERVER_RECV_GET_MSG_EVT:
        ESP_LOGI(TAG, "Received Generic OnOff Get");
        // Response is automatic due to ESP_BLE_MESH_SERVER_AUTO_RSP
        break;

    case ESP_BLE_MESH_GENERIC_SERVER_RECV_SET_MSG_EVT:
        ESP_LOGI(TAG, "Received Generic OnOff Set");
        // State change already handled in STATE_CHANGE_EVT above
        break;

    default:
        break;
    }
}
