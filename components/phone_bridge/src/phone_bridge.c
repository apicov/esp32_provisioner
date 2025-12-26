/**
 * Phone Bridge Implementation
 *
 * BLE GATT server that forwards mesh sensor data to smartphones
 */

#include "phone_bridge.h"
#include "esp_log.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_bt_defs.h"
#include "esp_bt_main.h"
#include "esp_gatt_common_api.h"
#include <string.h>

static const char *TAG = "PHONE_BRIDGE";

// UUIDs for our custom service
// Service UUID: 0000FFF0-0000-1000-8000-00805F9B34FB
// Characteristic UUID: 0000FFF1-0000-1000-8000-00805F9B34FB
#define PHONE_BRIDGE_SERVICE_UUID       0xFFF0
#define SENSOR_DATA_CHAR_UUID           0xFFF1

#define GATTS_TAG                       "PHONE_GATT"
#define DEVICE_NAME                     "ESP32-Mesh-Gateway"

#define GATTS_NUM_HANDLE                4

// GATT interface and connection IDs
static uint16_t phone_bridge_handle_table[GATTS_NUM_HANDLE];
static uint16_t phone_conn_id = 0;
static uint16_t phone_gatts_if = ESP_GATT_IF_NONE;
static bool phone_connected = false;
static bool notifications_enabled = false;

// Current sensor data
static sensor_data_packet_t current_data = {0};

// Service and characteristic definitions
static const uint16_t primary_service_uuid = ESP_GATT_UUID_PRI_SERVICE;
static const uint16_t character_declaration_uuid = ESP_GATT_UUID_CHAR_DECLARE;
static const uint16_t character_client_config_uuid = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;

static const uint8_t char_prop_read_notify = ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_NOTIFY;

// Service UUID
static const uint16_t service_uuid = PHONE_BRIDGE_SERVICE_UUID;

// Characteristic UUID
static const uint16_t sensor_data_uuid = SENSOR_DATA_CHAR_UUID;

// Client Characteristic Configuration Descriptor value
static uint16_t ccc_val = 0x0000;

// GATT attribute database
static const esp_gatts_attr_db_t gatt_db[GATTS_NUM_HANDLE] = {
    // Service Declaration
    [0] = {
        {ESP_GATT_AUTO_RSP},
        {
            ESP_UUID_LEN_16,
            (uint8_t *)&primary_service_uuid,
            ESP_GATT_PERM_READ,
            sizeof(uint16_t),
            sizeof(service_uuid),
            (uint8_t *)&service_uuid
        }
    },

    // Sensor Data Characteristic Declaration
    [1] = {
        {ESP_GATT_AUTO_RSP},
        {
            ESP_UUID_LEN_16,
            (uint8_t *)&character_declaration_uuid,
            ESP_GATT_PERM_READ,
            sizeof(uint8_t),
            sizeof(char_prop_read_notify),
            (uint8_t *)&char_prop_read_notify
        }
    },

    // Sensor Data Characteristic Value
    [2] = {
        {ESP_GATT_AUTO_RSP},
        {
            ESP_UUID_LEN_16,
            (uint8_t *)&sensor_data_uuid,
            ESP_GATT_PERM_READ,
            sizeof(sensor_data_packet_t),
            sizeof(current_data),
            (uint8_t *)&current_data
        }
    },

    // Sensor Data Client Characteristic Configuration Descriptor
    [3] = {
        {ESP_GATT_AUTO_RSP},
        {
            ESP_UUID_LEN_16,
            (uint8_t *)&character_client_config_uuid,
            ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
            sizeof(uint16_t),
            sizeof(ccc_val),
            (uint8_t *)&ccc_val
        }
    },
};

// GAP event handler
static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
        esp_ble_gap_start_advertising(&(esp_ble_adv_params_t){
            .adv_int_min = 0x20,
            .adv_int_max = 0x40,
            .adv_type = ADV_TYPE_IND,
            .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
            .channel_map = ADV_CHNL_ALL,
            .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
        });
        break;
    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        if (param->adv_start_cmpl.status != ESP_BT_STATUS_SUCCESS) {
            ESP_LOGE(TAG, "Advertising start failed");
        } else {
            ESP_LOGI(TAG, "Advertising started - phone can connect now");
        }
        break;
    default:
        break;
    }
}

