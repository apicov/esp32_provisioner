/*
 * ═══════════════════════════════════════════════════════════════════════════
 *                   BLE MESH PROVISIONER - CALLBACK HANDLERS
 * ═══════════════════════════════════════════════════════════════════════════
 *
 * WHAT IS BLE MESH?
 * ═════════════════
 *
 * BLE Mesh is a networking protocol that runs on top of Bluetooth Low Energy.
 * Unlike traditional Bluetooth (1-to-1 connections), BLE Mesh creates a
 * many-to-many network where devices can relay messages for each other.
 *
 * KEY DIFFERENCES FROM REGULAR BLUETOOTH:
 * ----------------------------------------
 *
 *   Regular BLE:                    BLE Mesh:
 *   ┌─────┐         ┌─────┐        ┌─────┐    ┌─────┐    ┌─────┐
 *   │Phone│────────▶│ LED │        │Phone│◀──▶│Light│◀──▶│ LED │
 *   └─────┘         └─────┘        └─────┘    └─────┘    └─────┘
 *   Direct link     Single device    Gateway   Relay      End node
 *   Limited range   No relaying      Messages hop through multiple devices
 *
 * WHY BLE MESH?
 * -------------
 * ✅ Extended range: Messages hop through relay nodes
 * ✅ Scalability: Supports thousands of devices
 * ✅ Reliability: Multiple paths to destination
 * ✅ Low power: Devices can sleep, wake up on events
 * ✅ Standardized: Interoperable between vendors
 *
 * BLE MESH ARCHITECTURE (OSI-style Layers):
 * ==========================================
 *
 *   ┌────────────────────────────────────────────────────────────┐
 *   │  Model Layer (Application Logic)                          │
 *   │  • Generic OnOff, Sensor, Vendor models                   │
 *   │  • Defines message formats and behaviors                  │
 *   ├────────────────────────────────────────────────────────────┤
 *   │  Foundation Layer (Network Management)                     │
 *   │  • Configuration Client/Server                            │
 *   │  • Provisioning                                           │
 *   │  • Health models                                          │
 *   ├────────────────────────────────────────────────────────────┤
 *   │  Access Layer (Encryption & Model Messages)               │
 *   │  • Message encryption (AES-CCM)                           │
 *   │  • Opcode + Parameters                                    │
 *   │  • Application key (AppKey) security                      │
 *   ├────────────────────────────────────────────────────────────┤
 *   │  Upper Transport Layer (Segmentation & Reassembly)        │
 *   │  • Splits large messages into segments                    │
 *   │  • Acknowledgments for reliable delivery                  │
 *   ├────────────────────────────────────────────────────────────┤
 *   │  Lower Transport Layer (Encryption)                       │
 *   │  • Transport encryption                                   │
 *   │  • Replay protection (sequence numbers)                   │
 *   ├────────────────────────────────────────────────────────────┤
 *   │  Network Layer (Routing)                                  │
 *   │  • Source/destination addresses                           │
 *   │  • TTL (Time To Live) for hop limiting                    │
 *   │  • Network key (NetKey) security                          │
 *   │  • Relay/forward logic                                    │
 *   ├────────────────────────────────────────────────────────────┤
 *   │  Bearer Layer (Physical Transport)                        │
 *   │  • Advertising Bearer (uses BLE advertisements)           │
 *   │  • GATT Bearer (uses BLE connections)                     │
 *   └────────────────────────────────────────────────────────────┘
 *
 * KEY CONCEPTS:
 * =============
 *
 * 1. PROVISIONING
 *    -----------
 *    Process of adding a device to the mesh network.
 *    Device gets: NetKey, unicast address, IV Index
 *
 * 2. ADDRESSES
 *    ---------
 *    • Unassigned: 0x0000 (not provisioned yet)
 *    • Unicast:    0x0001-0x7FFF (specific device)
 *    • Group:      0xC000-0xFEFF (multiple subscribers)
 *    • Virtual:    0x8000-0xBFFF (UUID-based groups)
 *    • All nodes:  0xFFFF (broadcast to everyone)
 *
 * 3. KEYS
 *    ----
 *    • NetKey: Network-level encryption (all devices have this)
 *    • AppKey: Application-level encryption (per-app or per-feature)
 *    • DevKey: Device-specific key (for configuration only)
 *
 * 4. MODELS
 *    ------
 *    Define application behavior (like "profiles" in classic BT):
 *    • Generic OnOff: Simple on/off control (lights, switches)
 *    • Sensor: Publish sensor data (temperature, motion, IMU)
 *    • Vendor: Custom application-specific models
 *
 * 5. ELEMENTS
 *    --------
 *    Addressable units within a device. Each element can have multiple models.
 *    Example: A light bulb might have 2 elements:
 *      Element 0: Generic OnOff Server (main light)
 *      Element 1: Generic Level Server (brightness)
 *
 * 6. PUBLISH/SUBSCRIBE
 *    -----------------
 *    Models can PUBLISH state changes to addresses.
 *    Models can SUBSCRIBE to addresses to receive messages.
 *    Example: Temperature sensor publishes to 0xC001 (all thermostats subscribe)
 *
 * ═══════════════════════════════════════════════════════════════════════════
 *                           THIS FILE'S ROLE
 * ═══════════════════════════════════════════════════════════════════════════
 *
 * This file contains callback functions that handle BLE Mesh events.
 * These callbacks are invoked by the ESP-IDF BLE Mesh stack when events occur.
 *
 * THREE MAIN CALLBACK TYPES:
 * --------------------------
 * 1. Provisioning callbacks (mesh_provisioning_cb):
 *    - Handle device discovery, provisioning links, and provisioning completion
 *
 * 2. Configuration Client callbacks (mesh_config_client_cb):
 *    - Handle node configuration after provisioning (AppKey add, model binding)
 *
 * 3. Model Client callbacks (mesh_generic_client_cb, mesh_sensor_client_cb):
 *    - Handle model-specific interactions (OnOff commands, sensor data)
 *
 * EVENT FLOW EXAMPLE (Full Provisioning + Configuration):
 * --------------------------------------------------------
 * 1. ESP_BLE_MESH_PROVISIONER_RECV_UNPROV_ADV_PKT_EVT
 *    → Unprovisioned device found via beacon
 *    → recv_unprov_adv_pkt() adds device to provisioning queue
 *
 * 2. ESP_BLE_MESH_PROVISIONER_PROV_LINK_OPEN_EVT
 *    → Provisioning bearer (PB-ADV or PB-GATT) established
 *    → Secure channel created for provisioning data exchange
 *
 * 3. ESP_BLE_MESH_PROVISIONER_PROV_COMPLETE_EVT
 *    → Device successfully provisioned (has NetKey and unicast address)
 *    → prov_complete() stores node info and requests composition data
 *
 * 4. ESP_BLE_MESH_CFG_CLIENT_GET_STATE_EVT (Composition Data)
 *    → Node responds with its capabilities (elements, models)
 *    → Provisioner sends AppKey to the node
 *
 * 5. ESP_BLE_MESH_CFG_CLIENT_SET_STATE_EVT (AppKey Add)
 *    → Node confirms AppKey received
 *    → Provisioner binds AppKey to Generic OnOff Server model
 *
 * 6. ESP_BLE_MESH_CFG_CLIENT_SET_STATE_EVT (Model App Bind)
 *    → Model is now bound to AppKey, can receive OnOff commands
 *    → Provisioner queries current OnOff state
 *
 * 7. ESP_BLE_MESH_GENERIC_CLIENT_GET_STATE_EVT (OnOff Status)
 *    → Node responds with current state (ON/OFF)
 *    → Configuration complete! Node is fully operational
 */

