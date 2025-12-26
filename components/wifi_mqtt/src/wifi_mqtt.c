/*
 * ============================================================================
 *                   WiFi-MQTT COMPONENT - IMPLEMENTATION
 * ============================================================================
 */

#include "wifi_mqtt.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "mqtt_client.h"
#include <string.h>

#define TAG "WIFI_MQTT"

/*
 * ============================================================================
 *                           INTERNAL STATE
 * ============================================================================
 */

static wifi_mqtt_config_t s_config = {0};
static wifi_mqtt_callbacks_t s_callbacks = {0};
static esp_mqtt_client_handle_t s_mqtt_client = NULL;
static esp_netif_t *s_netif = NULL;

// Connection state
static bool s_wifi_connected = false;
static bool s_mqtt_connected = false;
static uint8_t s_wifi_retry_count = 0;

/*
 * ============================================================================
 *                         WiFi EVENT HANDLERS
 * ============================================================================
 */

/**
 * WiFi event handler
 */
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
        case WIFI_EVENT_STA_START:
            ESP_LOGI(TAG, "WiFi started, connecting to AP...");
            esp_wifi_connect();
            break;

        case WIFI_EVENT_STA_DISCONNECTED:
            s_wifi_connected = false;

            // Call user callback
            if (s_callbacks.wifi_disconnected) {
                s_callbacks.wifi_disconnected();
            }

            // Auto-reconnect logic
            if (s_config.auto_reconnect) {
                if (s_config.max_wifi_retry == 0 ||
                    s_wifi_retry_count < s_config.max_wifi_retry) {

                    s_wifi_retry_count++;
                    ESP_LOGI(TAG, "WiFi disconnected, retrying... (%d)", s_wifi_retry_count);
                    esp_wifi_connect();
                } else {
                    ESP_LOGE(TAG, "WiFi connection failed after %d retries", s_wifi_retry_count);
                }
            } else {
                ESP_LOGI(TAG, "WiFi disconnected (auto-reconnect disabled)");
            }
            break;

        default:
            break;
        }
    } else if (event_base == IP_EVENT) {
        if (event_id == IP_EVENT_STA_GOT_IP) {
            ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
            ESP_LOGI(TAG, "WiFi connected! IP: " IPSTR, IP2STR(&event->ip_info.ip));

            s_wifi_connected = true;
            s_wifi_retry_count = 0;  // Reset retry counter

            // Call user callback
            if (s_callbacks.wifi_connected) {
                s_callbacks.wifi_connected();
            }

            // Start MQTT connection
            if (s_mqtt_client) {
                esp_mqtt_client_start(s_mqtt_client);
            }
        }
    }
}

/*
 * ============================================================================
 *                         MQTT EVENT HANDLERS
 * ============================================================================
 */

/**
 * MQTT event handler
 */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                                int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT connected to broker");
        s_mqtt_connected = true;

        // Call user callback
        if (s_callbacks.mqtt_connected) {
            s_callbacks.mqtt_connected();
        }
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT disconnected from broker");
        s_mqtt_connected = false;

        // Call user callback
        if (s_callbacks.mqtt_disconnected) {
            s_callbacks.mqtt_disconnected();
        }
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT subscribed, msg_id=%d", event->msg_id);
        break;

    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT unsubscribed, msg_id=%d", event->msg_id);
        break;

    case MQTT_EVENT_PUBLISHED:
        ESP_LOGD(TAG, "MQTT published, msg_id=%d", event->msg_id);

        // Call user callback
        if (s_callbacks.message_published) {
            s_callbacks.message_published(event->msg_id);
        }
        break;

    case MQTT_EVENT_DATA:
        ESP_LOGD(TAG, "MQTT data received: topic=%.*s", event->topic_len, event->topic);

        // Call user callback
        if (s_callbacks.message_received) {
            // Create null-terminated topic string
            char topic[128];
            int topic_len = event->topic_len < sizeof(topic) - 1 ?
                           event->topic_len : sizeof(topic) - 1;
            memcpy(topic, event->topic, topic_len);
            topic[topic_len] = '\0';

            s_callbacks.message_received(topic, event->data, event->data_len);
        }
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT error");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            ESP_LOGE(TAG, "  TCP error: 0x%x", event->error_handle->esp_transport_sock_errno);
        }
        break;

    default:
        ESP_LOGD(TAG, "MQTT event: %d", event_id);
        break;
    }
}

/*
 * ============================================================================
 *                       INITIALIZATION FUNCTIONS
 * ============================================================================
 */