// GATTS event handler
static void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param)
{
    ESP_LOGD(TAG, "GATTS event: %d, gatts_if: %d", event, gatts_if);

    switch (event) {
    case ESP_GATTS_REG_EVT:
        ESP_LOGI(TAG, "GATT server registered (app_id=%d, status=%d), creating service",
                 param->reg.app_id, param->reg.status);

        if (param->reg.status != ESP_GATT_OK) {
            ESP_LOGE(TAG, "GATT registration failed");
            return;
        }

        phone_gatts_if = gatts_if;

        // Set device name (mesh may override this, but set it anyway)
        esp_err_t ret = esp_ble_gap_set_device_name(DEVICE_NAME);
        ESP_LOGI(TAG, "Set device name result: %d", ret);

        // Create attribute table (don't configure advertising - mesh controls it)
        ret = esp_ble_gatts_create_attr_tab(gatt_db, gatts_if, GATTS_NUM_HANDLE, 0);
        ESP_LOGI(TAG, "Create attr table result: %d", ret);
        break;

    case ESP_GATTS_CREAT_ATTR_TAB_EVT:
        if (param->add_attr_tab.status != ESP_GATT_OK) {
            ESP_LOGE(TAG, "Create attribute table failed, error code=0x%x", param->add_attr_tab.status);
        } else {
            ESP_LOGI(TAG, "Attribute table created successfully");
            memcpy(phone_bridge_handle_table, param->add_attr_tab.handles, sizeof(phone_bridge_handle_table));
            esp_ble_gatts_start_service(phone_bridge_handle_table[0]);
        }
        break;

    case ESP_GATTS_CONNECT_EVT:
        ESP_LOGI(TAG, "Phone connected!");
        phone_conn_id = param->connect.conn_id;
        phone_connected = true;
        break;

    case ESP_GATTS_DISCONNECT_EVT:
        ESP_LOGI(TAG, "Phone disconnected");
        phone_connected = false;
        notifications_enabled = false;
        esp_ble_gap_start_advertising(&(esp_ble_adv_params_t){
            .adv_int_min = 0x20,
            .adv_int_max = 0x40,
            .adv_type = ADV_TYPE_IND,
            .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
            .channel_map = ADV_CHNL_ALL,
            .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
        });
        break;

    case ESP_GATTS_WRITE_EVT:
        // Check if CCC descriptor was written (enable/disable notifications)
        if (param->write.handle == phone_bridge_handle_table[3]) {
            uint16_t descr_value = param->write.value[1] << 8 | param->write.value[0];
            notifications_enabled = (descr_value == 0x0001);
            ESP_LOGI(TAG, "Notifications %s", notifications_enabled ? "enabled" : "disabled");
        }
        break;

    default:
        break;
    }
}

esp_err_t phone_bridge_init(void)
{
    ESP_LOGI(TAG, "Initializing phone bridge GATT service");

    // Note: BLE Mesh already initialized the BLE stack
    // We need to register our GATT service alongside mesh

    // Register GAP callback (mesh may have already registered, this adds another)
    esp_err_t ret = esp_ble_gap_register_callback(gap_event_handler);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GAP register callback failed: %d", ret);
    }

    // Register GATT server callback
    ret = esp_ble_gatts_register_callback(gatts_event_handler);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GATTS register callback failed: %d", ret);
    }

    // Register GATT server app with a unique app ID (mesh uses different IDs)
    ret = esp_ble_gatts_app_register(0x55);  // Use different app ID from mesh
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GATTS app register failed: %d", ret);
    }

    ESP_LOGI(TAG, "Phone bridge initialization complete");
    return ESP_OK;
}

esp_err_t phone_bridge_update_data(const sensor_data_packet_t *data)
{
    if (!data) {
        return ESP_ERR_INVALID_ARG;
    }

    // Update current data
    memcpy(&current_data, data, sizeof(sensor_data_packet_t));

    // Send notification if phone is connected and subscribed
    if (phone_connected && notifications_enabled && phone_gatts_if != ESP_GATT_IF_NONE) {
        esp_ble_gatts_send_indicate(
            phone_gatts_if,
            phone_conn_id,
            phone_bridge_handle_table[2],
            sizeof(sensor_data_packet_t),
            (uint8_t *)&current_data,
            false  // notification, not indication
        );
    }

    return ESP_OK;
}

bool phone_bridge_is_connected(void)
{
    return phone_connected && notifications_enabled;
}
