/*
 * BLE MESH PROVISIONER - Main Implementation
 * ===========================================
 *
 * EDUCATIONAL NOTE - FILE PURPOSE:
 * This file implements the core provisioner functionality. A provisioner is like
 * the "network administrator" of a BLE Mesh network - it brings new devices into
 * the network and sets them up.
 *
 * KEY RESPONSIBILITIES:
 * 1. Initialize Bluetooth stack (controller + host)
 * 2. Set up BLE Mesh with our composition data
 * 3. Scan for and provision unprovisioned devices
 * 4. Send control messages to provisioned nodes
 */

#include "ble_mesh_provisioner.h"
#include "ble_mesh_callbacks.h"
#include "ble_mesh_storage.h"

#include "esp_log.h"
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

#define TAG "BLE_MESH_PROV"

/*
 * Company ID for ESP32
 * Assigned by Bluetooth SIG - identifies the manufacturer
 */
#define CID_ESP 0x02E5

/*
 * Application Key fill byte
 * The actual app key is 16 bytes filled with this value (0x12121212...)
 * In production, use a cryptographically random key!
 */
#define APP_KEY_OCTET 0x12

/*
 * EDUCATIONAL NOTE - GLOBAL STATE:
 *
 * These variables maintain the provisioner's state:
 * - dev_uuid: Our unique device identifier (16 bytes)
 * - prov_config: Configuration passed by user
 * - prov_callbacks: Optional user callbacks
 */
static uint8_t dev_uuid[16];
static provisioner_config_t prov_config;
static provisioner_callbacks_t prov_callbacks;

/*
 * EDUCATIONAL NOTE - CLIENT MODELS:
 *
 * These are "client" models - they send commands to "server" models on other nodes.
 *
 * config_client: Used to configure other nodes (add keys, bind models, etc.)
 * onoff_client: Used to control Generic OnOff servers (lights, switches)
 *
 * The 'model' field inside gets populated automatically by the mesh stack
 * when we register these in our composition data.
 */
esp_ble_mesh_client_t config_client;
esp_ble_mesh_client_t onoff_client;

/*
 * EDUCATIONAL NOTE - CRYPTOGRAPHIC KEYS:
 *
 * BLE Mesh uses a layered security model:
 *
 * 1. Network Key (NetKey):
 *    - Encrypts all network-layer communication
 *    - Shared by all nodes in the network
 *    - Indexed by net_idx (usually 0 for primary network)
 *
 * 2. Application Key (AppKey):
 *    - Encrypts application-layer messages
 *    - Can have multiple keys for different applications
 *    - Indexed by app_idx (0 for our main application)
 *
 * This structure holds both keys for easy access.
 */
struct esp_ble_mesh_key {
    uint16_t net_idx;           // Network key index
    uint16_t app_idx;           // Application key index
    uint8_t  app_key[16];       // The actual 128-bit application key
};

struct esp_ble_mesh_key prov_key = {
    .net_idx = 0,
    .app_idx = 0,
    .app_key = {0}              // Will be filled during init
};

/*
 * EDUCATIONAL NOTE - CONFIGURATION SERVER:
 *
 * This defines how OUR provisioner behaves in the mesh:
 *
 * - net_transmit: Transmit each message 3 times (2 retransmissions) with 20ms intervals
 *                 This improves reliability in noisy environments
 *
 * - relay: DISABLED means we don't forward other nodes' messages
 *          (Provisioners typically don't relay to save power)
 *
 * - beacon: ENABLED means we broadcast mesh beacons
 *           Beacons help nodes discover the network
 *
 * - gatt_proxy: Allows non-mesh BLE devices to interact via GATT connections
 *
 * - default_ttl: Time-To-Live = 7 hops
 *                Messages can pass through up to 7 intermediate nodes
 *                Decrements at each hop to prevent infinite loops
 */
