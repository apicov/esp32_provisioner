#ifndef BLE_MESH_AUTO_CONFIG_H
#define BLE_MESH_AUTO_CONFIG_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_ble_mesh_config_model_api.h"
#include "ble_mesh_storage.h"

/*
 * Key structure for provisioner
 * This must match the definition in ble_mesh_callbacks.c
 */
struct esp_ble_mesh_key {
    uint16_t net_idx;      // Network key index
    uint16_t app_idx;      // Application key index
    uint8_t  app_key[16];  // 128-bit application key
};

/*
 * Bind next model to AppKey
 * Returns true if binding initiated, false if all models bound
 */
bool bind_next_model(uint16_t addr, mesh_node_info_t *node_info,
                     esp_ble_mesh_client_common_param_t *common,
                     struct esp_ble_mesh_key *prov_key);

/*
 * Configure publication for next model
 * Returns true if config initiated, false if all models configured
 */
bool configure_next_publication(uint16_t addr, mesh_node_info_t *node_info,
                                esp_ble_mesh_client_common_param_t *common,
                                struct esp_ble_mesh_key *prov_key);

/*
 * Configure subscription for next model
 * Returns true if config initiated, false if all models configured
 */
bool subscribe_next_model(uint16_t addr, mesh_node_info_t *node_info,
                          esp_ble_mesh_client_common_param_t *common,
                          struct esp_ble_mesh_key *prov_key);

#endif // BLE_MESH_AUTO_CONFIG_H
