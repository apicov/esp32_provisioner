/* ============================================================================
 *              AUTOMATIC MODEL CONFIGURATION STATE MACHINE
 * ============================================================================
 *
 * 📚 LEARNING ROADMAP:
 *   ⭐⭐ Intermediate - State machines, sequential async operations
 *   ⭐⭐⭐ Advanced - BLE Mesh configuration protocol, model discovery
 *
 * 🎯 BLE MESH CONCEPTS COVERED:
 *   • Model Configuration - AppKey binding, publish/subscribe setup
 *   • Composition Data Parsing - Discovering node capabilities
 *   • Group Addressing - Using 0xC001 for sensor data routing
 *   • Server vs Client Models - Different configuration requirements
 *   • DevKey vs AppKey - When each is used
 *
 * 💻 C/C++ TECHNIQUES DEMONSTRATED:
 *   • State Machine Pattern - Sequential async operation management
 *   • Iterator Pattern - Processing array elements one-by-one
 *   • Switch-based Polymorphism - Different behavior per model type
 *   • Boolean State Flags - Tracking completion of async operations
 *   • Early Return Pattern - Simplifying nested conditionals
 *
 * 🔄 STATE MACHINE DESIGN:
 *
 * PROBLEM:
 * After provisioning, a node needs multiple configuration steps executed
 * SEQUENTIALLY (each waits for previous to complete):
 *
 *   1. Add AppKey    → 2. Bind Model[0] → 3. Bind Model[1] → ...
 *      ↓ wait ack        ↓ wait ack          ↓ wait ack
 *
 * TRADITIONAL APPROACH (callback hell):
 *   void step1_done() { start_step2(); }
 *   void step2_done() { start_step3(); }
 *   void step3_done() { start_step4(); }
 *   // Deeply nested, hard to maintain
 *
 * OUR APPROACH (state machine with progress tracking):
 *   - Store current state in node_info struct
 *   - Each callback checks "what's next?" and advances
 *   - Cleaner, easier to debug, scalable
 *
 * STATE PROGRESSION:
 *
 *   ┌─────────────────────────────────────────────────────────────┐
 *   │ 1. COMPOSITION DATA RECEIVED                                │
 *   │    • Parse all models → store in node_info.models[]        │
 *   │    • Initialize: next_model_to_bind = 0                    │
 *   └──────────────────┬──────────────────────────────────────────┘
 *                      ▼
 *   ┌─────────────────────────────────────────────────────────────┐
 *   │ 2. APPKEY BINDING PHASE                                     │
 *   │    Loop: bind_next_model()                                  │
 *   │    • Check models[next_model_to_bind]                       │
 *   │    • If needs AppKey → send bind, wait for ack              │
 *   │    • On ack → next_model_to_bind++, repeat                  │
 *   │    • If all done → advance to publication                   │
 *   └──────────────────┬──────────────────────────────────────────┘
 *                      ▼
 *   ┌─────────────────────────────────────────────────────────────┐
 *   │ 3. PUBLICATION CONFIG PHASE                                 │
 *   │    Loop: configure_next_publication()                       │
 *   │    • Check models[next_model_to_pub]                        │
 *   │    • If server model → set publish address, wait for ack    │
 *   │    • On ack → next_model_to_pub++, repeat                   │
 *   │    • If all done → advance to subscription                  │
 *   └──────────────────┬──────────────────────────────────────────┘
 *                      ▼
 *   ┌─────────────────────────────────────────────────────────────┐
 *   │ 4. SUBSCRIPTION CONFIG PHASE                                │
 *   │    Loop: subscribe_next_model()                             │
 *   │    • Check models[next_model_to_sub]                        │
 *   │    • If client model → add subscription, wait for ack       │
 *   │    • On ack → next_model_to_sub++, repeat                   │
 *   │    • If all done → NODE READY! 🎉                           │
 *   └─────────────────────────────────────────────────────────────┘
 *
 * WHY THIS APPROACH?
 * ✅ Automatic - Works with ANY combination of models
 * ✅ No hardcoding - Discovers models from composition data
 * ✅ Extensible - Add new model types by updating helper functions
 * ✅ Robust - Handles failures gracefully (skip and continue)
 * ✅ Debuggable - Clear state visible in logs
 *
 * KEY C PATTERNS USED:
 * • while() loop with iterator - process next unconfigured model
 * • Early return - "return true" when async operation started
 * • Fall-through logic - skip non-applicable models efficiently
 * ============================================================================
 */