#include "ble_mesh_callbacks.h"
#include "ble_mesh_storage.h"
#include "ble_mesh_auto_config.h"
#include "esp_log.h"
#include "esp_ble_mesh_networking_api.h"
#include "mesh/utils.h"
#include <string.h>
#include <inttypes.h>

#define TAG "MESH_CB"

/*
 * LED STATE DEFINITIONS
 * ---------------------
 * These represent the binary on/off state used by Generic OnOff model
 */
#define LED_OFF             0x0  // Generic OnOff OFF state
#define LED_ON              0x1  // Generic OnOff ON state

/*
 * MESSAGE PARAMETERS
 * ------------------
 * TTL (Time To Live): Maximum number of hops a message can take
 *   - Each relay decrements TTL by 1
 *   - Message is dropped when TTL reaches 0
 *   - Value of 3 means message can hop through 3 intermediate nodes
 *   - Prevents infinite loops in mesh network
 *
 * Timeout: How long to wait for response (0 = use default timeout)
 *   - For acknowledged messages, this is max wait time for response
 *   - 0 means use stack's default timeout (typically 4 seconds)
 */
#define MSG_SEND_TTL        3    // Max 3 hops for messages
#define MSG_TIMEOUT         0    // Use default timeout

/*
 * COMPOSITION DATA
 * ----------------
 * Page 0 contains basic device information:
 *   - Company ID (CID)
 *   - Product ID (PID)
 *   - Version ID (VID)
 *   - Number of elements
 *   - List of models for each element
 *
 * This is the first thing we request after provisioning to understand
 * what the device can do (what models it supports)
 */
#define COMP_DATA_PAGE_0    0x00

/*
 * PROVISIONER ADDRESS
 * -------------------
 * The provisioner's own unicast address (must match config in provisioner_init)
 * Used when binding AppKey to our own models
 */
#define PROV_OWN_ADDR       0x0001

/*
 * EXTERNAL REFERENCES
 * -------------------
 * These are defined and initialized in ble_mesh_provisioner.c
 * We need access to them to send configuration and control messages
 *
 * - config_client: Configuration Client model instance
 *   Used to configure newly provisioned nodes (add keys, bind models)
 *
 * - onoff_client: Generic OnOff Client model instance
 *   Used to control Generic OnOff Server models on nodes (turn lights on/off)
 *
 * - prov_key: Network and application key information
 *   Contains the encryption keys used for all mesh communication
 */
extern esp_ble_mesh_client_t config_client;
extern esp_ble_mesh_client_t onoff_client;
extern struct esp_ble_mesh_key prov_key;

// Forward declarations for weak functions that can be overridden
void provisioner_vendor_msg_handler(uint16_t src_addr, uint32_t opcode, uint8_t *data, uint16_t length);
void provisioner_sensor_msg_handler(uint16_t src_addr, uint16_t property_id, int32_t value);

/*
 * COMPOSITION DATA PARSER
 * =======================
 *
 * Parses composition data to extract list of models present in the node.
 * Composition data format (BLE Mesh spec section 4.2.1):
 *
 * Page 0 Composition Data:
 *   [CID (2)] [PID (2)] [VID (2)] [CRPL (2)] [Features (2)]
 *   For each element:
 *     [Loc (2)] [NumS (1)] [NumV (1)] [SIG Model IDs...] [Vendor Model IDs...]
 *
 * Returns: Number of models found (both SIG and vendor)
 */
typedef struct {
    uint16_t model_id;
    uint16_t company_id;  // ESP_BLE_MESH_CID_NVAL for SIG models
    bool is_vendor;
} discovered_model_t;

#define MAX_DISCOVERED_MODELS 16

static int parse_composition_data(struct net_buf_simple *buf,
                                   discovered_model_t *models,
                                   int max_models)
{
    int model_count = 0;

    if (!buf || buf->len < 10) {
        ESP_LOGW(TAG, "Composition data too short: %d bytes", buf ? buf->len : 0);
        return 0;
    }

    // Skip: CID(2) + PID(2) + VID(2) + CRPL(2) + Features(2) = 10 bytes
    net_buf_simple_pull(buf, 10);

    // Parse elements (typically just one element in our nodes)
    while (buf->len >= 4 && model_count < max_models) {
        // Skip Location (2 bytes)
        net_buf_simple_pull(buf, 2);

        // Read number of SIG models and vendor models
        uint8_t num_sig = net_buf_simple_pull_u8(buf);
        uint8_t num_vnd = net_buf_simple_pull_u8(buf);

        ESP_LOGI(TAG, "  Element has %d SIG models, %d vendor models", num_sig, num_vnd);

        // Read SIG model IDs (2 bytes each)
        for (int i = 0; i < num_sig && model_count < max_models && buf->len >= 2; i++) {
            uint16_t model_id = net_buf_simple_pull_le16(buf);
            models[model_count].model_id = model_id;
            models[model_count].company_id = ESP_BLE_MESH_CID_NVAL;
            models[model_count].is_vendor = false;
            ESP_LOGI(TAG, "    SIG model [%d]: 0x%04x", model_count, model_id);
            model_count++;
        }

        // Read vendor model IDs (4 bytes each: CID(2) + MID(2))
        for (int i = 0; i < num_vnd && model_count < max_models && buf->len >= 4; i++) {
            uint16_t company_id = net_buf_simple_pull_le16(buf);
            uint16_t model_id = net_buf_simple_pull_le16(buf);
            models[model_count].model_id = model_id;
            models[model_count].company_id = company_id;
            models[model_count].is_vendor = true;
            ESP_LOGI(TAG, "    Vendor model [%d]: CID=0x%04x MID=0x%04x",
                     model_count, company_id, model_id);
            model_count++;
        }

        // If there are more elements, continue parsing
        // (but our nodes typically have only 1 element)
    }

    return model_count;
}

/*
 * HELPER FUNCTION: Set Common Message Parameters
 * ===============================================
 *
 * WHAT THIS DOES:
 * This function fills in the common parameters needed for ALL client messages
 * in BLE Mesh. Every time we send a message (configuration or control), we need
 * to specify where it's going, what operation to perform, and which keys to use.
 *
 * WHY IT EXISTS:
 * Instead of repeating this setup code every time we send a message, we use
 * this helper function. This follows the DRY principle (Don't Repeat Yourself).
 *
 * PARAMETERS BEING SET:
 * - opcode: What operation to perform (e.g., "get OnOff state", "add AppKey")
 * - model: Which client model is sending the message
 * - ctx.net_idx: Which network key to use for encryption (network layer)
 * - ctx.app_idx: Which application key to use for encryption (application layer)
 * - ctx.addr: Destination address (which node to send to)
 * - ctx.send_ttl: Maximum hops the message can take (prevents loops)
 * - msg_timeout: How long to wait for a response (0 = use default)
 *
 * EXAMPLE USAGE:
 * To send "Get OnOff state" to node at address 0x0005:
 *   mesh_set_msg_common(&common, 0x0005, onoff_client.model,
 *                       ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_GET);
 */
static esp_err_t mesh_set_msg_common(esp_ble_mesh_client_common_param_t *common,
                                     uint16_t unicast,
                                     esp_ble_mesh_model_t *model, uint32_t opcode)
{
    if (!common || !model) {
        return ESP_ERR_INVALID_ARG;
    }

    common->opcode = opcode;                        // What operation to perform
    common->model = model;                          // Which model is sending
    common->ctx.net_idx = prov_key.net_idx;        // Network key (subnet)
    common->ctx.app_idx = prov_key.app_idx;        // Application key (access control)
    common->ctx.addr = unicast;                     // Destination address
    common->ctx.send_ttl = MSG_SEND_TTL;           // Max hops (prevents loops)
    common->msg_timeout = MSG_TIMEOUT;              // Response timeout (0=default)

    return ESP_OK;
}

