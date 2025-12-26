# BLE Mesh to MQTT Bridge - User Guide

A comprehensive guide for using and extending the ESP32 BLE Mesh to MQTT Bridge.

---

## Table of Contents

1. [Overview](#overview)
2. [Quick Start](#quick-start)
3. [Architecture](#architecture)
4. [Configuration](#configuration)
5. [MQTT Topics](#mqtt-topics)
6. [Adding New Message Types](#adding-new-message-types)
7. [Testing](#testing)
8. [Troubleshooting](#troubleshooting)
9. [Examples](#examples)

---

## Overview

This bridge connects BLE Mesh sensor nodes to MQTT, enabling:

- **Cloud connectivity**: Send sensor data to cloud platforms (AWS IoT, Azure, Google Cloud)
- **Smartphone apps**: Control mesh nodes via MQTT from mobile apps
- **Data logging**: Store sensor data in databases (InfluxDB, MongoDB, etc.)
- **Integration**: Connect to Home Assistant, Node-RED, etc.

### What It Does

```
┌──────────────┐         ┌──────────────┐         ┌──────────────┐
│  M5Stick     │         │    ESP32     │         │    MQTT      │
│  (IMU Node)  │ ─Mesh─→ │   Bridge     │ ─WiFi─→ │   Broker     │
└──────────────┘         └──────────────┘         └──────────────┘
   Sensors                 This App                  Cloud/Apps
```

**Data Flow**:
1. M5Stick reads IMU sensor (10 Hz)
2. Sends compressed data via BLE Mesh (8 bytes)
3. Bridge receives and decodes
4. Publishes JSON to MQTT topic `mesh/0x0005/imu`
5. Cloud/apps receive real-time sensor data

---

## Quick Start

### Prerequisites

- ESP32 development board (for bridge)
- M5StickC Plus (or any BLE Mesh node)
- MQTT broker (local or cloud)
- WiFi network

### Step 1: Configure Project

```bash
cd esp32_provisioner
idf.py menuconfig
```

Navigate to **WiFi-MQTT Configuration** and set:

- WiFi SSID: Your WiFi network name
- WiFi Password: Your WiFi password
- MQTT Broker URI: `mqtt://broker.hivemq.com` (or your broker)
- MQTT Username: (optional)
- MQTT Password: (optional)
- MQTT Client ID: `esp32_mesh_bridge`

### Step 2: Build and Flash

```bash
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

### Step 3: Power On M5Stick Node

The M5Stick will:
1. Broadcast unprovisioned device beacon
2. Get auto-provisioned by the bridge
3. Start sending IMU data

### Step 4: Monitor MQTT Messages

Use any MQTT client:

**Command line**:
```bash
mosquitto_sub -h broker.hivemq.com -t "mesh/#" -v
```

**MQTT Explorer** (GUI):
- Download from http://mqtt-explorer.com/
- Connect to your broker
- Subscribe to `mesh/#`

You should see:
```
mesh/0x0005/imu {"node":"0x0005","timestamp":1234,"accel":{"x":0.12,"y":-0.05,"z":0.98},"gyro":{"x":5,"y":-2,"z":1}}
```

---

## Architecture

### Components

```
┌─────────────────────────────────────────────────────────────┐
│                    ESP32 Bridge Application                 │
│                                                             │
│  ┌──────────────────┐  ┌──────────────────┐                │
│  │  BLE Mesh        │  │  WiFi-MQTT       │                │
│  │  Provisioner     │  │  Component       │                │
│  └────────┬─────────┘  └────────┬─────────┘                │
│           │                     │                           │
│           │  Vendor Messages    │  MQTT Messages            │
│           ↓                     ↓                           │
│  ┌─────────────────────────────────────────┐                │
│  │     Message Router (main_with_mqtt.c)   │                │
│  │  • Routes opcodes to handlers           │                │
│  │  • Decodes sensor data                  │                │
│  │  • Publishes to MQTT                    │                │
│  │  • Handles MQTT commands                │                │
│  └─────────────────────────────────────────┘                │
└─────────────────────────────────────────────────────────────┘
```

### Message Flow

**Mesh → MQTT** (Sensor Data):
```
1. M5Stick: imu_data_t → BLE Mesh (opcode 0xC00001)
2. Bridge:  mesh_vendor_msg_handler() receives
3. Router:  Finds handle_imu_message()
4. Handler: Decodes → Creates JSON
5. MQTT:    Publishes to "mesh/0x0005/imu"
```

**MQTT → Mesh** (Commands):
```
1. MQTT:    Receive "mesh/command/0x0005/onoff" = "ON"
2. Bridge:  on_mqtt_message() parses topic
3. Handler: Calls provisioner_send_onoff(0x0005, true)
4. Mesh:    Sends Generic OnOff SET message
5. Node:    LED turns ON
```

---

## Configuration

### WiFi Settings

Edit via `menuconfig` or directly in `sdkconfig`:

```
CONFIG_WIFI_SSID="YourWiFiName"
CONFIG_WIFI_PASSWORD="YourPassword"
CONFIG_WIFI_MAXIMUM_RETRY=5
```

### MQTT Settings

```
CONFIG_MQTT_BROKER_URI="mqtt://broker.hivemq.com"
CONFIG_MQTT_CLIENT_ID="esp32_mesh_bridge"
CONFIG_MQTT_USERNAME=""
CONFIG_MQTT_PASSWORD=""
```

**Common MQTT Brokers**:

| Broker | URI | Notes |
|--------|-----|-------|
| HiveMQ Public | `mqtt://broker.hivemq.com` | Free, public, no auth |
| Mosquitto Local | `mqtt://192.168.1.100` | Run locally |
| AWS IoT | `mqtts://xxx.iot.region.amazonaws.com` | Requires TLS + certs |
| Azure IoT Hub | `mqtts://xxx.azure-devices.net` | Requires TLS + SAS |

### BLE Mesh Settings

```
CONFIG_MESH_UUID_PREFIX_0=0xDD
CONFIG_MESH_UUID_PREFIX_1=0xDD
```

**Important**: This must match the UUID prefix in your M5Stick nodes!

---

## MQTT Topics

### Published by Bridge (Mesh → MQTT)

| Topic | Payload | Rate | Description |
|-------|---------|------|-------------|
| `mesh/0x<addr>/imu` | JSON | 10 Hz | IMU sensor data |
| `mesh/0x<addr>/battery` | JSON | Variable | Battery status (future) |
| `mesh/events/provisioned` | JSON | On event | Node provisioned event |

**Example Payloads**:

**IMU Data**:
```json
{
  "node": "0x0005",
  "timestamp": 12345,
  "accel": {"x": 0.12, "y": -0.05, "z": 0.98},
  "gyro": {"x": 5, "y": -2, "z": 1}
}
```

**Provisioned Event**:
```json
{
  "address": "0x0005",
  "elements": 1
}
```

### Subscribed by Bridge (MQTT → Mesh)

| Topic | Payload | Description |
|-------|---------|-------------|
| `mesh/command/0x<addr>/onoff` | `ON` or `OFF` | Control LED |

**Examples**:
```bash
# Turn on LED on node 0x0005
mosquitto_pub -h broker.hivemq.com -t "mesh/command/0x0005/onoff" -m "ON"

# Turn off
mosquitto_pub -h broker.hivemq.com -t "mesh/command/0x0005/onoff" -m "OFF"
```

---

## Adding New Message Types

The bridge is designed to be easily extensible. Here's how to add support for new sensor types.

### Example: Adding Temperature Sensor

#### Step 1: Define Opcode

In `main_with_mqtt.c`:

```c
#define VENDOR_OP_TEMPERATURE   0xC00004    // Temperature sensor
```

#### Step 2: Define Data Structure

```c
/**
 * Temperature Data Structure (4 bytes)
 */
typedef struct {
    uint16_t timestamp_ms;      // 2 bytes: Timestamp
    int16_t temperature;        // 2 bytes: Temperature in 0.01°C units
} __attribute__((packed)) temperature_data_t;
```

**Why 0.01°C units?**
- Range: -327.68°C to +327.67°C (covers all sensors)
- Precision: 0.01°C (suitable for most applications)
- Size: 2 bytes (fits in single BLE Mesh segment)

#### Step 3: Create Handler Function

```c
/**
 * Handle temperature message
 */
static void handle_temperature_message(uint16_t src_addr,
                                        const uint8_t *data,
                                        uint16_t length)
{
    if (length != sizeof(temperature_data_t)) {
        ESP_LOGW(TAG, "Invalid temperature message length: %d", length);
        return;
    }

    const temperature_data_t *temp = (const temperature_data_t *)data;

    // Decode: 0.01°C units → °C
    float temperature_c = temp->temperature * 0.01f;

    // Build JSON payload
    char json[128];
    snprintf(json, sizeof(json),
            "{"
            "\"node\":\"0x%04x\","
            "\"timestamp\":%u,"
            "\"temperature\":%.2f"
            "}",
            src_addr,
            temp->timestamp_ms,
            temperature_c);

    // Publish to MQTT
    char topic[64];
    snprintf(topic, sizeof(topic), "mesh/0x%04x/temperature", src_addr);

    if (wifi_mqtt_publish(topic, json, 0) > 0) {
        ESP_LOGI(TAG, "📤 Published temp from 0x%04x: %.2f°C",
                 src_addr, temperature_c);
    }
}
```

#### Step 4: Add to Router

```c
static const message_route_t message_router[] = {
    { VENDOR_OP_IMU_DATA,       handle_imu_message,         "IMU Data" },
    { VENDOR_OP_BATTERY_STATUS, handle_battery_message,     "Battery Status" },
    { VENDOR_OP_TEMPERATURE,    handle_temperature_message, "Temperature" },  // NEW!
};
```

#### Step 5: Update M5Stick Node

In your M5Stick code:

```cpp
// Send temperature data
temperature_data_t temp_data = {
    .timestamp_ms = (uint16_t)(esp_timer_get_time() / 1000),
    .temperature = (int16_t)(temperature_celsius * 100)  // °C → 0.01°C
};

mesh_model_send_vendor(0, VENDOR_OP_TEMPERATURE,
                      (uint8_t*)&temp_data, sizeof(temp_data), 0x0001);
```

Done! Temperature data will now flow to MQTT.

---

## Testing

### 1. Test WiFi Connection

Monitor logs during boot:
```
I (5234) MESH_BRIDGE: ✅ WiFi connected: 192.168.1.123
```

If fails:
- Check SSID and password
- Verify WiFi is 2.4 GHz (ESP32 doesn't support 5 GHz)

### 2. Test MQTT Connection

```
I (6123) MESH_BRIDGE: ✅ MQTT connected
I (6125) MESH_BRIDGE: 📡 Subscribed to: mesh/command/#
```

If fails:
- Test broker reachability: `ping broker.hivemq.com`
- Check firewall (port 1883)
- Verify broker URI format

### 3. Test Mesh Provisioning

Power on M5Stick, watch logs:
```
I (12345) MESH_BRIDGE: ✅ Node provisioned: UUID=dddd... Address=0x0005 Elements=1
```

### 4. Test IMU Data Flow

```
I (15000) MESH_BRIDGE: 📤 Published IMU from 0x0005: a=[0.1,-0.1,1.0]g
```

Use MQTT client to verify:
```bash
mosquitto_sub -h broker.hivemq.com -t "mesh/0x0005/imu" -v
```

### 5. Test Commands

Send command from MQTT:
```bash
mosquitto_pub -h broker.hivemq.com -t "mesh/command/0x0005/onoff" -m "ON"
```

Watch bridge logs:
```
I (20000) MESH_BRIDGE: 📥 MQTT message: mesh/command/0x0005/onoff = ON
I (20001) MESH_BRIDGE: 💡 Sending OnOff command to 0x0005: ON
```

M5Stick LED should turn on!

---

## Troubleshooting

### WiFi Won't Connect

**Check**:
```bash
# View full WiFi logs
idf.py monitor | grep WIFI
```

**Common issues**:
- Wrong SSID/password
- WiFi is 5 GHz (ESP32 only supports 2.4 GHz)
- Too far from router (weak signal)

**Solution**:
```c
// Check RSSI (signal strength)
int8_t rssi = wifi_mqtt_get_rssi();
ESP_LOGI(TAG, "WiFi RSSI: %d dBm", rssi);
// Good: > -50 dBm, Fair: -50 to -70 dBm, Poor: < -70 dBm
```

### MQTT Connection Fails

**Test broker manually**:
```bash
# Try publishing
mosquitto_pub -h broker.hivemq.com -t "test" -m "hello"

# Try subscribing
mosquitto_sub -h broker.hivemq.com -t "test"
```

**Check logs**:
```
E (6000) WIFI_MQTT: MQTT error
E (6001) WIFI_MQTT:   TCP error: 0x76
```

**Solutions**:
- Verify broker URI (must start with `mqtt://` or `mqtts://`)
- Check firewall allows port 1883
- Try public broker: `mqtt://test.mosquitto.org`

### No IMU Data Received

**Check M5Stick is provisioned**:
```
I (12345) MESH_BRIDGE: ✅ Node provisioned: Address=0x0005
```

**Check M5Stick is sending**:
- M5Stick logs should show: "Publishing IMU data"
- Check M5Stick UUID matches bridge filter (0xdddd)

**Check MQTT subscription**:
```bash
# Subscribe to ALL mesh topics
mosquitto_sub -h broker.hivemq.com -t "mesh/#" -v
```

### Commands Don't Work

**Check topic format**:
```
✅ CORRECT: mesh/command/0x0005/onoff
❌ WRONG:   mesh/command/5/onoff         (missing 0x prefix)
❌ WRONG:   mesh/cmd/0x0005/onoff        (wrong path)
```

**Check payload**:
```
✅ CORRECT: "ON" or "OFF" (or "1" or "0")
❌ WRONG:   "on" (lowercase not supported)
```

**Enable debug logs**:
```c
esp_log_level_set("MESH_BRIDGE", ESP_LOG_DEBUG);
```

---

## Examples

### Example 1: Home Assistant Integration

**configuration.yaml**:
```yaml
mqtt:
  sensor:
    - name: "M5Stick Accel X"
      state_topic: "mesh/0x0005/imu"
      value_template: "{{ value_json.accel.x }}"
      unit_of_measurement: "g"

    - name: "M5Stick Accel Y"
      state_topic: "mesh/0x0005/imu"
      value_template: "{{ value_json.accel.y }}"
      unit_of_measurement: "g"

  switch:
    - name: "M5Stick LED"
      command_topic: "mesh/command/0x0005/onoff"
      payload_on: "ON"
      payload_off: "OFF"
```

### Example 2: Node-RED Flow

```json
[
  {
    "id": "mqtt-in",
    "type": "mqtt in",
    "topic": "mesh/+/imu",
    "broker": "broker.hivemq.com"
  },
  {
    "id": "json-parse",
    "type": "json"
  },
  {
    "id": "debug",
    "type": "debug",
    "name": "IMU Data"
  }
]
```

### Example 3: Python Script

```python
import paho.mqtt.client as mqtt
import json

def on_message(client, userdata, msg):
    data = json.loads(msg.payload)
    print(f"Node {data['node']}: Accel={data['accel']}")

client = mqtt.Client()
client.on_message = on_message
client.connect("broker.hivemq.com", 1883)
client.subscribe("mesh/+/imu")
client.loop_forever()
```

### Example 4: InfluxDB Data Logger

```python
from influxdb_client import InfluxDBClient, Point
import paho.mqtt.client as mqtt
import json

influx = InfluxDBClient(url="http://localhost:8086", token="mytoken")
write_api = influx.write_api()

def on_message(client, userdata, msg):
    data = json.loads(msg.payload)

    point = Point("imu") \
        .tag("node", data['node']) \
        .field("accel_x", data['accel']['x']) \
        .field("accel_y", data['accel']['y']) \
        .field("accel_z", data['accel']['z'])

    write_api.write(bucket="sensors", record=point)

client = mqtt.Client()
client.on_message = on_message
client.connect("broker.hivemq.com", 1883)
client.subscribe("mesh/+/imu")
client.loop_forever()
```

---

## Performance

**Typical Metrics** (ESP32 @ 240 MHz):

- **WiFi Connection**: 2-5 seconds
- **MQTT Connection**: 0.5-1 second
- **Mesh Provisioning**: 5-10 seconds per node
- **Message Latency**:
  - Mesh → Bridge: ~50-100 ms
  - Bridge → MQTT: ~10-50 ms
  - **Total: ~100-150 ms**

**Throughput**:
- Single node at 10 Hz: ~80 bytes/s
- 10 nodes at 10 Hz: ~800 bytes/s
- **Max sustainable**: ~50 nodes at 10 Hz

---

## Security Considerations

### Current Implementation

- WiFi: WPA2 encryption
- BLE Mesh: AES-CCM encryption (NetKey + AppKey)
- MQTT: **No TLS** (uses plain `mqtt://`)

### Production Recommendations

1. **Use MQTT over TLS** (`mqtts://`):
   ```c
   .mqtt_broker_uri = "mqtts://broker.example.com:8883"
   ```

2. **Add client certificates** (for AWS IoT, Azure):
   ```c
   // In wifi_mqtt component, add:
   .cert_pem = "-----BEGIN CERTIFICATE-----\n..."
   .client_cert_pem = "..."
   .client_key_pem = "..."
   ```

3. **Use MQTT authentication**:
   ```c
   .mqtt_username = "device123"
   .mqtt_password = "secure_password"
   ```

4. **Firewall**:
   - Allow only necessary ports (1883 or 8883)
   - Restrict MQTT broker access

---

## Advanced Topics

### Multiple Message Types

You can send different sensor types from the same node:

```c
// In M5Stick node:
void sensor_task(void *param) {
    while(1) {
        // Send IMU data (10 Hz)
        publish_imu_data();
        vTaskDelay(pdMS_TO_TICKS(100));

        // Send temperature every 5 seconds
        static int count = 0;
        if (++count >= 50) {
            publish_temperature();
            count = 0;
        }
    }
}
```

### Custom MQTT Topics

Modify topic patterns in handlers:

```c
// Change from: mesh/0x0005/imu
// To: sensors/living_room/m5stick/imu
snprintf(topic, sizeof(topic), "sensors/%s/m5stick/imu", room_name);
```

### Binary vs JSON

**JSON** (current):
- ✅ Human-readable
- ✅ Easy to parse
- ❌ Larger size (~200 bytes)

**Binary** (alternative):
```c
wifi_mqtt_publish_binary("mesh/0x0005/imu_raw",
                         (uint8_t*)&imu_data,
                         sizeof(imu_data),
                         0);
```
- ✅ Smaller (8 bytes)
- ❌ Needs custom parser

---

## License

This bridge implementation is part of the ESP32_BLE_mesh project.

---

## Support

For issues or questions:
- Check logs with `idf.py monitor`
- Enable debug: `esp_log_level_set("*", ESP_LOG_DEBUG)`
- Consult ESP-IDF documentation
- Check BLE Mesh specification

---

## Changelog

**v1.0** - Initial release
- WiFi-MQTT component integration
- Generalized message routing
- IMU data support
- Bi-directional commands