#include "ble_mesh_auto_config.h"
#include "ble_mesh_storage.h"
#include "esp_log.h"
#include <string.h>

#define TAG "AUTO_CONFIG"

/* ===================================================================
 * SECTION: Model Classification Helpers
 * BLE MESH CONCEPT: Server vs Client model behavior
 * C PATTERN: Switch-based classification lookup
 * DIFFICULTY: ⭐⭐ Intermediate
 * =================================================================== */

/**
 * Check if model supports publication
 *
 * BLE MESH CONCEPT: Publication Model
 * ====================================
 *
 * SERVER MODELS:
 * - Publish their STATE changes (e.g., "light is now ON")
 * - Examples: OnOff Server, Sensor Server, Level Server
 * - Think: "I have data to share"
 *
 * CLIENT MODELS:
 * - Do NOT publish (they send commands and receive responses)
 * - Examples: OnOff Client, Sensor Client
 * - Think: "I control others or request data"
 *
 * PUBLISH MECHANISM:
 * When a server model's state changes, it automatically publishes
 * a status message to its configured publish address.
 *
 * Example:
 *   1. Light turns ON (state change)
 *   2. OnOff Server automatically publishes OnOff Status to 0xC001
 *   3. All subscribers of 0xC001 receive the update
 *
 * C PATTERN: Guard Clause First
 * Check special cases (vendor models) before the switch statement.
 * This avoids a huge switch with vendor IDs.
 *
 * @param model_id    SIG model ID (e.g., 0x1000 = Generic OnOff Server)
 * @param company_id  Company ID (ESP_BLE_MESH_CID_NVAL = SIG model)
 * @return true if model publishes state, false otherwise
 */
static bool model_supports_publication(uint16_t model_id, uint16_t company_id)
{
    // BLE MESH: Vendor models (non-SIG)
    // ESP_BLE_MESH_CID_NVAL (0xFFFF) means "not a vendor model" (it's SIG)
    // Any other company_id means it's a vendor model
    if (company_id != ESP_BLE_MESH_CID_NVAL) {
        // Vendor models: typically support publication
        // Our vendor models (IMU data) publish sensor readings
        return true;
    }

    // C PATTERN: Switch for SIG model classification
    // Organized by model type for clarity
    //
    // BLE MESH MODEL NUMBERS:
    // 0x1000 series = Generic models (OnOff, Level, Power, Battery)
    // 0x1100 series = Sensor models
    // 0x1200 series = Time models
    // 0x1300 series = Scene models
    //
    // EVEN numbers = SERVER models (e.g., 0x1000 = OnOff Server)
    // ODD numbers  = CLIENT models (e.g., 0x1001 = OnOff Client)
    switch (model_id) {
    case 0x1000:  // Generic OnOff Server
    case 0x1002:  // Generic Level Server
    case 0x1004:  // Generic Default Transition Time Server
    case 0x1006:  // Generic Power OnOff Server
    case 0x1008:  // Generic Power OnOff Setup Server
    case 0x100A:  // Generic Power Level Server
    case 0x100C:  // Generic Power Level Setup Server
    case 0x100E:  // Generic Battery Server
    case 0x1100:  // Sensor Server
    case 0x1200:  // Time Server
    case 0x1201:  // Time Setup Server
    case 0x1300:  // Scene Server
    case 0x1301:  // Scene Setup Server
    case 0x1303:  // Scheduler Server
    case 0x1304:  // Scheduler Setup Server
        return true;
    default:
        return false;  // Client models and Config models
    }
}