/*
 * PROVISIONING COMPLETE HANDLER
 * ==============================
 *
 * This function is called when a device has been successfully provisioned.
 * At this point, the device has received:
 *   - Network Key (NetKey) for encrypted communication
 *   - Unicast address (its unique identifier in the mesh)
 *   - IV Index (for replay protection)
 *
 * But the device is NOT yet usable! We still need to configure it by:
 *   1. Getting its composition data (what models it has)
 *   2. Adding application keys (for model-specific encryption)
 *   3. Binding models to application keys (authorizing them)
 *
 * WHAT THIS FUNCTION DOES:
 * Step 1: Assign a friendly name to the node (e.g., "NODE-0", "NODE-1")
 * Step 2: Store the node's information (UUID, address, element count)
 * Step 3: Request composition data to start the configuration process
 *
 * PARAMETERS:
 * @param node_idx - Internal index assigned by the mesh stack
 * @param uuid - Device's unique 16-byte identifier
 * @param unicast - Unicast address assigned to this node (e.g., 0x0005)
 * @param elem_num - Number of elements in the node (usually 1)
 * @param net_idx - Network key index used for this node
 *
 * NEXT STEP:
 * After this function sends the composition data request, the response
 * will trigger ESP_BLE_MESH_CFG_CLIENT_GET_STATE_EVT in mesh_config_client_cb()
 */
static esp_err_t prov_complete(int node_idx, const esp_ble_mesh_octet16_t uuid,
                               uint16_t unicast, uint8_t elem_num, uint16_t net_idx)
{
    esp_ble_mesh_client_common_param_t common = {0};
    esp_ble_mesh_cfg_client_get_state_t get_state = {0};
    char name[11] = {0};
    int err;

    ESP_LOGI(TAG, "node index: 0x%x, unicast address: 0x%02x, element num: %d, netkey index: 0x%02x",
             node_idx, unicast, elem_num, net_idx);
    ESP_LOGI(TAG, "device uuid: %s", bt_hex(uuid, 16));

    // Step 1: Assign human-readable name to the node
    sprintf(name, "%s%d", "NODE-", node_idx);
    err = esp_ble_mesh_provisioner_set_node_name(node_idx, name);
    if (err) {
        ESP_LOGE(TAG, "Set node name failed");
        return ESP_FAIL;
    }

    // Step 2: Store node information for later use
    // We store UUID (for identification), unicast (for addressing),
    // elem_num (number of addressable entities), and initial LED state
    err = mesh_storage_add_node(uuid, unicast, elem_num, LED_OFF);
    if (err) {
        ESP_LOGE(TAG, "Store node info failed");
        return ESP_FAIL;
    }

    // Step 3: Request composition data (what models/capabilities does this node have?)
    // This is the first configuration step - we need to know what the device
    // can do before we can configure it properly
    mesh_set_msg_common(&common, unicast, config_client.model, ESP_BLE_MESH_MODEL_OP_COMPOSITION_DATA_GET);
    get_state.comp_data_get.page = COMP_DATA_PAGE_0;  // Page 0 = basic device info
    err = esp_ble_mesh_config_client_get_state(&common, &get_state);
    if (err) {
        ESP_LOGE(TAG, "Send config comp data get failed");
        return ESP_FAIL;
    }

    return ESP_OK;
}

/*
 * PROVISIONING LINK OPENED
 * ========================
 *
 * Called when a provisioning bearer (communication channel) is established
 * with an unprovisioned device. This happens BEFORE actual provisioning data
 * is exchanged.
 *
 * TWO TYPES OF BEARERS:
 * - PB-ADV (Provisioning over Advertising):
 *   Uses BLE advertisements, works without connection, can provision
 *   multiple devices simultaneously
 *
 * - PB-GATT (Provisioning over GATT):
 *   Uses BLE connection, more reliable, but one device at a time
 *
 * This is just informational - the mesh stack handles the provisioning
 * automatically after this link is established.
 */
static void prov_link_open(esp_ble_mesh_prov_bearer_t bearer)
{
    ESP_LOGI(TAG, "%s link open", bearer == ESP_BLE_MESH_PROV_ADV ? "PB-ADV" : "PB-GATT");
}

/*
 * PROVISIONING LINK CLOSED
 * =========================
 *
 * Called when the provisioning bearer closes. This can happen for various reasons:
 * - Provisioning completed successfully (normal case)
 * - Timeout (device didn't respond)
 * - Error during provisioning
 * - Device moved out of range
 *
 * The reason code indicates why the link closed:
 * - 0x00: Success (provisioning completed)
 * - 0x01: Timeout
 * - 0x02: Invalid PDU
 * - etc. (see BLE Mesh spec for full list)
 */
static void prov_link_close(esp_ble_mesh_prov_bearer_t bearer, uint8_t reason)
{
    ESP_LOGI(TAG, "%s link close, reason 0x%02x",
             bearer == ESP_BLE_MESH_PROV_ADV ? "PB-ADV" : "PB-GATT", reason);
}

/*
 * UNPROVISIONED DEVICE DISCOVERED
 * ================================
 *
 * This function is called when the provisioner detects an unprovisioned device
 * broadcasting its "Unprovisioned Device Beacon". This beacon contains:
 *   - Device UUID (unique identifier)
 *   - OOB (Out-of-Band) information (what authentication methods it supports)
 *   - BLE MAC address
 *
 * WHAT IS OOB?
 * OOB provides extra security during provisioning:
 *   - Static OOB: Pre-shared secret (e.g., printed on device)
 *   - Output OOB: Device displays a code (e.g., LED blinks)
 *   - Input OOB: User enters a code on the device
 *   - No OOB (0x0000): Just works mode (less secure, used here)
 *
 * WHAT THIS FUNCTION DOES:
 * 1. Logs the discovered device information
 * 2. Adds the device to the provisioning queue
 * 3. Sets flags to:
 *    - Start provisioning immediately (ADD_DEV_START_PROV_NOW_FLAG)
 *    - Remove from queue after provisioning (ADD_DEV_RM_AFTER_PROV_FLAG)
 *    - Allow flushing if queue is full (ADD_DEV_FLUSHABLE_DEV_FLAG)
 *
 * NEXT STEP:
 * After adding the device, the mesh stack will automatically:
 * 1. Establish a provisioning link (triggers prov_link_open)
 * 2. Exchange provisioning data
 * 3. Provision the device (triggers prov_complete)
 */
static void recv_unprov_adv_pkt(uint8_t dev_uuid[16], uint8_t addr[BD_ADDR_LEN],
                                esp_ble_mesh_addr_type_t addr_type, uint16_t oob_info,
                                uint8_t adv_type, esp_ble_mesh_prov_bearer_t bearer)
{
    esp_ble_mesh_unprov_dev_add_t add_dev = {0};
    int err;

    ESP_LOGI(TAG, "address: %s, address type: %d, adv type: %d", bt_hex(addr, BD_ADDR_LEN), addr_type, adv_type);
    ESP_LOGI(TAG, "device uuid: %s", bt_hex(dev_uuid, 16));
    ESP_LOGI(TAG, "oob info: %d, bearer: %s", oob_info, (bearer & ESP_BLE_MESH_PROV_ADV) ? "PB-ADV" : "PB-GATT");

    // Fill device information
    memcpy(add_dev.addr, addr, BD_ADDR_LEN);
    add_dev.addr_type = (esp_ble_mesh_addr_type_t)addr_type;
    memcpy(add_dev.uuid, dev_uuid, 16);
    add_dev.oob_info = oob_info;
    add_dev.bearer = (esp_ble_mesh_prov_bearer_t)bearer;

    // Add device to provisioning queue with these flags:
    // - ADD_DEV_RM_AFTER_PROV_FLAG: Remove from queue after provisioning
    // - ADD_DEV_START_PROV_NOW_FLAG: Start provisioning immediately (don't wait)
    // - ADD_DEV_FLUSHABLE_DEV_FLAG: Can be flushed if queue is full
    err = esp_ble_mesh_provisioner_add_unprov_dev(&add_dev,
            (esp_ble_mesh_dev_add_flag_t)(ADD_DEV_RM_AFTER_PROV_FLAG | ADD_DEV_START_PROV_NOW_FLAG | ADD_DEV_FLUSHABLE_DEV_FLAG));
    if (err) {
        ESP_LOGE(TAG, "Add unprovisioned device into queue failed");
    }
}

