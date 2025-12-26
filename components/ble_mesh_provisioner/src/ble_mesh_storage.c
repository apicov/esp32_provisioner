/*
 * BLE MESH PROVISIONER - NODE STORAGE
 * ====================================
 *
 * This file provides simple in-memory storage for provisioned nodes.
 * In a production system, you would want to store this data in non-volatile
 * storage (NVS/flash) so nodes remain known after reboot.
 *
 * WHAT IS STORED:
 * For each provisioned node, we track:
 *   - UUID: Device's unique 16-byte identifier
 *   - Unicast address: The node's mesh address (where to send messages)
 *   - Element count: Number of addressable entities in the node
 *   - OnOff state: Last known state of the Generic OnOff model
 *
 * WHY STORE THIS:
 * - We need the unicast address to send control messages
 * - UUID helps identify devices if they rejoin the network
 * - Element count tells us how many addresses this node occupies
 * - State tracking allows us to maintain sync with the device
 *
 * LIMITATIONS:
 * - Storage is volatile (lost on reboot)
 * - Maximum 10 nodes (MESH_STORAGE_MAX_NODES)
 * - No persistence to flash
 * - No node removal function (only add/update)
 *
 * PRODUCTION IMPROVEMENTS:
 * - Use NVS (Non-Volatile Storage) API for persistence
 * - Add node removal/update functions
 * - Store device key (for secure configuration)
 * - Store subscription lists and publication addresses
 * - Add timestamp for last seen/communicated
 */

#include "ble_mesh_storage.h"
#include <string.h>
#include "esp_log.h"

#define TAG "MESH_STORAGE"

/*
 * NODE STORAGE ARRAY
 * ------------------
 * Simple array to store up to MESH_STORAGE_MAX_NODES (10) provisioned nodes
 * In production, this would be stored in NVS flash for persistence
 */
static mesh_node_info_t nodes[MESH_STORAGE_MAX_NODES];

/*
 * NODE COUNTER
 * ------------
 * Tracks how many nodes are currently stored
 * Range: 0 to MESH_STORAGE_MAX_NODES
 */
static uint16_t node_count = 0;

/*
 * INITIALIZE STORAGE
 * ==================
 *
 * Clears all stored node information and resets the counter.
 * Should be called once during provisioner initialization.
 *
 * In a production system with NVS persistence, this function would:
 * - Open NVS namespace
 * - Optionally load previously stored nodes from flash
 * - Initialize any necessary data structures
 */
esp_err_t mesh_storage_init(void)
{
    memset(nodes, 0, sizeof(nodes));  // Clear all node data
    node_count = 0;                    // Reset counter
    ESP_LOGI(TAG, "Storage initialized");
    return ESP_OK;
}

/*
 * ADD OR UPDATE NODE
 * ==================
 *
 * Stores information about a newly provisioned node, or updates an existing one.
 *
 * BEHAVIOR:
 * - If node with same UUID exists → Update its information
 * - If node is new and storage has space → Add new entry
 * - If storage is full → Return error
 *
 * WHEN CALLED:
 * Called from prov_complete() right after a device is successfully provisioned
 *
 * PARAMETERS:
 * @param uuid - Device's unique 16-byte identifier
 * @param unicast - Assigned unicast address (where to send messages)
 * @param elem_num - Number of elements (addressable entities) in the node
 * @param onoff_state - Initial OnOff state (usually LED_OFF/0)
 *
 * RETURN:
 * - ESP_OK: Node added/updated successfully
 * - ESP_ERR_INVALID_ARG: UUID is NULL
 * - ESP_ERR_NO_MEM: Storage is full (10 nodes maximum)
 */
esp_err_t mesh_storage_add_node(const uint8_t uuid[16], uint16_t unicast, uint8_t elem_num, uint8_t onoff_state)
{
    if (!uuid) {
        return ESP_ERR_INVALID_ARG;
    }

    if (node_count >= MESH_STORAGE_MAX_NODES) {
        ESP_LOGE(TAG, "Storage full");
        return ESP_ERR_NO_MEM;
    }

    // Check if node already exists (compare UUIDs)
    // This handles the case where a node is re-provisioned
    for (int i = 0; i < node_count; i++) {
        if (memcmp(nodes[i].uuid, uuid, 16) == 0) {
            ESP_LOGW(TAG, "Node already exists, updating");
            nodes[i].unicast = unicast;
            nodes[i].elem_num = elem_num;
            nodes[i].onoff_state = onoff_state;
            return ESP_OK;
        }
    }

    // Add new node to the end of the array
    memcpy(nodes[node_count].uuid, uuid, 16);
    nodes[node_count].unicast = unicast;
    nodes[node_count].elem_num = elem_num;
    nodes[node_count].onoff_state = onoff_state;
    node_count++;

    ESP_LOGI(TAG, "Node added: unicast=0x%04x, elem_num=%d, total=%d", unicast, elem_num, node_count);
    return ESP_OK;
}

/*
 * GET NODE BY UNICAST ADDRESS
 * ============================
 *
 * Retrieves stored information for a node given its unicast address.
 *
 * WHY BY UNICAST?
 * - When we receive messages from nodes, they come with unicast address
 * - When we want to send commands, we need to look up node info by address
 * - Unicast is the primary way to identify nodes in mesh communication
 *
 * WHEN CALLED:
 * Called before sending control messages (e.g., OnOff commands) to verify
 * the node exists and retrieve its information.
 *
 * PARAMETERS:
 * @param unicast - The unicast address to search for
 * @param info - Output parameter to receive the node information
 *
 * RETURN:
 * - ESP_OK: Node found, info filled
 * - ESP_ERR_INVALID_ARG: info pointer is NULL
 * - ESP_ERR_NOT_FOUND: No node with this unicast address
 */
esp_err_t mesh_storage_get_node(uint16_t unicast, mesh_node_info_t *info)
{
    if (!info) {
        return ESP_ERR_INVALID_ARG;
    }

    // Linear search through nodes array
    // For production with many nodes, consider a hash table or sorted array
    for (int i = 0; i < node_count; i++) {
        if (nodes[i].unicast == unicast) {
            memcpy(info, &nodes[i], sizeof(mesh_node_info_t));
            return ESP_OK;
        }
    }

    return ESP_ERR_NOT_FOUND;
}

/*
 * UPDATE NODE INFO
 * ================
 *
 * Updates stored information for an existing node.
 * Used to update composition data, model binding states, etc.
 *
 * PARAMETERS:
 * @param unicast - The unicast address of the node to update
 * @param info - New node information to store
 *
 * RETURN:
 * - ESP_OK: Node updated successfully
 * - ESP_ERR_INVALID_ARG: info pointer is NULL
 * - ESP_ERR_NOT_FOUND: No node with this unicast address
 */
esp_err_t mesh_storage_update_node(uint16_t unicast, const mesh_node_info_t *info)
{
    if (!info) {
        return ESP_ERR_INVALID_ARG;
    }

    for (int i = 0; i < node_count; i++) {
        if (nodes[i].unicast == unicast) {
            memcpy(&nodes[i], info, sizeof(mesh_node_info_t));
            ESP_LOGD(TAG, "Node updated: unicast=0x%04x", unicast);
            return ESP_OK;
        }
    }

    return ESP_ERR_NOT_FOUND;
}

/*
 * GET NODE COUNT
 * ==============
 *
 * Returns the total number of provisioned nodes currently stored.
 *
 * USES:
 * - Monitoring network size
 * - Checking if storage is full
 * - Iterating over all nodes
 * - Displaying statistics to user
 */
uint16_t mesh_storage_get_node_count(void)
{
    return node_count;
}