static esp_ble_mesh_cfg_srv_t config_server = {
    .net_transmit = ESP_BLE_MESH_TRANSMIT(2, 20),      // 2 retransmits, 20ms apart
    .relay = ESP_BLE_MESH_RELAY_DISABLED,              // Don't relay messages
    .relay_retransmit = ESP_BLE_MESH_TRANSMIT(2, 20),  // If we did relay
    .beacon = ESP_BLE_MESH_BEACON_ENABLED,             // Broadcast beacons
#if defined(CONFIG_BLE_MESH_GATT_PROXY_SERVER)
    .gatt_proxy = ESP_BLE_MESH_GATT_PROXY_ENABLED,     // Support GATT proxy
#else
    .gatt_proxy = ESP_BLE_MESH_GATT_PROXY_NOT_SUPPORTED,
#endif
#if defined(CONFIG_BLE_MESH_FRIEND)
    .friend_state = ESP_BLE_MESH_FRIEND_ENABLED,       // Friend feature (for low-power nodes)
#else
    .friend_state = ESP_BLE_MESH_FRIEND_NOT_SUPPORTED,
#endif
    .default_ttl = 7,                                  // Max 7 hops
};

/*
 * EDUCATIONAL NOTE - SENSOR CLIENT:
 *
 * The Sensor Client model allows us to receive sensor data from nodes.
 * This is essential for our IMU sensor data collection.
 */
static esp_ble_mesh_client_t sensor_client;

/*
 * EDUCATIONAL NOTE - VENDOR MODEL CLIENT:
 *
 * Vendor models allow custom application-specific messages beyond the
 * standard SIG models. We use this to receive bulk IMU data (all 6 axes
 * in one message) more efficiently than the standard sensor model.
 *
 * Company ID 0x0001 is used for this custom implementation.
 */
#define VENDOR_MODEL_ID_CLIENT  0x0000
#define VENDOR_MODEL_ID_SERVER  0x0001
#define VENDOR_COMPANY_ID       0x0001

// Vendor model operations (opcodes we can receive)
// IMPORTANT: Must have at least one 3-byte vendor opcode
// Our IMU data opcode: 0xC00001 = ESP_BLE_MESH_MODEL_OP_3(0xC0, 0x0001)
static esp_ble_mesh_model_op_t vendor_model_op[] = {
    ESP_BLE_MESH_MODEL_OP(ESP_BLE_MESH_MODEL_OP_3(0xC0, 0x0001), 0),  // IMU data opcode
    ESP_BLE_MESH_MODEL_OP_END,
};

/*
 * EDUCATIONAL NOTE - MODELS ARRAY:
 *
 * This array defines what functionality our provisioner supports.
 * Think of models as "apps" or "capabilities" of the device.
 *
 * 1. Configuration Server (CFG_SRV):
 *    - MANDATORY for all mesh nodes
 *    - Allows remote configuration of this node
 *    - Responds to config messages (get/set state)
 *
 * 2. Configuration Client (CFG_CLI):
 *    - Allows THIS node to configure OTHER nodes
 *    - Sends config messages (AppKey add, model bind, etc.)
 *    - Essential for a provisioner!
 *
 * 3. Generic OnOff Client (GEN_ONOFF_CLI):
 *    - Sends ON/OFF commands
 *    - Controls Generic OnOff Server models (lights, switches)
 *    - Demonstrates application-level control
 *
 * 4. Sensor Client (SENSOR_CLI):
 *    - Receives sensor data from Sensor Server models
 *    - Essential for collecting IMU data from M5Stick nodes
 */
static esp_ble_mesh_model_t root_models[] = {
    ESP_BLE_MESH_MODEL_CFG_SRV(&config_server),        // Configuration Server
    ESP_BLE_MESH_MODEL_CFG_CLI(&config_client),        // Configuration Client
    ESP_BLE_MESH_MODEL_GEN_ONOFF_CLI(NULL, &onoff_client), // Generic OnOff Client
    ESP_BLE_MESH_MODEL_SENSOR_CLI(NULL, &sensor_client),   // Sensor Client
};