void mesh_provisioning_cb(esp_ble_mesh_prov_cb_event_t event,
                          esp_ble_mesh_prov_cb_param_t *param)
{
    switch (event) {
    case ESP_BLE_MESH_PROVISIONER_PROV_ENABLE_COMP_EVT:
        ESP_LOGI(TAG, "Provisioner enabled, err_code %d", param->provisioner_prov_enable_comp.err_code);
        break;
    case ESP_BLE_MESH_PROVISIONER_PROV_DISABLE_COMP_EVT:
        ESP_LOGI(TAG, "Provisioner disabled, err_code %d", param->provisioner_prov_disable_comp.err_code);
        break;
    case ESP_BLE_MESH_PROVISIONER_RECV_UNPROV_ADV_PKT_EVT:
        ESP_LOGI(TAG, "Unprovisioned device found");
        recv_unprov_adv_pkt(param->provisioner_recv_unprov_adv_pkt.dev_uuid, param->provisioner_recv_unprov_adv_pkt.addr,
                            param->provisioner_recv_unprov_adv_pkt.addr_type, param->provisioner_recv_unprov_adv_pkt.oob_info,
                            param->provisioner_recv_unprov_adv_pkt.adv_type, param->provisioner_recv_unprov_adv_pkt.bearer);
        break;
    case ESP_BLE_MESH_PROVISIONER_PROV_LINK_OPEN_EVT:
        prov_link_open(param->provisioner_prov_link_open.bearer);
        break;
    case ESP_BLE_MESH_PROVISIONER_PROV_LINK_CLOSE_EVT:
        prov_link_close(param->provisioner_prov_link_close.bearer, param->provisioner_prov_link_close.reason);
        break;
    case ESP_BLE_MESH_PROVISIONER_PROV_COMPLETE_EVT:
        prov_complete(param->provisioner_prov_complete.node_idx, param->provisioner_prov_complete.device_uuid,
                      param->provisioner_prov_complete.unicast_addr, param->provisioner_prov_complete.element_num,
                      param->provisioner_prov_complete.netkey_idx);
        break;
    case ESP_BLE_MESH_PROVISIONER_ADD_UNPROV_DEV_COMP_EVT:
        ESP_LOGI(TAG, "Add unprov device complete, err_code %d", param->provisioner_add_unprov_dev_comp.err_code);
        break;
    case ESP_BLE_MESH_PROVISIONER_SET_DEV_UUID_MATCH_COMP_EVT:
        ESP_LOGI(TAG, "Set dev UUID match complete, err_code %d", param->provisioner_set_dev_uuid_match_comp.err_code);
        break;
    case ESP_BLE_MESH_PROVISIONER_SET_NODE_NAME_COMP_EVT:
        ESP_LOGI(TAG, "Set node name complete, err_code %d", param->provisioner_set_node_name_comp.err_code);
        if (param->provisioner_set_node_name_comp.err_code == ESP_OK) {
            const char *name = esp_ble_mesh_provisioner_get_node_name(param->provisioner_set_node_name_comp.node_index);
            if (name) {
                ESP_LOGI(TAG, "Node %d name is: %s", param->provisioner_set_node_name_comp.node_index, name);
            }
        }
        break;
    case ESP_BLE_MESH_PROVISIONER_ADD_LOCAL_APP_KEY_COMP_EVT:
        ESP_LOGI(TAG, "Add local app key complete, err_code %d", param->provisioner_add_app_key_comp.err_code);
        if (param->provisioner_add_app_key_comp.err_code == ESP_OK) {
            esp_err_t err;
            prov_key.app_idx = param->provisioner_add_app_key_comp.app_idx;

            // Bind Generic OnOff Client model
            err = esp_ble_mesh_provisioner_bind_app_key_to_local_model(PROV_OWN_ADDR, prov_key.app_idx,
                    ESP_BLE_MESH_MODEL_ID_GEN_ONOFF_CLI, ESP_BLE_MESH_CID_NVAL);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Provisioner bind Generic OnOff Client failed");
            }

            // Bind Sensor Client model to receive sensor data from nodes
            err = esp_ble_mesh_provisioner_bind_app_key_to_local_model(PROV_OWN_ADDR, prov_key.app_idx,
                    ESP_BLE_MESH_MODEL_ID_SENSOR_CLI, ESP_BLE_MESH_CID_NVAL);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Provisioner bind Sensor Client failed");
            }

            // Bind Vendor Client model to receive bulk IMU data
            err = esp_ble_mesh_provisioner_bind_app_key_to_local_model(PROV_OWN_ADDR, prov_key.app_idx,
                    0x0000, 0x0001);  // Vendor model: model_id=0x0000 (CLIENT), company_id=0x0001
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Provisioner bind Vendor Client failed");
            }
        }
        break;
    case ESP_BLE_MESH_PROVISIONER_BIND_APP_KEY_TO_MODEL_COMP_EVT:
        ESP_LOGI(TAG, "Bind app key to model complete, err_code %d", param->provisioner_bind_app_key_to_model_comp.err_code);
        break;
    default:
        break;
    }
}