/**
 * Initialize WiFi in station mode
 */
static esp_err_t wifi_init(void)
{
    esp_err_t ret;

    // Initialize TCP/IP stack
    ret = esp_netif_init();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to init netif");
        return ret;
    }

    // Create default event loop (if not already created)
    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to create event loop");
        return ret;
    }

    // Create WiFi station interface
    if (!s_netif) {
        s_netif = esp_netif_create_default_wifi_sta();
        if (!s_netif) {
            ESP_LOGE(TAG, "Failed to create WiFi STA interface");
            return ESP_FAIL;
        }
    }

    // Initialize WiFi with default config
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init WiFi");
        return ret;
    }

    // Register event handlers
    ret = esp_event_handler_instance_register(WIFI_EVENT,
                                               ESP_EVENT_ANY_ID,
                                               &wifi_event_handler,
                                               NULL,
                                               NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register WiFi event handler");
        return ret;
    }

    ret = esp_event_handler_instance_register(IP_EVENT,
                                               IP_EVENT_STA_GOT_IP,
                                               &wifi_event_handler,
                                               NULL,
                                               NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register IP event handler");
        return ret;
    }

    // Configure WiFi
    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .capable = true,
                .required = false
            },
        },
    };

    // Copy SSID and password
    strncpy((char*)wifi_config.sta.ssid, s_config.wifi_ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char*)wifi_config.sta.password, s_config.wifi_password, sizeof(wifi_config.sta.password) - 1);

    // Set WiFi mode and config
    ret = esp_wifi_set_mode(WIFI_MODE_STA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set WiFi mode");
        return ret;
    }

    ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set WiFi config");
        return ret;
    }

    ESP_LOGI(TAG, "WiFi initialized");
    return ESP_OK;
}

/**
 * Initialize MQTT client
 */
static esp_err_t mqtt_init(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = s_config.mqtt_broker_uri,
    };

    // Optional: Set username/password
    if (s_config.mqtt_username) {
        mqtt_cfg.credentials.username = s_config.mqtt_username;
    }
    if (s_config.mqtt_password) {
        mqtt_cfg.credentials.authentication.password = s_config.mqtt_password;
    }

    // Optional: Set client ID
    if (s_config.mqtt_client_id) {
        mqtt_cfg.credentials.client_id = s_config.mqtt_client_id;
    }

    // Optional: Set port
    if (s_config.mqtt_port > 0) {
        mqtt_cfg.broker.address.port = s_config.mqtt_port;
    }

    // Optional: Set keepalive
    if (s_config.mqtt_keepalive > 0) {
        mqtt_cfg.session.keepalive = s_config.mqtt_keepalive;
    } else {
        mqtt_cfg.session.keepalive = 120;  // Default 120 seconds
    }

    // Create MQTT client
    s_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    if (!s_mqtt_client) {
        ESP_LOGE(TAG, "Failed to init MQTT client");
        return ESP_FAIL;
    }

    // Register MQTT event handler
    esp_err_t ret = esp_mqtt_client_register_event(s_mqtt_client,
                                                    ESP_EVENT_ANY_ID,
                                                    mqtt_event_handler,
                                                    NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register MQTT event handler");
        return ret;
    }

    ESP_LOGI(TAG, "MQTT client initialized");
    return ESP_OK;
}

/*
 * ============================================================================
 *                          PUBLIC API FUNCTIONS
 * ============================================================================
 */

esp_err_t wifi_mqtt_init(const wifi_mqtt_config_t *config)
{
    esp_err_t ret;

    if (!config) {
        ESP_LOGE(TAG, "Invalid config");
        return ESP_ERR_INVALID_ARG;
    }

    if (!config->wifi_ssid || !config->mqtt_broker_uri) {
        ESP_LOGE(TAG, "Missing required config (SSID or broker URI)");
        return ESP_ERR_INVALID_ARG;
    }

    // Store configuration
    memcpy(&s_config, config, sizeof(wifi_mqtt_config_t));
    memcpy(&s_callbacks, &config->callbacks, sizeof(wifi_mqtt_callbacks_t));

    // Set defaults
    if (s_config.max_wifi_retry == 0) {
        s_config.max_wifi_retry = 5;  // Default: 5 retries
    }
    s_config.auto_reconnect = true;  // Always enable auto-reconnect

    // Initialize NVS (required for WiFi)
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init NVS");
        return ret;
    }

    // Initialize WiFi
    ret = wifi_init();
    if (ret != ESP_OK) {
        return ret;
    }

    // Initialize MQTT
    ret = mqtt_init();
    if (ret != ESP_OK) {
        return ret;
    }

    ESP_LOGI(TAG, "WiFi-MQTT component initialized");
    return ESP_OK;
}

