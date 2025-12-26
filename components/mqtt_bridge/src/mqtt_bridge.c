/**
 * MQTT Bridge Implementation
 */

#include "mqtt_bridge.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include "cJSON.h"
#include <string.h>

static const char *TAG = "MQTT_BRIDGE";

// MQTT client handle
static esp_mqtt_client_handle_t mqtt_client = NULL;
static bool mqtt_connected = false;

// MQTT Topics
#define TOPIC_SENSOR_DATA   "esp32/sensor/data"
#define TOPIC_STATUS        "esp32/status"

// MQTT event handler
static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT connected to broker");
        mqtt_connected = true;

        // Publish online status
        esp_mqtt_client_publish(mqtt_client, TOPIC_STATUS, "online", 0, 1, true);
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "MQTT disconnected from broker");
        mqtt_connected = false;
        break;

    case MQTT_EVENT_PUBLISHED:
        ESP_LOGD(TAG, "Message published, msg_id=%d", event->msg_id);
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT error occurred");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            ESP_LOGE(TAG, "Last error code: 0x%x", event->error_handle->esp_transport_sock_errno);
        }
        break;

    default:
        break;
    }
}

esp_err_t mqtt_bridge_init(const mqtt_bridge_config_t *config)
{
    if (!config || !config->broker_uri) {
        ESP_LOGE(TAG, "Invalid configuration");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Initializing MQTT bridge");
    ESP_LOGI(TAG, "Broker: %s", config->broker_uri);

    // MQTT client configuration
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = config->broker_uri,
        .credentials.client_id = config->client_id,
        .credentials.username = config->username,
        .credentials.authentication.password = config->password,
        .session.last_will.topic = TOPIC_STATUS,
        .session.last_will.msg = "offline",
        .session.last_will.qos = 1,
        .session.last_will.retain = true,
    };

    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    if (!mqtt_client) {
        ESP_LOGE(TAG, "Failed to initialize MQTT client");
        return ESP_FAIL;
    }

    // Register event handler
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID,
                                   mqtt_event_handler, NULL);

    // Start MQTT client
    esp_err_t ret = esp_mqtt_client_start(mqtt_client);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start MQTT client: %d", ret);
        return ret;
    }

    ESP_LOGI(TAG, "MQTT client started");
    return ESP_OK;
}

esp_err_t mqtt_bridge_publish_data(const mqtt_sensor_data_t *data)
{
    if (!mqtt_client) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!mqtt_connected) {
        return ESP_ERR_NOT_FINISHED;
    }

    if (!data) {
        return ESP_ERR_INVALID_ARG;
    }

    // Create JSON payload
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        ESP_LOGE(TAG, "Failed to create JSON object");
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddNumberToObject(root, "timestamp", data->timestamp);
    cJSON_AddNumberToObject(root, "accel_x", data->m5_accel_x / 1000.0);
    cJSON_AddNumberToObject(root, "accel_y", data->m5_accel_y / 1000.0);
    cJSON_AddNumberToObject(root, "accel_z", data->m5_accel_z / 1000.0);
    cJSON_AddNumberToObject(root, "gyro_x", data->m5_gyro_x / 1000.0);
    cJSON_AddNumberToObject(root, "gyro_y", data->m5_gyro_y / 1000.0);
    cJSON_AddNumberToObject(root, "gyro_z", data->m5_gyro_z / 1000.0);
    cJSON_AddNumberToObject(root, "heart_rate", data->heart_rate);
    cJSON_AddBoolToObject(root, "valid", data->data_valid);

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!json_str) {
        ESP_LOGE(TAG, "Failed to serialize JSON");
        return ESP_ERR_NO_MEM;
    }

    // Publish to MQTT
    int msg_id = esp_mqtt_client_publish(mqtt_client, TOPIC_SENSOR_DATA,
                                         json_str, 0, 0, false);

    free(json_str);

    if (msg_id < 0) {
        ESP_LOGE(TAG, "Failed to publish message");
        return ESP_FAIL;
    }

    ESP_LOGD(TAG, "Published sensor data, msg_id=%d", msg_id);
    return ESP_OK;
}

bool mqtt_bridge_is_connected(void)
{
    return mqtt_connected;
}