void mesh_config_client_cb(esp_ble_mesh_cfg_client_cb_event_t event,
                           esp_ble_mesh_cfg_client_cb_param_t *param)
{
    esp_ble_mesh_client_common_param_t common = {0};
    mesh_node_info_t node_info;
    uint32_t opcode;
    uint16_t addr;
    int err;

    opcode = param->params->opcode;
    addr = param->params->ctx.addr;

    ESP_LOGI(TAG, "Config client event %d, addr: 0x%04x, opcode: 0x%04" PRIx32,
             event, addr, opcode);

    if (param->error_code) {
        ESP_LOGE(TAG, "Send config client message failed, opcode 0x%04" PRIx32, opcode);
        return;
    }

    err = mesh_storage_get_node(addr, &node_info);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Get node info failed");
        return;
    }

    switch (event) {
    case ESP_BLE_MESH_CFG_CLIENT_GET_STATE_EVT:
        if (opcode == ESP_BLE_MESH_MODEL_OP_COMPOSITION_DATA_GET) {
            ESP_LOGI(TAG, "📋 Composition data received from 0x%04x", addr);

            // Parse composition data to discover all models
            struct net_buf_simple *comp_data = param->status_cb.comp_data_status.composition_data;

            // Make a copy because parse function consumes the buffer
            struct net_buf_simple comp_data_copy;
            net_buf_simple_clone(comp_data, &comp_data_copy);

            discovered_model_t discovered[MAX_DISCOVERED_MODELS];
            int discovered_count = parse_composition_data(&comp_data_copy, discovered, MAX_DISCOVERED_MODELS);

            ESP_LOGI(TAG, "  Discovered %d models total", discovered_count);

            // Store discovered models in node storage
            node_info.model_count = discovered_count;
            for (int i = 0; i < discovered_count; i++) {
                node_info.models[i].model_id = discovered[i].model_id;
                node_info.models[i].company_id = discovered[i].company_id;
                node_info.models[i].is_vendor = discovered[i].is_vendor;
                node_info.models[i].appkey_bound = false;
                node_info.models[i].pub_configured = false;
            }
            node_info.composition_received = true;
            node_info.appkey_added = false;
            node_info.next_model_to_bind = 0;
            node_info.next_model_to_pub = 0;

            // Update storage with discovered models
            err = mesh_storage_update_node(addr, &node_info);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to update node storage");
            }

            // Add AppKey (needed for all model communication)
            esp_ble_mesh_cfg_client_set_state_t set_state = {0};
            mesh_set_msg_common(&common, addr, config_client.model, ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD);
            set_state.app_key_add.net_idx = prov_key.net_idx;
            set_state.app_key_add.app_idx = prov_key.app_idx;
            memcpy(set_state.app_key_add.app_key, prov_key.app_key, 16);
            err = esp_ble_mesh_config_client_set_state(&common, &set_state);
            if (err) {
                ESP_LOGE(TAG, "Config AppKey Add failed");
            }
        }
        break;
    case ESP_BLE_MESH_CFG_CLIENT_SET_STATE_EVT:
        if (opcode == ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD) {
            ESP_LOGI(TAG, "✅ AppKey added - starting automatic model binding");

            node_info.appkey_added = true;
            mesh_storage_update_node(addr, &node_info);

            // Start binding models automatically
            mesh_set_msg_common(&common, addr, config_client.model, ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND);
            if (!bind_next_model(addr, &node_info, &common, &prov_key)) {
                ESP_LOGI(TAG, "No models to bind (unexpected)");
            } else {
                mesh_storage_update_node(addr, &node_info);
            }
        } else if (opcode == ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND) {
            /*
             * ════════════════════════════════════════════════════════════════
             *                    AUTOMATIC MODEL BINDING
             * ════════════════════════════════════════════════════════════════
             *
             * This handler processes model binding responses and continues
             * binding remaining models sequentially until all are bound.
             *
             * Once all models are bound, it automatically transitions to
             * configuring publication for server models.
             */
            uint16_t bound_model_id = param->status_cb.model_app_status.model_id;
            uint16_t bound_company_id = param->status_cb.model_app_status.company_id;

            ESP_LOGI(TAG, "✅ Model bound: 0x%04x (CID=0x%04x)", bound_model_id, bound_company_id);

            // Mark this model as bound in storage
            for (int i = 0; i < node_info.model_count; i++) {
                if (node_info.models[i].model_id == bound_model_id &&
                    node_info.models[i].company_id == bound_company_id) {
                    node_info.models[i].appkey_bound = true;
                    break;
                }
            }
            node_info.next_model_to_bind++;
            mesh_storage_update_node(addr, &node_info);

            // Try to bind next model
            mesh_set_msg_common(&common, addr, config_client.model, ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND);
            if (!bind_next_model(addr, &node_info, &common, &prov_key)) {
                // All models bound! Start publication configuration
                ESP_LOGI(TAG, "🔧 All models bound - configuring publications");
                mesh_set_msg_common(&common, addr, config_client.model, ESP_BLE_MESH_MODEL_OP_MODEL_PUB_SET);
                if (!configure_next_publication(addr, &node_info, &common, &prov_key)) {
                    ESP_LOGI(TAG, "🎉 Node fully configured!");
                } else {
                    mesh_storage_update_node(addr, &node_info);
                }
            } else {
                mesh_storage_update_node(addr, &node_info);
            }
        } else if (opcode == ESP_BLE_MESH_MODEL_OP_MODEL_PUB_SET) {
            /*
             * ════════════════════════════════════════════════════════════════
             *              AUTOMATIC PUBLICATION CONFIGURATION
             * ════════════════════════════════════════════════════════════════
             *
             * This handler processes publication configuration responses and
             * continues configuring remaining server models sequentially.
             *
             * Publication address is set to 0x0001 (provisioner) so all
             * published messages are sent to the gateway.
             */
            uint16_t pub_model_id = param->status_cb.model_pub_status.model_id;
            uint16_t pub_company_id = param->status_cb.model_pub_status.company_id;

            ESP_LOGI(TAG, "✅ Publication configured: 0x%04x (CID=0x%04x)", pub_model_id, pub_company_id);

            // Mark this model's publication as configured
            for (int i = 0; i < node_info.model_count; i++) {
                if (node_info.models[i].model_id == pub_model_id &&
                    node_info.models[i].company_id == pub_company_id) {
                    node_info.models[i].pub_configured = true;
                    break;
                }
            }
            node_info.next_model_to_pub++;
            mesh_storage_update_node(addr, &node_info);

            // Configure next publication
            mesh_set_msg_common(&common, addr, config_client.model, ESP_BLE_MESH_MODEL_OP_MODEL_PUB_SET);
            if (!configure_next_publication(addr, &node_info, &common, &prov_key)) {
                ESP_LOGI(TAG, "🎉 Node fully configured and ready!");
            } else {
                mesh_storage_update_node(addr, &node_info);
            }
        }
        break;
    case ESP_BLE_MESH_CFG_CLIENT_TIMEOUT_EVT:
        ESP_LOGW(TAG, "Config client timeout, opcode 0x%04" PRIx32, opcode);
        // Retry logic could be added here
        break;
    default:
        break;
    }
}

void mesh_generic_client_cb(esp_ble_mesh_generic_client_cb_event_t event,
                            esp_ble_mesh_generic_client_cb_param_t *param)
{
    esp_ble_mesh_client_common_param_t common = {0};
    mesh_node_info_t node_info;
    uint32_t opcode;
    uint16_t addr;
    int err;

    opcode = param->params->opcode;
    addr = param->params->ctx.addr;

    ESP_LOGI(TAG, "Generic client event %d, addr: 0x%04x, opcode: 0x%04" PRIx32,
             event, addr, opcode);

    if (param->error_code) {
        ESP_LOGE(TAG, "Send generic client message failed, opcode 0x%04" PRIx32, opcode);
        return;
    }

    err = mesh_storage_get_node(addr, &node_info);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Get node info failed");
        return;
    }

    switch (event) {
    case ESP_BLE_MESH_GENERIC_CLIENT_GET_STATE_EVT:
        if (opcode == ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_GET) {
            uint8_t onoff = param->status_cb.onoff_status.present_onoff;
            ESP_LOGI(TAG, "OnOff state: 0x%02x", onoff);

            // Toggle the state
            esp_ble_mesh_generic_client_set_state_t set_state = {0};
            mesh_set_msg_common(&common, addr, onoff_client.model, ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET);
            set_state.onoff_set.op_en = false;
            set_state.onoff_set.onoff = !onoff;
            set_state.onoff_set.tid = 0;
            err = esp_ble_mesh_generic_client_set_state(&common, &set_state);
            if (err) {
                ESP_LOGE(TAG, "Generic OnOff Set failed");
            }
        }
        break;
    case ESP_BLE_MESH_GENERIC_CLIENT_SET_STATE_EVT:
        if (opcode == ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET) {
            uint8_t onoff = param->status_cb.onoff_status.present_onoff;
            ESP_LOGI(TAG, "OnOff set to: 0x%02x", onoff);
        }
        break;
    case ESP_BLE_MESH_GENERIC_CLIENT_TIMEOUT_EVT:
        ESP_LOGW(TAG, "Generic client timeout, opcode 0x%04" PRIx32, opcode);
        break;
    default:
        break;
    }
}