/*
 * EDUCATIONAL NOTE - VENDOR MODELS ARRAY:
 *
 * Vendor models are kept in a separate array from SIG models.
 * This array contains our custom vendor model for receiving bulk IMU data.
 *
 * We define the model inline using ESP_BLE_MESH_VENDOR_MODEL macro to ensure
 * it's a constant expression (required for static array initialization).
 */
static esp_ble_mesh_model_t vendor_models[] = {
    ESP_BLE_MESH_VENDOR_MODEL(VENDOR_COMPANY_ID, VENDOR_MODEL_ID_CLIENT, vendor_model_op, NULL, NULL),
};

/*
 * EDUCATIONAL NOTE - ELEMENTS:
 *
 * Elements are addressable entities within a node. Each element:
 * - Gets its own unicast address
 * - Contains one or more models
 * - Represents a physical or logical "thing"
 *
 * Example multi-element device (ceiling fan):
 *   Element 0 (address 0x0001): Fan motor (Level model)
 *   Element 1 (address 0x0002): Light (OnOff model)
 *
 * Our provisioner has just 1 element with our 4 SIG models and 1 vendor model.
 *
 * ESP_BLE_MESH_ELEMENT parameters:
 * - 0: Location (0 = main/primary element)
 * - root_models: Array of SIG models in this element
 * - vendor_models: Array of vendor models in this element
 */
static esp_ble_mesh_elem_t elements[] = {
    ESP_BLE_MESH_ELEMENT(0, root_models, vendor_models),
};

/*
 * EDUCATIONAL NOTE - COMPOSITION DATA:
 *
 * This is like the "business card" of our node. It tells other nodes:
 * - Who made this device (Company ID)
 * - What elements it has
 * - What models are in each element
 *
 * When we provision a new node, we request ITS composition data
 * to learn what it can do, then configure it appropriately.
 *
 * CID_ESP: Identifies this as an Espressif device
 * elements: Our array of elements (just one)
 * element_count: How many elements (calculated by ARRAY_SIZE macro)
 */
static esp_ble_mesh_comp_t composition = {
    .cid = CID_ESP,                          // Company ID
    .element_count = ARRAY_SIZE(elements),   // Number of elements
    .elements = elements,                    // Pointer to elements array
};

/*
 * EDUCATIONAL NOTE - PROVISION STRUCTURE:
 *
 * This structure contains provisioning parameters. It serves dual purposes:
 *
 * 1. When we ACT AS A NODE (being provisioned by someone else):
 *    .uuid = Our device UUID for identification
 *
 * 2. When we ACT AS A PROVISIONER (provisioning others):
 *    .prov_uuid = Our provisioner UUID
 *    .prov_unicast_addr = Our own address in the mesh
 *    .prov_start_address = Where to start assigning addresses to new nodes
 *
 * Since CONFIG_BLE_MESH_NODE and CONFIG_BLE_MESH_PROVISIONER are both enabled,
 * we set both uuid fields to the same value.
 *
 * Initialized during provisioner_init() with user-provided configuration.
 */
static esp_ble_mesh_prov_t provision;

/*
 * FUNCTION: generate_dev_uuid
 * ===========================
 *
 * EDUCATIONAL NOTE:
 * Creates a unique 16-byte UUID for this device.
 *
 * UUID Structure:
 * [0-1]   : Prefix (e.g., 0xdddd) - for filtering during provisioning
 * [2-7]   : Device's Bluetooth MAC address (6 bytes) - ensures uniqueness
 * [8-15]  : Zeros (padding)
 *
 * WHY THIS MATTERS:
 * - Each unprovisioned device broadcasts its UUID
 * - Provisioners can filter by UUID prefix (e.g., only provision 0xdddd devices)
 * - MAC address ensures no two devices have the same UUID
 * - This is how you identify "your" devices vs. neighbor's devices
 *
 * Example UUID:
 * [dd dd fc e8 c0 a0 bf aa 00 00 00 00 00 00 00 00]
 *  \-/  \-----------------/  \---------------------/
 * prefix   MAC address          padding
 */
