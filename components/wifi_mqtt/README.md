# WiFi-MQTT Component

A reusable ESP-IDF component providing simple WiFi and MQTT functionality with automatic reconnection and clean callback interface.

## Features

- ✅ Automatic WiFi connection with configurable retry logic
- ✅ Auto-reconnect on WiFi/MQTT disconnection
- ✅ Simple publish/subscribe interface
- ✅ QoS support (0, 1, 2)
- ✅ Event callbacks for all connection states
- ✅ Thread-safe message publishing
- ✅ Binary data support
- ✅ MQTT topic wildcards (# and +)
- ✅ Connection state monitoring
- ✅ IP address and RSSI utilities

## Installation

1. Copy the `wifi_mqtt` folder to your project's `components/` directory:
   ```
   your_project/
   ├── main/
   └── components/
       └── wifi_mqtt/
   ```

2. The component will be automatically detected by ESP-IDF build system.

## Quick Start

### Example 1: Simple Sensor Publisher

```c
#include "wifi_mqtt.h"
#include "esp_log.h"

#define TAG "SENSOR"

// Callback when MQTT is connected
void on_mqtt_connected(void) {
    ESP_LOGI(TAG, "MQTT connected - ready to publish");
}

void app_main(void)
{
    // Configure WiFi-MQTT
    wifi_mqtt_config_t config = {
        .wifi_ssid = "YourWiFiSSID",
        .wifi_password = "YourPassword",
        .mqtt_broker_uri = "mqtt://broker.hivemq.com",
        .callbacks = {
            .mqtt_connected = on_mqtt_connected,
        }
    };

    // Initialize and start
    ESP_ERROR_CHECK(wifi_mqtt_init(&config));
    ESP_ERROR_CHECK(wifi_mqtt_start());

    // Wait for connection
    while (!wifi_mqtt_is_mqtt_connected()) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    // Publish sensor data every 5 seconds
    while (1) {
        float temperature = 25.5;  // Read from sensor
        float humidity = 60.0;

        char json[128];
        snprintf(json, sizeof(json),
                "{\"temperature\":%.1f,\"humidity\":%.1f}",
                temperature, humidity);

        wifi_mqtt_publish("home/living_room/sensor", json, 0);

        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
```

### Example 2: Command Receiver (Subscribe)

```c
#include "wifi_mqtt.h"
#include "driver/gpio.h"

#define LED_GPIO 2
#define TAG "LED_CONTROLLER"

// Callback when message received
void on_message(const char *topic, const char *data, int data_len) {
    char msg[64];
    snprintf(msg, sizeof(msg), "%.*s", data_len, data);

    ESP_LOGI(TAG, "Received: %s = %s", topic, msg);

    // Control LED based on message
    if (strcmp(msg, "ON") == 0) {
        gpio_set_level(LED_GPIO, 1);
    } else if (strcmp(msg, "OFF") == 0) {
        gpio_set_level(LED_GPIO, 0);
    }
}

// Callback when MQTT connected
void on_mqtt_connected(void) {
    ESP_LOGI(TAG, "MQTT connected - subscribing to topics");
    wifi_mqtt_subscribe("home/living_room/led/command", 0);
}

void app_main(void)
{
    // Configure LED GPIO
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << LED_GPIO),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io_conf);

    // Configure WiFi-MQTT
    wifi_mqtt_config_t config = {
        .wifi_ssid = "YourWiFiSSID",
        .wifi_password = "YourPassword",
        .mqtt_broker_uri = "mqtt://broker.hivemq.com",
        .callbacks = {
            .mqtt_connected = on_mqtt_connected,
            .message_received = on_message,
        }
    };

    // Initialize and start
    ESP_ERROR_CHECK(wifi_mqtt_init(&config));
    ESP_ERROR_CHECK(wifi_mqtt_start());
}
```

### Example 3: BLE Mesh → MQTT Bridge

```c
#include "wifi_mqtt.h"
#include "ble_mesh_provisioner.h"

#define TAG "MESH_BRIDGE"

// Forward BLE Mesh vendor messages to MQTT
void mesh_vendor_client_cb(esp_ble_mesh_model_cb_event_t event,
                           esp_ble_mesh_model_cb_param_t *param)
{
    if (event == ESP_BLE_MESH_MODEL_OPERATION_EVT) {
        uint32_t opcode = param->model_operation.opcode;
        uint8_t *data = param->model_operation.msg;
        uint16_t length = param->model_operation.length;
        uint16_t src_addr = param->model_operation.ctx->addr;

        if (opcode == VENDOR_MODEL_OP_IMU_DATA) {
            // Decode IMU data from BLE Mesh node
            typedef struct {
                uint16_t timestamp_ms;
                int8_t accel_x, accel_y, accel_z;
                int8_t gyro_x, gyro_y, gyro_z;
            } __attribute__((packed)) imu_compact_data_t;

            imu_compact_data_t *imu = (imu_compact_data_t*)data;

            // Convert to JSON
            char json[256];
            snprintf(json, sizeof(json),
                    "{\"node\":\"0x%04x\","
                    "\"timestamp\":%u,"
                    "\"accel\":[%.1f,%.1f,%.1f],"
                    "\"gyro\":[%d,%d,%d]}",
                    src_addr,
                    imu->timestamp_ms,
                    imu->accel_x * 0.1f, imu->accel_y * 0.1f, imu->accel_z * 0.1f,
                    imu->gyro_x * 10, imu->gyro_y * 10, imu->gyro_z * 10);

            // Publish to MQTT
            char topic[64];
            snprintf(topic, sizeof(topic), "mesh/node/0x%04x/imu", src_addr);
            wifi_mqtt_publish(topic, json, 0);
        }
    }
}

void on_mqtt_connected(void) {
    ESP_LOGI(TAG, "MQTT connected - bridge active");

    // Subscribe to commands for mesh nodes
    wifi_mqtt_subscribe("mesh/command/#", 0);
}

void on_message(const char *topic, const char *data, int data_len) {
    // Parse topic: mesh/command/0x0005/onoff
    uint16_t node_addr;
    char command[32];

    if (sscanf(topic, "mesh/command/0x%hx/%s", &node_addr, command) == 2) {
        if (strcmp(command, "onoff") == 0) {
            // Parse ON/OFF command
            char msg[16];
            snprintf(msg, sizeof(msg), "%.*s", data_len, data);

            bool onoff = (strcmp(msg, "ON") == 0);
            provisioner_send_onoff(node_addr, onoff);
        }
    }
}

void app_main(void)
{
    // Initialize WiFi-MQTT
    wifi_mqtt_config_t wifi_config = {
        .wifi_ssid = CONFIG_WIFI_SSID,
        .wifi_password = CONFIG_WIFI_PASSWORD,
        .mqtt_broker_uri = CONFIG_MQTT_BROKER_URI,
        .callbacks = {
            .mqtt_connected = on_mqtt_connected,
            .message_received = on_message,
        }
    };
    wifi_mqtt_init(&wifi_config);
    wifi_mqtt_start();

    // Initialize BLE Mesh Provisioner
    provisioner_config_t prov_config = {
        .own_address = 0x0001,
        .node_start_address = 0x0005,
        .match_prefix = {0xdd, 0xdd},
        .net_idx = 0,
        .app_idx = 0,
    };
    provisioner_init(&prov_config, NULL);
    provisioner_start();

    ESP_LOGI(TAG, "BLE Mesh ↔ MQTT Bridge started");
}
```

## API Reference

### Configuration

```c
typedef struct {
    // WiFi settings
    const char *wifi_ssid;
    const char *wifi_password;
    uint8_t max_wifi_retry;  // 0 = infinite

    // MQTT settings
    const char *mqtt_broker_uri;       // Required
    const char *mqtt_username;         // Optional
    const char *mqtt_password;         // Optional
    const char *mqtt_client_id;        // Optional (auto-generated if NULL)
    uint16_t mqtt_port;                // Optional (default: 1883 for mqtt://, 8883 for mqtts://)
    uint16_t mqtt_keepalive;           // Optional (default: 120 seconds)

    // Callbacks
    wifi_mqtt_callbacks_t callbacks;
} wifi_mqtt_config_t;
```

### Callbacks

```c
typedef struct {
    void (*wifi_connected)(void);
    void (*wifi_disconnected)(void);
    void (*mqtt_connected)(void);
    void (*mqtt_disconnected)(void);
    void (*message_received)(const char *topic, const char *data, int data_len);
    void (*message_published)(int msg_id);
} wifi_mqtt_callbacks_t;
```

### Initialization Functions

```c
// Initialize component
esp_err_t wifi_mqtt_init(const wifi_mqtt_config_t *config);

// Start WiFi and MQTT
esp_err_t wifi_mqtt_start(void);

// Stop WiFi and MQTT
esp_err_t wifi_mqtt_stop(void);

// Check connection status
bool wifi_mqtt_is_wifi_connected(void);
bool wifi_mqtt_is_mqtt_connected(void);
```

### Publish Functions

```c
// Publish string message
int wifi_mqtt_publish(const char *topic, const char *data, int qos);

// Publish binary data
int wifi_mqtt_publish_binary(const char *topic, const void *data, int len, int qos);
```

**QoS Levels**:
- `0`: At most once (fire and forget)
- `1`: At least once (acknowledged)
- `2`: Exactly once (assured delivery)

### Subscribe Functions

```c
// Subscribe to topic
int wifi_mqtt_subscribe(const char *topic, int qos);

// Unsubscribe from topic
int wifi_mqtt_unsubscribe(const char *topic);
```

**Topic Wildcards**:
- `#` - Multi-level wildcard (e.g., `sensor/#` matches `sensor/temp`, `sensor/room/temp`)
- `+` - Single-level wildcard (e.g., `sensor/+/temp` matches `sensor/room1/temp`)

### Utility Functions

```c
// Get WiFi IP address
esp_err_t wifi_mqtt_get_ip_address(char *ip_str, size_t len);

// Get WiFi signal strength (RSSI in dBm)
int8_t wifi_mqtt_get_rssi(void);
```

## Configuration via menuconfig

You can use Kconfig to make WiFi/MQTT settings configurable:

Create `components/wifi_mqtt/Kconfig.projbuild`:

```
menu "WiFi-MQTT Configuration"

config WIFI_SSID
    string "WiFi SSID"
    default "myssid"
    help
        WiFi network name

config WIFI_PASSWORD
    string "WiFi Password"
    default "mypassword"
    help
        WiFi password

config MQTT_BROKER_URI
    string "MQTT Broker URI"
    default "mqtt://broker.hivemq.com"
    help
        MQTT broker URI (e.g., mqtt://broker.example.com or mqtts://secure.example.com)

config MQTT_USERNAME
    string "MQTT Username"
    default ""
    help
        MQTT username (leave empty if no auth)

config MQTT_PASSWORD
    string "MQTT Password"
    default ""
    help
        MQTT password (leave empty if no auth)

endmenu
```

Then use in your code:

```c
wifi_mqtt_config_t config = {
    .wifi_ssid = CONFIG_WIFI_SSID,
    .wifi_password = CONFIG_WIFI_PASSWORD,
    .mqtt_broker_uri = CONFIG_MQTT_BROKER_URI,
    // ...
};
```

## Troubleshooting

### WiFi Won't Connect

**Check**:
1. SSID and password are correct
2. WiFi network is 2.4 GHz (ESP32 doesn't support 5 GHz)
3. Check logs for specific error

```c
esp_log_level_set("WIFI_MQTT", ESP_LOG_DEBUG);
```

### MQTT Connection Fails

**Check**:
1. Broker URI is correct (starts with `mqtt://` or `mqtts://`)
2. Broker is reachable (try from another client)
3. Firewall allows port 1883 (or 8883 for TLS)
4. Username/password if required

**Test with public broker**:
```c
.mqtt_broker_uri = "mqtt://test.mosquitto.org"
```

### Messages Not Received

**Check**:
1. `message_received` callback is set
2. Subscribed to correct topic
3. QoS level matches expectations
4. Topic wildcards are correct

### Memory Issues

**Increase stack size**:
```c
// In menuconfig:
CONFIG_ESP_MAIN_TASK_STACK_SIZE=8192
```

**Monitor free heap**:
```c
ESP_LOGI(TAG, "Free heap: %d", esp_get_free_heap_size());
```

## Thread Safety

- ✅ `wifi_mqtt_publish()` - Thread-safe
- ✅ `wifi_mqtt_publish_binary()` - Thread-safe
- ✅ `wifi_mqtt_subscribe()` - Thread-safe
- ✅ `wifi_mqtt_unsubscribe()` - Thread-safe
- ⚠️ Callbacks - Called from event loop task (don't block!)

**Callback Best Practices**:
```c
// ❌ BAD - Blocking operation in callback
void on_message(const char *topic, const char *data, int data_len) {
    vTaskDelay(pdMS_TO_TICKS(1000));  // Don't do this!
}

// ✅ GOOD - Queue message for processing
QueueHandle_t msg_queue;

void on_message(const char *topic, const char *data, int data_len) {
    message_t msg;
    strncpy(msg.topic, topic, sizeof(msg.topic));
    memcpy(msg.data, data, data_len);
    msg.len = data_len;
    xQueueSend(msg_queue, &msg, 0);  // Non-blocking
}
```

## Performance

**Typical Performance** (ESP32 @ 240 MHz):
- WiFi connection time: 2-5 seconds
- MQTT connection time: 0.5-1 second
- Publish latency (QoS 0): <10 ms
- Publish latency (QoS 1): ~50-100 ms
- Max throughput: ~1 MB/s (limited by WiFi)

**Optimization Tips**:
1. Use QoS 0 for high-frequency data
2. Batch small messages into JSON arrays
3. Use binary encoding for large datasets
4. Enable MQTT persistence for reliability

## Examples in This Repository

- **Simple sensor**: See example above
- **BLE Mesh bridge**: `esp32_provisioner/main/main_with_mqtt.c`
- **Phone bridge**: `esp32_provisioner/main/main_with_bridge.c`

## License

This component is part of the ESP32_BLE_mesh project.