void mesh_sensor_client_cb(esp_ble_mesh_sensor_client_cb_event_t event,
                            esp_ble_mesh_sensor_client_cb_param_t *param)
{
    uint32_t opcode;
    uint16_t addr;

    opcode = param->params->opcode;
    addr = param->params->ctx.addr;

    ESP_LOGI(TAG, "📊 Sensor client event %d, addr: 0x%04x, opcode: 0x%04" PRIx32,
             event, addr, opcode);

    if (param->error_code) {
        ESP_LOGE(TAG, "Sensor client error, opcode 0x%04" PRIx32, opcode);
        return;
    }

    switch (event) {
    case ESP_BLE_MESH_SENSOR_CLIENT_GET_STATE_EVT:
        ESP_LOGI(TAG, "Sensor Get State event");
        break;

    case ESP_BLE_MESH_SENSOR_CLIENT_SET_STATE_EVT:
        ESP_LOGI(TAG, "Sensor Set State event");
        break;

    case ESP_BLE_MESH_SENSOR_CLIENT_PUBLISH_EVT:
        /*
         * ════════════════════════════════════════════════════════════════════════
         *                    SENSOR DATA RECEPTION AND PARSING
         * ════════════════════════════════════════════════════════════════════════
         *
         * This event fires when a node PUBLISHES sensor data to the mesh network.
         *
         * BLE MESH PUBLISH vs UNICAST:
         * ----------------------------
         * - PUBLISH: Node broadcasts data to a publish address (e.g., 0x0001)
         *   - Multiple devices can subscribe to the same publish address
         *   - This is how sensors share data efficiently
         *   - One transmission reaches all subscribers
         *
         * - UNICAST: Direct message to a specific device address
         *   - Used for commands and queries (e.g., "Get sensor value")
         *   - Requires separate transmission for each recipient
         *
         * WHY THIS IS A "PUBLISH" EVENT:
         * ------------------------------
         * Even though we configured the node's publication address to point to us
         * (the provisioner at 0x0001), the mesh stack treats this as a publication
         * because the node used esp_ble_mesh_model_publish() or similar API.
         * The sensor is "broadcasting" its state, not responding to a query.
         *
         * SENSOR STATUS MESSAGE FORMAT:
         * ------------------------------
         * The BLE Mesh spec defines a special encoding called MPID (Marshalled
         * Property ID) that packs sensor metadata efficiently:
         *
         *   [Opcode: 0x52] [MPID Header] [Sensor Data]
         *        ↑              ↑              ↑
         *    1-3 bytes      2-3 bytes      variable
         *
         * The opcode (0x52 = SENSOR_STATUS) is handled by the mesh stack and
         * NOT included in the buffer we receive. We only get [MPID + Data].
         */
        if (opcode == ESP_BLE_MESH_MODEL_OP_SENSOR_STATUS) {
            ESP_LOGI(TAG, "📡 Received Sensor Status from 0x%04x", addr);

            /*
             * THE MARSHALLED SENSOR DATA BUFFER
             * ==================================
             *
             * This buffer contains the sensor data in BLE Mesh's compact format.
             * The term "marshalled" means the data has been packed/serialized
             * according to the BLE Mesh Sensor specification.
             *
             * BUFFER STRUCTURE:
             * -----------------
             * buf->data = pointer to start of data
             * buf->len  = total bytes available
             *
             * The buffer uses "net_buf_simple" which is ESP-IDF's network buffer
             * abstraction. It has an internal "cursor" that moves as we read:
             *
             *   Initial:  [MPID|DATA............]
             *                ↑ cursor at start
             *
             *   After reading MPID: [MPID|DATA............]
             *                             ↑ cursor moved forward
             */
            struct net_buf_simple *buf = param->status_cb.sensor_status.marshalled_sensor_data;
            if (buf && buf->len >= 1) {
                // Debug: dump raw buffer (helps understand the encoding)
                ESP_LOGI(TAG, "  Raw buffer len=%d:", buf->len);
                ESP_LOG_BUFFER_HEX(TAG, buf->data, buf->len);

                /*
                 * ════════════════════════════════════════════════════════════════
                 *          MPID FORMAT DETECTION (Format A vs Format B)
                 * ════════════════════════════════════════════════════════════════
                 *
                 * BLE Mesh defines TWO encoding formats for sensor property IDs:
                 *
                 * WHY TWO FORMATS?
                 * ----------------
                 * - Format A is compact (2 bytes) but limited to property IDs ≤ 0x07FF
                 * - Format B is larger (3 bytes) but supports ALL property IDs (0x0000-0xFFFF)
                 *
                 * The first byte's LSB (bit 0) tells us which format:
                 *   Bit 0 = 0 → Format A (2-byte header)
                 *   Bit 0 = 1 → Format B (3-byte header)
                 *
                 * CRITICAL: We must "PEEK" at the byte, not consume it yet!
                 * ---------------------------------------------------------
                 * We use buf->data[0] to READ the byte without moving the cursor.
                 * If we used net_buf_simple_pull_u8(), the byte would be CONSUMED
                 * and we couldn't read it again. This caused the original bug!
                 */
                uint8_t first_byte = buf->data[0];  // PEEK, don't consume
                uint8_t format = first_byte & 0x01;  // Extract bit 0

                uint16_t property_id;
                uint8_t data_length;

                if (format == 0) {
                    /*
                     * ════════════════════════════════════════════════════════════════
                     *                   FORMAT A: COMPACT 2-BYTE MPID
                     * ════════════════════════════════════════════════════════════════
                     *
                     * Used for property IDs 0x0000 - 0x07FF (most common sensors)
                     *
                     * BIT LAYOUT (16 bits total, little-endian):
                     * ──────────────────────────────────────────────────────────────
                     *
                     *  Byte 0:  [P P P P | L L L L]    Byte 1:  [0 P P P P P P P]
                     *            ↑ ↑ ↑ ↑   ↑ ↑ ↑ ↑                ↑ ↑ ↑ ↑ ↑ ↑ ↑ ↑
                     *         Prop[3:0] Len[3:0]              Fmt Prop[10:4]
                     *
                     * Where:
                     *   Fmt (bit 0)     = 0 (indicates Format A)
                     *   Len (bits 1-4)  = Length field (4 bits = 0-15)
                     *   Prop (bits 5-15)= Property ID (11 bits = 0-2047)
                     *
                     * LENGTH ENCODING:
                     * ----------------
                     * The 4-bit length field can represent:
                     *   0x0-0xE: Actual length = length_field + 1 (1-15 bytes)
                     *   0xF:     Length is encoded IN the sensor data (variable)
                     *
                     * EXAMPLE: Temperature sensor (property ID = 0x004F, 2 bytes data)
                     * ──────────────────────────────────────────────────────────────
                     *   Property ID = 0x004F = 0b00001001111
                     *   Data length = 2 bytes → length_field = 1 (because 1+1=2)
                     *   Format bit = 0
                     *
                     *   MPID bits:  [00001001111][0001][0] = 0x0272
                     *   Wire bytes: 0x72 0x02 (little-endian)
                     *
                     * EXAMPLE: Accelerometer X (property ID = 0x5001, 4 bytes) - TOO LARGE!
                     * ──────────────────────────────────────────────────────────────
                     *   Property ID = 0x5001 = 0b101 0000 0000 0001
                     *                            ↑ This is 13 bits - DOESN'T FIT!
                     *
                     *   Format A only supports 11-bit property IDs (0x000-0x7FF)
                     *   We MUST use Format B for 0x5001!
                     */
                    if (buf->len >= 2) {
                        // Read 2 bytes in little-endian (ESP32 is little-endian)
                        uint16_t mpid = (buf->data[1] << 8) | buf->data[0];

                        // Extract fields using bit masking and shifting
                        uint8_t length_field = (mpid >> 1) & 0x0F;   // Bits 1-4
                        property_id = (mpid >> 5) & 0x7FF;           // Bits 5-15
                        data_length = (length_field == 0x0F) ? 0 : (length_field + 1);

                        // Now consume the 2-byte MPID header from buffer
                        // This moves buf->data forward by 2 bytes
                        net_buf_simple_pull(buf, 2);

                        ESP_LOGI(TAG, "  Format A: mpid=0x%04x, prop_id=0x%04x, len=%d",
                                 mpid, property_id, data_length);
                    } else {
                        ESP_LOGW(TAG, "  ⚠️  Format A MPID but buffer too short");
                        break;
                    }
                } else {
                    /*
                     * ════════════════════════════════════════════════════════════════
                     *                   FORMAT B: EXTENDED 3-BYTE MPID
                     * ════════════════════════════════════════════════════════════════
                     *
                     * Used for property IDs > 0x07FF or when variable length needed
                     *
                     * BIT LAYOUT (24 bits total, little-endian):
                     * ──────────────────────────────────────────────────────────────
                     *
                     *  Byte 0:  [L L L L L L L 1]
                     *            ↑ ↑ ↑ ↑ ↑ ↑ ↑ ↑
                     *         Length[6:0]  Fmt
                     *
                     *  Byte 1:  [P P P P P P P P]    Byte 2:  [P P P P P P P P]
                     *            ↑ ↑ ↑ ↑ ↑ ↑ ↑ ↑                ↑ ↑ ↑ ↑ ↑ ↑ ↑ ↑
                     *         Prop[7:0]                      Prop[15:8]
                     *
                     * Where:
                     *   Fmt (bit 0)      = 1 (indicates Format B)
                     *   Len (bits 1-7)   = Length field (7 bits = 0-127)
                     *   Prop (bytes 1-2) = Property ID (16 bits = 0-65535, little-endian)
                     *
                     * LENGTH ENCODING:
                     * ----------------
                     * The 7-bit length field can represent:
                     *   0x00-0x7E: Actual length = length_field (0-126 bytes)
                     *   0x7F:      Length is encoded IN the sensor data (variable)
                     *
                     * EXAMPLE: Accelerometer X (property ID = 0x5001, 4 bytes data)
                     * ──────────────────────────────────────────────────────────────
                     *   Property ID = 0x5001 (little-endian: 0x01 0x50)
                     *   Data length = 4 bytes
                     *   Format bit = 1
                     *
                     *   Byte 0 = (4 << 1) | 1 = 0b00001001 = 0x09
                     *   Byte 1 = 0x01 (property ID low byte)
                     *   Byte 2 = 0x50 (property ID high byte)
                     *
                     *   Wire bytes: 0x09 0x01 0x50 [sensor data...]
                     *
                     * WHY LITTLE-ENDIAN?
                     * ------------------
                     * BLE uses little-endian for multi-byte values (Bluetooth spec).
                     * ESP32 is also little-endian, so we can read directly.
                     *
                     * Property ID 0x5001 in memory:
                     *   buf->data[1] = 0x01 (low byte)
                     *   buf->data[2] = 0x50 (high byte)
                     *
                     * To reconstruct: (0x50 << 8) | 0x01 = 0x5001
                     */
                    if (buf->len >= 3) {
                        // Read format byte (contains length field)
                        uint8_t format_byte = buf->data[0];
                        uint8_t length_field = (format_byte >> 1) & 0x7F;  // Bits 1-7

                        // Read 16-bit property ID in little-endian
                        property_id = (buf->data[2] << 8) | buf->data[1];

                        // Decode length (0x7F means variable/unknown)
                        data_length = (length_field == 0x7F) ? 0 : length_field;

                        // Consume the 3-byte MPID header
                        net_buf_simple_pull(buf, 3);

                        ESP_LOGI(TAG, "  Format B: fmt_byte=0x%02x, len_field=%d, prop_id=0x%04x, data_len=%d",
                                 format_byte, length_field, property_id, data_length);
                    } else {
                        ESP_LOGW(TAG, "  ⚠️  Format B MPID but buffer too short");
                        break;
                    }
                }

                ESP_LOGI(TAG, "  Property ID: 0x%04x, Length: %d bytes, buf remaining: %d",
                         property_id, data_length, buf->len);

                /*
                 * ════════════════════════════════════════════════════════════════
                 *              EXTRACTING THE ACTUAL SENSOR DATA
                 * ════════════════════════════════════════════════════════════════
                 *
                 * Now that we've parsed the MPID header and know:
                 *   - property_id: What sensor this is (e.g., 0x5001 = Accel X)
                 *   - data_length: How many bytes the value occupies
                 *
                 * We can extract the actual sensor reading!
                 *
                 * THE BUFFER CURSOR IS NOW POSITIONED AT THE DATA:
                 * ------------------------------------------------
                 *   Before: [09 01 50 | 50 00 00 00]
                 *                       ↑ cursor here (after pulling MPID)
                 *
                 * SENSOR DATA ENCODING:
                 * ---------------------
                 * BLE Mesh sensors typically use little-endian integers:
                 *   - 1 byte:  int8_t  or uint8_t  (-128 to 127, or 0-255)
                 *   - 2 bytes: int16_t or uint16_t (-32768 to 32767, or 0-65535)
                 *   - 4 bytes: int32_t or uint32_t (full 32-bit range)
                 *
                 * The BLE Mesh spec defines specific encodings for each sensor type.
                 * For our IMU sensors (custom property IDs 0x5001-0x5006):
                 *   - Accelerometer: int32_t in milli-g (mg), e.g., 1000 = 1.0g
                 *   - Gyroscope:     int32_t in milli-dps (mdps), e.g., 1000 = 1°/s
                 *
                 * READING MULTI-BYTE VALUES:
                 * ---------------------------
                 * ESP-IDF provides helper functions to read little-endian values:
                 *   - net_buf_simple_pull_u8()   : Read 1 byte (unsigned)
                 *   - net_buf_simple_pull_le16() : Read 2 bytes little-endian
                 *   - net_buf_simple_pull_le32() : Read 4 bytes little-endian
                 *
                 * These functions:
                 *   1. Read the value from current cursor position
                 *   2. Convert from little-endian to host byte order
                 *   3. Advance the cursor by the number of bytes read
                 *
                 * EXAMPLE: Reading Accel X = 80 (0x50 00 00 00 in little-endian)
                 * ──────────────────────────────────────────────────────────────
                 *   Buffer: [50 00 00 00]
                 *            ↑  ↑  ↑  ↑
                 *           b0 b1 b2 b3
                 *
                 *   net_buf_simple_pull_le32() reads:
                 *     value = (b3 << 24) | (b2 << 16) | (b1 << 8) | b0
                 *           = (0x00 << 24) | (0x00 << 16) | (0x00 << 8) | 0x50
                 *           = 0x00000050 = 80
                 *
                 * WHY SIGNED INTEGERS?
                 * --------------------
                 * IMU data can be negative (e.g., accelerometer pointing down,
                 * gyroscope rotating counter-clockwise). We cast to signed types
                 * to correctly interpret the two's complement encoding.
                 */
                if (data_length > 0 && buf->len >= data_length) {
                    if (data_length == 4) {
                        // Read 4-byte signed integer (most common for sensor values)
                        int32_t value = net_buf_simple_pull_le32(buf);
                        ESP_LOGI(TAG, "  ✅ Sensor 0x%04x = %d", property_id, (int)value);

                        // Call external handler (can be overridden by mesh_mqtt_bridge)
                        provisioner_sensor_msg_handler(addr, property_id, value);

                        // REAL-WORLD INTERPRETATION EXAMPLES:
                        // ------------------------------------
                        // If property_id == 0x5001 (Accel X):
                        //   value = 80 mg = 0.08g (slight tilt)
                        //   value = 1000 mg = 1.0g (Earth's gravity)
                        //
                        // If property_id == 0x5004 (Gyro X):
                        //   value = 500 mdps = 0.5°/s (slow rotation)
                        //   value = 30000 mdps = 30°/s (fast spin)

                    } else if (data_length == 2) {
                        // Read 2-byte signed integer
                        int16_t value = (int16_t)net_buf_simple_pull_le16(buf);
                        ESP_LOGI(TAG, "  ✅ Sensor 0x%04x = %d", property_id, (int)value);

                        // Call external handler
                        provisioner_sensor_msg_handler(addr, property_id, (int32_t)value);

                        // Used by some standard sensors (e.g., temperature in 0.01°C)
                        // Example: 2543 = 25.43°C

                    } else if (data_length == 1) {
                        // Read 1-byte signed integer
                        int8_t value = (int8_t)net_buf_simple_pull_u8(buf);
                        ESP_LOGI(TAG, "  ✅ Sensor 0x%04x = %d", property_id, (int)value);

                        // Call external handler
                        provisioner_sensor_msg_handler(addr, property_id, (int32_t)value);

                        // Used by simple sensors (e.g., battery percentage 0-100)

                    } else {
                        // Unsupported length - log the raw bytes for debugging
                        ESP_LOGI(TAG, "  ⚠️  Unsupported data length: %d bytes", data_length);
                        ESP_LOG_BUFFER_HEX(TAG, buf->data, data_length);
                    }
                } else if (data_length == 0) {
                    /*
                     * VARIABLE-LENGTH SENSOR DATA
                     * ============================
                     *
                     * If length_field was 0x0F (Format A) or 0x7F (Format B),
                     * it means the length is encoded WITHIN the sensor data.
                     *
                     * This is used for complex sensors that can have different
                     * data sizes (e.g., strings, variable-length arrays).
                     *
                     * To handle this, we'd need to read the first byte(s) of
                     * the data to determine the actual length, then read that
                     * many more bytes.
                     *
                     * Our IMU sensors use fixed-length data, so we don't
                     * implement this case.
                     */
                    ESP_LOGI(TAG, "  ⚠️  Zero-length sensor data (length field = 0x7F)");
                } else {
                    // Buffer corruption or mesh transmission error
                    ESP_LOGW(TAG, "  ⚠️  Buffer too short for sensor data (need %d, have %d)",
                            data_length, buf->len);
                }
            }
        }
        break;

    case ESP_BLE_MESH_SENSOR_CLIENT_TIMEOUT_EVT:
        ESP_LOGW(TAG, "Sensor client timeout, opcode 0x%04" PRIx32, opcode);
        break;

    default:
        break;
    }
}