static void generate_dev_uuid(uint8_t *uuid, const uint8_t *prefix)
{
    if (!uuid) {
        ESP_LOGE(TAG, "Invalid device uuid");
        return;
    }

    // Clear the UUID buffer
    memset(uuid, 0, 16);

    // Set the prefix (first 2 bytes) - used for UUID matching/filtering
    uuid[0] = prefix[0];
    uuid[1] = prefix[1];

    // Copy the Bluetooth MAC address (next 6 bytes) - ensures uniqueness
    // esp_bt_dev_get_address() returns pointer to 6-byte MAC address
    memcpy(uuid + 2, esp_bt_dev_get_address(), BD_ADDR_LEN);

    // Remaining 8 bytes stay zero (padding)
}

/*
 * FUNCTION: bluetooth_init
 * ========================
 *
 * EDUCATIONAL NOTE - BLUETOOTH STACK LAYERS:
 *
 * The ESP32 Bluetooth stack has two main parts:
 *
 * 1. CONTROLLER (Lower layer):
 *    - Handles radio/PHY layer
 *    - Link layer operations
 *    - Lives in ROM/RAM, very hardware-specific
 *    - Communicates via HCI (Host Controller Interface)
 *
 * 2. HOST (Upper layer - Bluedroid):
 *    - Handles L2CAP, SMP, ATT, GATT
 *    - Application-level BLE operations
 *    - More flexible, software-based
 *
 * INITIALIZATION SEQUENCE:
 *
 * Step 1: Release Classic Bluetooth memory
 *         Since we only need BLE, free up the Classic BT memory
 *         Saves ~30KB of RAM!
 *
 * Step 2: Initialize BT Controller
 *         Sets up the hardware radio and link layer
 *         Uses default config (defined in sdkconfig)
 *
 * Step 3: Enable BLE mode
 *         Activates BLE-specific features in the controller
 *         (vs. Classic Bluetooth or Dual mode)
 *
 * Step 4: Initialize Bluedroid host stack
 *         Sets up GAP, GATT, SMP services
 *         Prepares for BLE Mesh on top
 *
 * Step 5: Enable Bluedroid
 *         Starts the host stack
 *         Now ready for BLE Mesh initialization
 */
static esp_err_t bluetooth_init(void)
{
    esp_err_t ret;

    // STEP 1: Release Classic Bluetooth memory
    // BLE Mesh only needs BLE, not Classic BT
    // This frees up RAM for our application
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));

    // STEP 2: Initialize the Bluetooth controller
    // BT_CONTROLLER_INIT_CONFIG_DEFAULT() provides standard config from sdkconfig
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret) {
        ESP_LOGE(TAG, "Initialize controller failed");
        return ret;
    }

    // STEP 3: Enable BLE mode on the controller
    // This activates the BLE radio and link layer
    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret) {
        ESP_LOGE(TAG, "Enable controller failed");
        return ret;
    }

    // STEP 4: Initialize Bluedroid host stack
    // Sets up GAP, GATT, SMP, and other BLE services
    ret = esp_bluedroid_init();
    if (ret) {
        ESP_LOGE(TAG, "Init bluetooth failed");
        return ret;
    }

    // STEP 5: Enable (start) the Bluedroid stack
    // Now we can use BLE APIs and initialize BLE Mesh on top
    ret = esp_bluedroid_enable();
    if (ret) {
        ESP_LOGE(TAG, "Enable bluetooth failed");
        return ret;
    }

    return ret;
}