/**
 * Get publication address for a model type
 *
 * BLE MESH CONCEPT: Group Addresses for Pub/Sub
 * ==============================================
 *
 * ADDRESS TYPES:
 * • 0x0001-0x7FFF: Unicast (specific device)
 * • 0xC000-0xFEFF: Group (multiple subscribers)
 * • 0xFFFF:        All nodes (broadcast)
 *
 * GROUP 0xC001: "Sensor Data Group"
 * We use this as a central hub for all sensor data:
 * - IMU sensors publish to 0xC001
 * - Heart rate sensors publish to 0xC001
 * - Gateways subscribe to 0xC001
 * - Provisioner subscribes to 0xC001
 *
 * WHY GROUP ADDRESSING?
 * ✅ Decoupling: Publishers don't need to know all subscribers
 * ✅ Efficiency: One publish reaches multiple subscribers
 * ✅ Flexibility: Add/remove subscribers without reconfiguring publishers
 *
 * ALTERNATIVE DESIGN (not used here):
 * Could publish to provisioner's unicast (0x0001), but then:
 * ❌ Only provisioner gets data (gateways excluded)
 * ❌ Tightly coupled architecture
 *
 * @param model_id  SIG model ID
 * @return Group address where this model should publish
 */
static uint16_t get_publication_address(uint16_t model_id)
{
    // BLE MESH: All sensor-related models publish to sensor group
    // This allows multiple receivers (provisioner, gateways, displays)
    switch (model_id) {
    case 0x1100:  // Sensor Server - publish sensor data
        return 0xC001;
    default:
        // Vendor models (IMU, custom sensors) also use sensor group
        // Changed from 0x0001 (unicast to provisioner) to support
        // multi-subscriber architecture (gateways + provisioner)
        return 0xC001;
    }
}

/*
 * HELPER: Check if model supports subscription
 * Client models can subscribe to receive data from servers.
 */
static bool model_supports_subscription(uint16_t model_id, uint16_t company_id)
{
    // Only vendor CLIENT models (0x0000) should subscribe, not servers (0x0001)
    if (company_id != ESP_BLE_MESH_CID_NVAL) {
        return (model_id == 0x0000);  // Only vendor client receives
    }

    // SIG client models that support subscription
    switch (model_id) {
    case 0x1102:  // Sensor Client
        return true;
    default:
        return false;
    }
}

/*
 * HELPER: Get subscription address for a client model
 * All client and vendor models subscribe to sensor group 0xC001
 */
static uint16_t get_subscription_address(uint16_t model_id)
{
    switch (model_id) {
    case 0x1102:  // Sensor Client - subscribe to sensor group
        return 0xC001;
    default:
        return 0xC001;  // Vendor models also subscribe to sensor group
    }
}

/*
 * HELPER: Check if model should be bound to AppKey
 * Config models (0x0000, 0x0001) use DevKey, not AppKey.
 */
static bool model_needs_appkey_binding(uint16_t model_id, uint16_t company_id)
{
    // All vendor models need AppKey
    if (company_id != ESP_BLE_MESH_CID_NVAL) {
        return true;
    }

    // Skip Configuration models (they use DevKey)
    if (model_id == 0x0000 || model_id == 0x0001) {
        return false;
    }

    // All other SIG models need AppKey
    return true;
}

/* ===================================================================
 * SECTION: State Machine Core Functions
 * BLE MESH CONCEPT: Sequential configuration protocol
 * C PATTERN: Iterator with early return
 * DIFFICULTY: ⭐⭐⭐ Advanced
 * =================================================================== */