/*
 * ═══════════════════════════════════════════════════════════════════════════
 *                    VENDOR MODEL MESSAGE HANDLER
 * ═══════════════════════════════════════════════════════════════════════════
 *
 * VENDOR MODELS - CUSTOM APPLICATION DATA
 * ========================================
 *
 * Vendor models allow you to define custom message formats beyond the standard
 * BLE Mesh SIG models (OnOff, Level, Sensor, etc.). They're perfect for:
 * - Bulk data transmission (like sending all 6 IMU axes in one message)
 * - Application-specific protocols
 * - Proprietary features
 *
 * VENDOR OPCODE FORMAT:
 * ---------------------
 * Vendor opcodes are 3 bytes:
 *   [Opcode byte] [Company ID low] [Company ID high]
 *
 * Example: 0xC00001
 *   - 0xC0 = opcode byte
 *   - 0x00 0x01 = Company ID (little-endian)
 *
 * For custom/test applications, you can use any company ID, but for production
 * you should use your Bluetooth SIG assigned company ID.
 *
 * WHY USE VENDOR MODELS FOR IMU DATA?
 * ------------------------------------
 * Sending 6 IMU values (accel_x/y/z, gyro_x/y/z) via standard Sensor model
 * requires 6 separate messages. With a vendor model, we can pack all 6 values
 * into a single 24-byte message (6 × 4 bytes), reducing:
 * - Network traffic (1 message instead of 6)
 * - Power consumption (less radio time)
 * - Latency (all data arrives together)
 */