/*
 * FUNCTION: provisioner_init
 * ==========================
 *
 * EDUCATIONAL NOTE - INITIALIZATION FLOW:
 *
 * This is the main initialization function that sets up everything needed
 * for the provisioner to work. It's called once at startup.
 *
 * WHAT HAPPENS (in order):
 *
 * 1. VALIDATE & STORE CONFIG:
 *    Save user configuration (addresses, keys, UUID prefix)
 *
 * 2. INITIALIZE STORAGE:
 *    Set up node information storage (tracks provisioned nodes)
 *
 * 3. INITIALIZE BLUETOOTH:
 *    Start BT controller and Bluedroid host stack
 *
 * 4. GENERATE UUID:
 *    Create unique device identifier using MAC address
 *
 * 5. SETUP PROVISIONING PARAMETERS:
 *    Configure addresses, keys, and security settings
 *
 * 6. REGISTER CALLBACKS:
 *    Tell mesh stack where to send events (provisioning, config, messages)
 *
 * 7. INITIALIZE BLE MESH:
 *    Start the mesh stack with our composition data
 *
 * 8. SET UUID MATCHING:
 *    Tell provisioner to only scan for devices with matching UUID prefix
 *
 * After this, call provisioner_start() to begin scanning and provisioning.
 */
esp_err_t provisioner_init(const provisioner_config_t *config, const provisioner_callbacks_t *callbacks)
{
    esp_err_t err;

    // Validate input
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }

    // STEP 1: Store configuration for later use
    memcpy(&prov_config, config, sizeof(provisioner_config_t));
    if (callbacks) {
        memcpy(&prov_callbacks, callbacks, sizeof(provisioner_callbacks_t));
    }

    // STEP 2: Initialize node storage
    // This keeps track of all nodes we've provisioned
    err = mesh_storage_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Storage init failed");
        return err;
    }

    // STEP 3: Initialize Bluetooth stack
    // Sets up controller and Bluedroid host
    err = bluetooth_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Bluetooth init failed");
        return err;
    }

    // STEP 4: Generate unique device UUID
    // Uses MAC address to ensure uniqueness
    // Format: [prefix][MAC][zeros]
    generate_dev_uuid(dev_uuid, config->match_prefix);

    // STEP 5: Initialize provision structure
    // This must be done at runtime because some fields (addresses) come from config
    // We use a temporary struct then memcpy to handle const fields in the struct
    esp_ble_mesh_prov_t temp_prov = {
        .uuid = dev_uuid,                           // Our UUID (for being provisioned)
        .prov_uuid = dev_uuid,                      // Our UUID (as provisioner)
        .prov_unicast_addr = config->own_address,   // Our address in the mesh
        .prov_start_address = config->node_start_address, // Where to start assigning addresses
        .prov_attention = 0x00,                     // Attention timer (not used)
        .prov_algorithm = 0x00,                     // Security algorithm (FIPS P-256)
        .prov_pub_key_oob = 0x00,                   // Public key OOB (not used)
        .prov_static_oob_val = NULL,                // Static OOB value (not used)
        .prov_static_oob_len = 0x00,                // Static OOB length
        .flags = 0x00,                              // Key refresh flag
        .iv_index = 0x00,                           // IV index (for replay protection)
    };
    memcpy(&provision, &temp_prov, sizeof(provision));

    // STEP 6: Setup cryptographic keys
    // Network key is automatically generated by the stack
    // Application key is filled with a pattern (0x12121212...)
    // In production, use cryptographically random keys!
    prov_key.net_idx = config->net_idx;
    prov_key.app_idx = config->app_idx;
    memset(prov_key.app_key, APP_KEY_OCTET, sizeof(prov_key.app_key));

    // STEP 7: Register event callbacks
    // These tell the mesh stack where to send events:
    // - Provisioning events (device found, provisioning complete, etc.)
    // - Configuration events (comp data received, appkey added, etc.)
    // - Generic client events (OnOff status received, etc.)
    // - Sensor client events (Sensor data received, etc.)
    // - Vendor model events (Custom vendor messages received, etc.)
    esp_ble_mesh_register_prov_callback(mesh_provisioning_cb);
    esp_ble_mesh_register_config_client_callback(mesh_config_client_cb);
    esp_ble_mesh_register_generic_client_callback(mesh_generic_client_cb);
    esp_ble_mesh_register_sensor_client_callback(mesh_sensor_client_cb);

    // Register vendor model callback
    // This receives bulk IMU data from nodes
    esp_ble_mesh_register_custom_model_callback(mesh_vendor_client_cb);

    // STEP 8: Initialize BLE Mesh stack
    // This sets up all the mesh layers (network, transport, access)
    // Registers our composition data (what models we support)
    err = esp_ble_mesh_init(&provision, &composition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize mesh stack");
        return err;
    }

    // STEP 9: Set UUID matching filter
    // Only scan for devices whose UUID starts with config->match_prefix
    // Parameters:
    //   - match_prefix: The bytes to match (e.g., [0xdd, 0xdd])
    //   - 2: Length of prefix (2 bytes)
    //   - 0x0: Offset in UUID where matching starts (byte 0)
    //   - false: Don't match exactly, just check prefix
    err = esp_ble_mesh_provisioner_set_dev_uuid_match(config->match_prefix, 2, 0x0, false);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set matching device uuid");
        return err;
    }

    ESP_LOGI(TAG, "Provisioner initialized successfully");
    return ESP_OK;
}