/**
 * Bind next model to AppKey (State Machine Step 2)
 *
 * BLE MESH CONCEPT: AppKey Binding
 * =================================
 *
 * WHAT IS MODEL APP BINDING?
 * After a node is provisioned and has the AppKey, each MODEL must be
 * explicitly bound to that AppKey before it can use it.
 *
 * WHY BINDING IS NEEDED:
 * - A node can have multiple AppKeys (e.g., AppKey[0] for lights, AppKey[1] for HVAC)
 * - Each model needs to know WHICH AppKey to use for encryption
 * - Binding says: "Model X, use AppKey[0] for your messages"
 *
 * BINDING PROCESS:
 *   Provisioner → Node: "MODEL_APP_BIND: Element=0x0010, Model=0x1000, AppKey=0"
 *   Node → Provisioner: "MODEL_APP_STATUS: Success, bound!"
 *
 * CONFIGURATION MODELS EXCEPTION:
 * Config Server (0x0000) and Config Client (0x0001) use DevKey, NOT AppKey.
 * These models handle network configuration and must work even if AppKeys change.
 *
 * C PATTERN: while() Loop with Early Return
 * ==========================================
 *
 * This pattern is elegant for processing a list asynchronously:
 *
 * while (has_more_items) {
 *     item = get_next_item();
 *     if (should_skip(item)) { advance(); continue; }  // Skip quickly
 *     if (start_async_operation(item)) {
 *         return true;  // ← Early return! Wait for callback
 *     }
 * }
 * return false;  // All done
 *
 * The key insight: We only process ONE item per call, then return.
 * When the async operation completes, the callback calls us again,
 * and we pick up where we left off (iterator advanced).
 *
 * @param addr       Node's unicast address
 * @param node_info  Node state (contains models[] and next_model_to_bind index)
 * @param common     Common parameters for mesh message
 * @param prov_key   Network and application keys
 * @return true if binding started (wait for ack), false if all done
 */
bool bind_next_model(uint16_t addr, mesh_node_info_t *node_info,
                     esp_ble_mesh_client_common_param_t *common,
                     struct esp_ble_mesh_key *prov_key)
{
    // C PATTERN: Iterator-based processing
    // Loop through models array starting from last position
    // next_model_to_bind acts as our "iterator" or "cursor"
    while (node_info->next_model_to_bind < node_info->model_count) {
        int idx = node_info->next_model_to_bind;
        node_model_info_t *model = &node_info->models[idx];

        // C PATTERN: Guard clauses for early skip
        // Check conditions that allow us to skip this model

        // Skip if already bound (idempotency check)
        if (model->appkey_bound) {
            node_info->next_model_to_bind++;
            continue;
        }

        // BLE MESH: Config models use DevKey, not AppKey
        // Skip binding for Configuration Server/Client (0x0000, 0x0001)
        if (!model_needs_appkey_binding(model->model_id, model->company_id)) {
            ESP_LOGI(TAG, "  Skipping model 0x%04x (uses DevKey)", model->model_id);
            model->appkey_bound = true;  // Mark as "done" so we don't retry
            node_info->next_model_to_bind++;
            continue;
        }

        // Bind this model
        ESP_LOGI(TAG, "  Binding model [%d/%d]: 0x%04x (CID=0x%04x)",
                 idx + 1, node_info->model_count,
                 model->model_id, model->company_id);

        esp_ble_mesh_cfg_client_set_state_t set_state = {0};
        common->opcode = ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND;

        set_state.model_app_bind.element_addr = node_info->unicast;
        set_state.model_app_bind.model_app_idx = prov_key->app_idx;
        set_state.model_app_bind.model_id = model->model_id;
        set_state.model_app_bind.company_id = model->company_id;

        esp_err_t err = esp_ble_mesh_config_client_set_state(common, &set_state);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "  Bind failed for model 0x%04x, err=%d", model->model_id, err);
            node_info->next_model_to_bind++;
            continue;  // Try next model
        }

        return true;  // Binding initiated, wait for response
    }

    // All models processed
    ESP_LOGI(TAG, "✅ All models bound!");
    return false;
}

/*
 * Configure publication for next model in sequence
 * Returns true if config was initiated, false if all done
 */