// IMU vendor opcode (matches node's opcode)
#define VENDOR_MODEL_OP_IMU_DATA  0xC00001  // Combined IMU data (all 6 axes)

/**
 * WEAK FUNCTION PATTERN FOR EXTENSIBILITY
 * ========================================
 *
 * This is a "weak" function that can be overridden by external components.
 * Default implementation does nothing (allows external components to handle vendor messages).
 *
 * USAGE:
 * - If mesh_mqtt_bridge component is linked, it will override this
 * - If no bridge is used, this default (empty) version is called
 *
 * This pattern allows the provisioner to remain independent while still
 * being extensible for MQTT bridges, data loggers, etc.
 */
__attribute__((weak)) void provisioner_vendor_msg_handler(uint16_t src_addr, uint32_t opcode,
                                                           uint8_t *data, uint16_t length)
{
    // Default: do nothing
    // External components can override this function
}

/**
 * Weak function for sensor message handling
 * Can be overridden by mesh_mqtt_bridge or other components
 */
__attribute__((weak)) void provisioner_sensor_msg_handler(uint16_t src_addr, uint16_t property_id,
                                                           int32_t value)
{
    // Default: do nothing
    // External components can override this function
}

void mesh_vendor_client_cb(esp_ble_mesh_model_cb_event_t event,
                           esp_ble_mesh_model_cb_param_t *param)
{
    uint32_t opcode;
    uint16_t addr;

    switch (event) {
    case ESP_BLE_MESH_MODEL_SEND_COMP_EVT:
        ESP_LOGI(TAG, "Vendor model send complete");
        break;

    case ESP_BLE_MESH_MODEL_OPERATION_EVT:
        /* Handle direct vendor messages (not published) */
        opcode = param->model_operation.opcode;
        addr = param->model_operation.ctx->addr;
        uint16_t length = param->model_operation.length;
        uint8_t *data = param->model_operation.msg;

        // Call external handler first (can be overridden by mesh_mqtt_bridge)
        provisioner_vendor_msg_handler(addr, opcode, data, length);

        // Log IMU data for debugging
        if (opcode == VENDOR_MODEL_OP_IMU_DATA) {
            // Compact IMU data (8 bytes: timestamp + all 6 axes in int8_t)
            if (length == 8) {
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

                ESP_LOGI(TAG, "📊 IMU [t=%u] Accel:[%.1f,%.1f,%.1f]g Gyro:[%d,%d,%d]dps",
                         imu->timestamp_ms, ax, ay, az, gx, gy, gz);
            }
        } else {
            ESP_LOGD(TAG, "📩 Vendor opcode=0x%06" PRIx32 " from 0x%04x len=%d",
                     opcode, addr, length);
        }
        break;

    case ESP_BLE_MESH_CLIENT_MODEL_RECV_PUBLISH_MSG_EVT:
        /*
         * ════════════════════════════════════════════════════════════════════════
         *                    VENDOR MODEL PUBLISHED MESSAGE RECEPTION
         * ════════════════════════════════════════════════════════════════════════
         *
         * This event fires when a node publishes vendor model data (not used currently).
         * We use direct unicast messages (MODEL_OPERATION_EVT) instead.
         */
        opcode = param->client_recv_publish_msg.opcode;
        addr = param->client_recv_publish_msg.ctx->addr;

        ESP_LOGI(TAG, "📦 Published vendor message from 0x%04x, opcode=0x%06" PRIx32, addr, opcode);
        break;

    case ESP_BLE_MESH_CLIENT_MODEL_SEND_TIMEOUT_EVT:
        ESP_LOGW(TAG, "Vendor model send timeout");
        break;

    default:
        ESP_LOGD(TAG, "Vendor model event: %d", event);
        break;
    }
}