/*
 * FUNCTION: provisioner_start
 * ===========================
 *
 * EDUCATIONAL NOTE:
 *
 * Starts the provisioning process. After calling this, the provisioner will:
 * 1. Start scanning for unprovisioned device beacons
 * 2. Automatically provision matching devices
 * 3. Configure them with application keys
 *
 * TWO PROVISIONING BEARERS:
 *
 * - PB-ADV (Provisioning over Advertising):
 *   Uses BLE advertisements (connectionless)
 *   Faster, works with multiple devices simultaneously
 *   Preferred method for most use cases
 *
 * - PB-GATT (Provisioning over GATT):
 *   Uses BLE GATT connections
 *   More reliable in noisy environments
 *   Can go through GATT proxy nodes
 *   Slower, one device at a time
 *
 * We enable both for maximum compatibility.
 *
 * LOCAL APP KEY:
 * After enabling provisioning, we add our application key locally.
 * This allows our provisioner to send application-layer messages
 * (like Generic OnOff commands) to provisioned nodes.
 */
esp_err_t provisioner_start(void)
{
    esp_err_t err;

    // Enable provisioning on both bearers (ADV and GATT)
    // This starts scanning for unprovisioned device beacons
    err = esp_ble_mesh_provisioner_prov_enable(
        (esp_ble_mesh_prov_bearer_t)(ESP_BLE_MESH_PROV_ADV | ESP_BLE_MESH_PROV_GATT)
    );
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable provisioner");
        return err;
    }

    // Add our application key locally
    // This registers the AppKey with our mesh stack so we can:
    // 1. Send it to provisioned nodes during configuration
    // 2. Use it to encrypt our own application messages
    err = esp_ble_mesh_provisioner_add_local_app_key(
        prov_key.app_key,    // The 16-byte application key
        prov_key.net_idx,    // Network key index it's bound to
        prov_key.app_idx     // Application key index
    );
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add local app key");
        return err;
    }

    ESP_LOGI(TAG, "Provisioner started - nodes will publish to 0x0001");
    return ESP_OK;
}

/*
 * FUNCTION: provisioner_stop
 * ==========================
 *
 * EDUCATIONAL NOTE:
 *
 * Stops the provisioning process:
 * - Disables scanning for unprovisioned devices
 * - Stops automatic provisioning
 * - Existing provisioned nodes remain in the network
 * - Can be restarted later with provisioner_start()
 *
 * Useful for:
 * - Saving power when not actively provisioning
 * - Preventing accidental provisioning of new devices
 * - Pausing provisioning temporarily
 */