bool configure_next_publication(uint16_t addr, mesh_node_info_t *node_info,
                                esp_ble_mesh_client_common_param_t *common,
                                struct esp_ble_mesh_key *prov_key)
{
    // Find next model that needs publication config
    while (node_info->next_model_to_pub < node_info->model_count) {
        int idx = node_info->next_model_to_pub;
        node_model_info_t *model = &node_info->models[idx];

        // Skip if already configured
        if (model->pub_configured) {
            node_info->next_model_to_pub++;
            continue;
        }

        // Skip if model doesn't support publication
        if (!model_supports_publication(model->model_id, model->company_id)) {
            ESP_LOGI(TAG, "  Skipping publication for 0x%04x (client model)", model->model_id);
            model->pub_configured = true;  // Mark as "done"
            node_info->next_model_to_pub++;
            continue;
        }

        // Configure publication for this model
        ESP_LOGI(TAG, "  Configuring publication [%d/%d]: 0x%04x (CID=0x%04x)",
                 idx + 1, node_info->model_count,
                 model->model_id, model->company_id);

        esp_ble_mesh_cfg_client_set_state_t set_state = {0};
        common->opcode = ESP_BLE_MESH_MODEL_OP_MODEL_PUB_SET;

        uint16_t pub_addr = get_publication_address(model->model_id);
        ESP_LOGI(TAG, "    Publishing to: 0x%04x (model_id=0x%04x, cid=0x%04x)",
                 pub_addr, model->model_id, model->company_id);

        set_state.model_pub_set.element_addr = node_info->unicast;
        set_state.model_pub_set.publish_addr = pub_addr;  // Use model-specific publish address
        set_state.model_pub_set.publish_app_idx = prov_key->app_idx;
        set_state.model_pub_set.publish_ttl = 7;
        set_state.model_pub_set.publish_period = 0;  // Manual publishing
        set_state.model_pub_set.publish_retransmit = 0;
        set_state.model_pub_set.company_id = model->company_id;
        set_state.model_pub_set.model_id = model->model_id;

        esp_err_t err = esp_ble_mesh_config_client_set_state(common, &set_state);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "  Pub config failed for model 0x%04x, err=%d", model->model_id, err);
            node_info->next_model_to_pub++;
            continue;  // Try next model
        }

        return true;  // Config initiated, wait for response
    }

    // All models processed
    ESP_LOGI(TAG, "✅ All publications configured!");
    return false;
}

/*
 * Configure next subscription in sequence
 * Returns true if subscription was initiated, false if all done
 */
bool subscribe_next_model(uint16_t addr, mesh_node_info_t *node_info,
                          esp_ble_mesh_client_common_param_t *common,
                          struct esp_ble_mesh_key *prov_key)
{
    // Find next model that needs subscription
    while (node_info->next_model_to_sub < node_info->model_count) {
        int idx = node_info->next_model_to_sub;
        node_model_info_t *model = &node_info->models[idx];

        // Skip if already subscribed
        if (model->sub_configured) {
            node_info->next_model_to_sub++;
            continue;
        }

        // Skip if model doesn't support subscription
        if (!model_supports_subscription(model->model_id, model->company_id)) {
            model->sub_configured = true;  // Mark as "done"
            node_info->next_model_to_sub++;
            continue;
        }

        // Configure subscription for this model
        ESP_LOGI(TAG, "  Configuring subscription [%d/%d]: 0x%04x (CID=0x%04x)",
                 idx + 1, node_info->model_count,
                 model->model_id, model->company_id);

        uint16_t sub_addr = get_subscription_address(model->model_id);
        ESP_LOGI(TAG, "    Subscribing to: 0x%04x", sub_addr);

        esp_ble_mesh_cfg_client_set_state_t set_state = {0};
        common->opcode = ESP_BLE_MESH_MODEL_OP_MODEL_SUB_ADD;

        set_state.model_sub_add.element_addr = node_info->unicast;
        set_state.model_sub_add.sub_addr = sub_addr;
        set_state.model_sub_add.company_id = model->company_id;
        set_state.model_sub_add.model_id = model->model_id;

        esp_err_t err = esp_ble_mesh_config_client_set_state(common, &set_state);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "  Sub config failed for model 0x%04x, err=%d", model->model_id, err);
            node_info->next_model_to_sub++;
            continue;  // Try next model
        }

        return true;  // Config initiated, wait for response
    }

    // All models processed
    ESP_LOGI(TAG, "🎉 All subscriptions configured! Node is ready!");
    return false;
}
