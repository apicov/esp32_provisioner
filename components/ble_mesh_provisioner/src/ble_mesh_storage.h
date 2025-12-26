#ifndef BLE_MESH_STORAGE_H
#define BLE_MESH_STORAGE_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_ble_mesh_defs.h"

#define MESH_STORAGE_MAX_NODES 10
#define MAX_MODELS_PER_NODE 16

typedef struct {
    uint16_t model_id;
    uint16_t company_id;  // ESP_BLE_MESH_CID_NVAL for SIG models
    bool is_vendor;
    bool appkey_bound;    // Track if AppKey is bound to this model
    bool pub_configured;  // Track if publication is configured
} node_model_info_t;

typedef struct {
    uint8_t  uuid[16];
    uint16_t unicast;
    uint8_t  elem_num;
    uint8_t  onoff_state;

    // Discovered models from composition data
    node_model_info_t models[MAX_MODELS_PER_NODE];
    uint8_t model_count;

    // Configuration state tracking
    uint8_t next_model_to_bind;  // Index of next model to bind
    uint8_t next_model_to_pub;   // Index of next model to configure publication
    bool composition_received;
    bool appkey_added;
} mesh_node_info_t;

esp_err_t mesh_storage_init(void);
esp_err_t mesh_storage_add_node(const uint8_t uuid[16], uint16_t unicast, uint8_t elem_num, uint8_t onoff_state);
esp_err_t mesh_storage_get_node(uint16_t unicast, mesh_node_info_t *info);
esp_err_t mesh_storage_update_node(uint16_t unicast, const mesh_node_info_t *info);
uint16_t mesh_storage_get_node_count(void);

#endif // BLE_MESH_STORAGE_H