esp_err_t provisioner_stop(void)
{
    esp_err_t err;

    // Disable both provisioning bearers
    // Stops scanning and provisioning
    err = esp_ble_mesh_provisioner_prov_disable(
        (esp_ble_mesh_prov_bearer_t)(ESP_BLE_MESH_PROV_ADV | ESP_BLE_MESH_PROV_GATT)
    );
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to disable provisioner");
        return err;
    }

    ESP_LOGI(TAG, "Provisioner stopped");
    return ESP_OK;
}

/*
 * FUNCTION: provisioner_send_onoff
 * ================================
 *
 * EDUCATIONAL NOTE - SENDING MESH MESSAGES:
 *
 * This demonstrates how to send a message in BLE Mesh.
 * We're sending a Generic OnOff SET command to control a light/switch.
 *
 * MESSAGE STRUCTURE:
 *
 * 1. COMMON PARAMETERS (addressing and security):
 *    - opcode: What operation to perform (OnOff SET)
 *    - model: Which client model is sending (our onoff_client)
 *    - ctx.net_idx: Which network key to use for encryption
 *    - ctx.app_idx: Which application key to use for encryption
 *    - ctx.addr: Destination address (the target node)
 *    - ctx.send_ttl: Time-to-live (max hops = 3)
 *
 * 2. STATE PARAMETERS (what to set):
 *    - onoff: 0=OFF, 1=ON
 *    - tid: Transaction ID (to detect duplicate messages)
 *    - op_en: Optional parameters enabled (false = no transition time)
 *
 * WHAT HAPPENS:
 * 1. Message is encrypted with AppKey
 * 2. Encrypted payload is wrapped with network headers
 * 3. Encrypted again with NetKey
 * 4. Broadcast as BLE advertisement
 * 5. Target node receives, decrypts, and responds with OnOff STATUS
 */
esp_err_t provisioner_send_onoff(uint16_t unicast, bool onoff)
{
    esp_ble_mesh_generic_client_set_state_t set_state = {0};
    esp_ble_mesh_client_common_param_t common = {0};
    mesh_node_info_t node_info;
    esp_err_t err;

    // Verify the node exists in our storage
    err = mesh_storage_get_node(unicast, &node_info);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Node not found");
        return err;
    }

    // SETUP COMMON PARAMETERS (addressing and keys)
    common.opcode = ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET;  // OnOff SET operation
    common.model = onoff_client.model;                     // Our Generic OnOff client
    common.ctx.net_idx = prov_key.net_idx;                // Network key to use
    common.ctx.app_idx = prov_key.app_idx;                // Application key to use
    common.ctx.addr = unicast;                            // Destination address
    common.ctx.send_ttl = 3;                              // Max 3 hops
    common.msg_timeout = 0;                               // Wait forever for response

    // SETUP STATE PARAMETERS (what to set)
    set_state.onoff_set.op_en = false;      // No optional parameters
    set_state.onoff_set.onoff = onoff ? 1 : 0;  // 0=OFF, 1=ON
    set_state.onoff_set.tid = 0;            // Transaction ID (should increment)

    // SEND THE MESSAGE
    // This goes through: Access layer → Transport layer → Network layer → BLE
    err = esp_ble_mesh_generic_client_set_state(&common, &set_state);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send onoff command");
        return err;
    }

    return ESP_OK;
}

/*
 * FUNCTION: provisioner_get_node_count
 * ====================================
 *
 * EDUCATIONAL NOTE:
 *
 * Returns the number of nodes we've successfully provisioned.
 * Useful for:
 * - Monitoring network growth
 * - UI displays showing network size
 * - Determining if provisioning was successful
 *
 * Does NOT include the provisioner itself in the count.
 */
uint16_t provisioner_get_node_count(void)
{
    return mesh_storage_get_node_count();
}

