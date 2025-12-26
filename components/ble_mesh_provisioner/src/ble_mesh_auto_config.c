/*
 * ═══════════════════════════════════════════════════════════════════════════
 *              AUTOMATIC MODEL CONFIGURATION STATE MACHINE
 * ═══════════════════════════════════════════════════════════════════════════
 *
 * This module implements automatic configuration of newly provisioned nodes
 * by parsing their composition data and configuring all discovered models.
 *
 * CONFIGURATION FLOW:
 * 1. Parse composition data → Discover all models
 * 2. Add AppKey to node
 * 3. Bind AppKey to ALL discovered models (sequentially)
 * 4. Configure publication for ALL server models
 * 5. Node ready for operation
 *
 * WHY AUTOMATIC?
 * - Works with ANY combination of models
 * - No hardcoded model IDs
 * - Extensible to new model types
 * - Reduces provisioner code complexity
 */

#include "ble_mesh_auto_config.h"
#include "ble_mesh_storage.h"
#include "esp_log.h"
#include <string.h>

#define TAG "AUTO_CONFIG"

/*
 * HELPER: Check if model supports publication
 * Server models can publish their state, client models cannot.
 */
static bool model_supports_publication(uint16_t model_id, uint16_t company_id)
{
    // Vendor models: typically support publication
    if (company_id != ESP_BLE_MESH_CID_NVAL) {
        return true;
    }

    // SIG models that support publication (server models)
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

/*
 * Bind next model in sequence
 * Returns true if binding was initiated, false if all done
 */
bool bind_next_model(uint16_t addr, mesh_node_info_t *node_info,
                     esp_ble_mesh_client_common_param_t *common,
                     struct esp_ble_mesh_key *prov_key)
{
    // Find next model that needs binding
    while (node_info->next_model_to_bind < node_info->model_count) {
        int idx = node_info->next_model_to_bind;
        node_model_info_t *model = &node_info->models[idx];

        // Skip if already bound
        if (model->appkey_bound) {
            node_info->next_model_to_bind++;
            continue;
        }

        // Skip if model doesn't need AppKey
        if (!model_needs_appkey_binding(model->model_id, model->company_id)) {
            ESP_LOGI(TAG, "  Skipping model 0x%04x (uses DevKey)", model->model_id);
            model->appkey_bound = true;  // Mark as "done"
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

        set_state.model_pub_set.element_addr = node_info->unicast;
        set_state.model_pub_set.publish_addr = 0x0001;  // Publish to provisioner (using PUBLISH mechanism)
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
    ESP_LOGI(TAG, "🎉 All publications configured! Node is ready!");
    return false;
}