esp_err_t wifi_mqtt_start(void)
{
    esp_err_t ret;

    // Start WiFi
    ret = esp_wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start WiFi");
        return ret;
    }

    ESP_LOGI(TAG, "WiFi-MQTT started");
    return ESP_OK;
}

esp_err_t wifi_mqtt_stop(void)
{
    // Stop MQTT
    if (s_mqtt_client) {
        esp_mqtt_client_stop(s_mqtt_client);
    }

    // Stop WiFi
    esp_wifi_stop();

    s_wifi_connected = false;
    s_mqtt_connected = false;

    ESP_LOGI(TAG, "WiFi-MQTT stopped");
    return ESP_OK;
}

bool wifi_mqtt_is_wifi_connected(void)
{
    return s_wifi_connected;
}

bool wifi_mqtt_is_mqtt_connected(void)
{
    return s_mqtt_connected;
}

int wifi_mqtt_publish(const char *topic, const char *data, int qos)
{
    if (!s_mqtt_connected) {
        ESP_LOGW(TAG, "MQTT not connected, cannot publish");
        return -1;
    }

    if (!topic || !data) {
        ESP_LOGE(TAG, "Invalid topic or data");
        return -1;
    }

    int msg_id = esp_mqtt_client_publish(s_mqtt_client, topic, data, 0, qos, 0);
    if (msg_id < 0) {
        ESP_LOGE(TAG, "Failed to publish message");
    }

    return msg_id;
}

int wifi_mqtt_publish_binary(const char *topic, const void *data, int len, int qos)
{
    if (!s_mqtt_connected) {
        ESP_LOGW(TAG, "MQTT not connected, cannot publish");
        return -1;
    }

    if (!topic || !data) {
        ESP_LOGE(TAG, "Invalid topic or data");
        return -1;
    }

    int msg_id = esp_mqtt_client_publish(s_mqtt_client, topic, data, len, qos, 0);
    if (msg_id < 0) {
        ESP_LOGE(TAG, "Failed to publish binary message");
    }

    return msg_id;
}

int wifi_mqtt_subscribe(const char *topic, int qos)
{
    if (!s_mqtt_connected) {
        ESP_LOGW(TAG, "MQTT not connected, cannot subscribe");
        return -1;
    }

    if (!topic) {
        ESP_LOGE(TAG, "Invalid topic");
        return -1;
    }

    int msg_id = esp_mqtt_client_subscribe(s_mqtt_client, topic, qos);
    if (msg_id < 0) {
        ESP_LOGE(TAG, "Failed to subscribe to topic: %s", topic);
    } else {
        ESP_LOGI(TAG, "Subscribed to: %s (QoS %d)", topic, qos);
    }

    return msg_id;
}

int wifi_mqtt_unsubscribe(const char *topic)
{
    if (!s_mqtt_connected) {
        ESP_LOGW(TAG, "MQTT not connected, cannot unsubscribe");
        return -1;
    }

    if (!topic) {
        ESP_LOGE(TAG, "Invalid topic");
        return -1;
    }

    int msg_id = esp_mqtt_client_unsubscribe(s_mqtt_client, topic);
    if (msg_id < 0) {
        ESP_LOGE(TAG, "Failed to unsubscribe from topic: %s", topic);
    } else {
        ESP_LOGI(TAG, "Unsubscribed from: %s", topic);
    }

    return msg_id;
}

esp_err_t wifi_mqtt_get_ip_address(char *ip_str, size_t len)
{
    if (!s_wifi_connected || !s_netif) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_netif_ip_info_t ip_info;
    esp_err_t ret = esp_netif_get_ip_info(s_netif, &ip_info);
    if (ret != ESP_OK) {
        return ret;
    }

    snprintf(ip_str, len, IPSTR, IP2STR(&ip_info.ip));
    return ESP_OK;
}

int8_t wifi_mqtt_get_rssi(void)
{
    if (!s_wifi_connected) {
        return 0;
    }

    wifi_ap_record_t ap_info;
    esp_err_t ret = esp_wifi_sta_get_ap_info(&ap_info);
    if (ret != ESP_OK) {
        return 0;
    }

    return ap_info.rssi;
}
