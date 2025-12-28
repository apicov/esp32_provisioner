/* ============================================================================
 * BLE MESH PROVISIONER - NODE STORAGE
 * ============================================================================
 *
 * 📚 LEARNING ROADMAP:
 *   ⭐ Foundation - Static arrays, memcpy/memcmp, linear search
 *   ⭐⭐ Intermediate - Data persistence concepts, NVS architecture
 *   ⭐⭐⭐ Advanced - Production storage design patterns
 *
 * 🎯 BLE MESH CONCEPTS COVERED:
 *   • Node Information Management - Tracking provisioned devices
 *   • Unicast Addressing - Node identification in the mesh
 *   • State Synchronization - Maintaining device state awareness
 *
 * 💻 C/C++ TECHNIQUES DEMONSTRATED:
 *   • Static Array Storage - Compile-time allocation vs dynamic
 *   • Linear Search - O(n) lookup, trade-offs
 *   • memcpy/memcmp - Efficient memory operations
 *   • Defensive Programming - NULL checks, boundary validation
 *   • Pass-by-pointer - Output parameters pattern
 *
 * 📝 CURRENT IMPLEMENTATION:
 * ==========================
 *
 * This is a SIMPLE in-memory storage for educational purposes.
 * Data is stored in a static array and LOST on reboot.
 *
 * WHAT IS STORED:
 * For each provisioned node:
 *   - UUID (16 bytes): Unique device identifier
 *   - Unicast address: Where to send messages
 *   - Element count: Number of addressable entities
 *   - Model information: Discovered capabilities
 *   - Configuration state: Binding/publication progress
 *
 * WHY STORE THIS:
 * ✅ Control - Need unicast address to send commands
 * ✅ Recovery - UUID helps identify rejoining devices
 * ✅ Addressing - Element count = address range
 * ✅ State sync - Track configuration completion
 *
 * LIMITATIONS (What's missing):
 * ❌ Volatile - Lost on reboot/power cycle
 * ❌ Size limited - Max 10 nodes hardcoded
 * ❌ No removal - Can only add/update
 * ❌ Linear search - O(n) lookup time
 *
 * 🏭 PRODUCTION IMPROVEMENTS:
 * ===========================
 *
 * ESP32 NVS (Non-Volatile Storage) Integration:
 * ```c
 * #include "nvs_flash.h"
 * #include "nvs.h"
 *
 * // Initialize NVS
 * esp_err_t ret = nvs_flash_init();
 * nvs_handle_t nvs_handle;
 * nvs_open("mesh_storage", NVS_READWRITE, &nvs_handle);
 *
 * // Save node (binary blob)
 * nvs_set_blob(nvs_handle, "node_0", &node_info, sizeof(mesh_node_info_t));
 * nvs_commit(nvs_handle);
 *
 * // Load node
 * size_t size = sizeof(mesh_node_info_t);
 * nvs_get_blob(nvs_handle, "node_0", &node_info, &size);
 * ```
 *
 * Additional Features for Production:
 * • Device Key storage (for secure reconfiguration)
 * • Subscription/publication lists
 * • Last-seen timestamps
 * • Network key indices
 * • Hash table for O(1) unicast lookups
 * • Node removal/expiry logic
 * • Wear-leveling considerations
 *
 * C PATTERN NOTE: Static vs Dynamic Allocation
 * ============================================
 *
 * CURRENT (Static):
 *   static mesh_node_info_t nodes[10];  // 10 * sizeof(mesh_node_info_t) bytes
 *   ✅ Fast allocation (compile-time)
 *   ✅ No fragmentation
 *   ✅ Automatic cleanup
 *   ❌ Fixed size
 *   ❌ Wastes memory if underutilized
 *
 * ALTERNATIVE (Dynamic):
 *   mesh_node_info_t *nodes = malloc(max_nodes * sizeof(mesh_node_info_t));
 *   ✅ Flexible sizing
 *   ✅ Can grow/shrink
 *   ❌ Requires free()
 *   ❌ Can fragment heap
 *   ❌ Slower allocation
 *
 * For embedded systems with predictable workload, static is often preferred.
 * ============================================================================
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

    // C PATTERN: UUID Comparison with memcmp()
    //
    // memcmp() signature: int memcmp(const void *s1, const void *s2, size_t n)
    // Returns: 0 if equal, <0 if s1<s2, >0 if s1>s2
    //
    // WHY memcmp() instead of manual loop?
    // ✅ Optimized assembly for the platform
    // ✅ Handles alignment correctly
    // ✅ Clearer intent
    //
    // ALTERNATIVE (manual comparison):
    //   bool uuids_equal = true;
    //   for (int j = 0; j < 16; j++) {
    //       if (nodes[i].uuid[j] != uuid[j]) {
    //           uuids_equal = false;
    //           break;
    //       }
    //   }
    //
    // Check if node already exists (handles re-provisioning)
    for (int i = 0; i < node_count; i++) {
        if (memcmp(nodes[i].uuid, uuid, 16) == 0) {  // UUIDs match
            ESP_LOGW(TAG, "Node already exists, updating");
            // Update existing entry (idempotent operation)
            nodes[i].unicast = unicast;
            nodes[i].elem_num = elem_num;
            nodes[i].onoff_state = onoff_state;
            return ESP_OK;
        }
    }

    // C PATTERN: Struct Member Assignment
    //
    // Add new node to next available slot
    // Note: We use memcpy for array (uuid), direct assignment for scalars
    //
    // WHY memcpy for uuid?
    // Array assignment doesn't work: nodes[i].uuid = uuid; // ❌ Compiler error!
    // Must copy element-by-element or use memcpy
    //
    // memcpy() signature: void *memcpy(void *dest, const void *src, size_t n)
    // Copies n bytes from src to dest
    memcpy(nodes[node_count].uuid, uuid, 16);  // Copy 16-byte UUID
    nodes[node_count].unicast = unicast;       // Direct scalar assignment
    nodes[node_count].elem_num = elem_num;     // Direct scalar assignment
    nodes[node_count].onoff_state = onoff_state;
    node_count++;  // Increment AFTER all fields set (defensive)

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
