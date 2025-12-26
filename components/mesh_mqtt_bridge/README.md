# Mesh-MQTT Bridge Component

## Overview

The **mesh_mqtt_bridge** is a **glue layer** that connects two independent components:
- `ble_mesh_provisioner` - BLE Mesh networking
- `wifi_mqtt` - WiFi and MQTT connectivity

This design keeps both components agnostic to each other, maintaining clean separation of concerns.

## Architecture

```
┌──────────────────┐
│ BLE Mesh Network │
│   (IMU nodes)    │
└────────┬─────────┘
         │ vendor messages
         ↓
┌──────────────────┐
│ Mesh-MQTT Bridge │ ← This component
│  (glue layer)    │
└────────┬─────────┘
         │ MQTT pub/sub
         ↓
┌──────────────────┐
│   MQTT Broker    │
│   (mosquitto)    │
└──────────────────┘
```

## Why This Design?

### ✅ Benefits:
- **Independent Components**: `wifi_mqtt` doesn't know about BLE Mesh
- **Independent Components**: `ble_mesh_provisioner` doesn't know about MQTT
- **Optional Bridge**: Can run mesh without MQTT, or MQTT without mesh
- **Easy Testing**: Test each component independently
- **Extensible**: Add new message types without modifying core components

### ❌ Alternative (Wrong) Approach:
- Modifying `ble_mesh_provisioner` to add MQTT callbacks → couples components
- Modifying `wifi_mqtt` to understand mesh opcodes → couples components

## How It Works

### 1. Weak Function Override Pattern

The `ble_mesh_provisioner` component defines a **weak function**:

```c
// In ble_mesh_callbacks.c (provisioner component)
__attribute__((weak)) void provisioner_vendor_msg_handler(uint16_t src_addr,
                                                           uint32_t opcode,
                                                           uint8_t *data,
                                                           uint16_t length)
{
    // Default: do nothing
}
```

The `mesh_mqtt_bridge` component **overrides** this function:

```c
// In mesh_mqtt_bridge.c (this component)
void provisioner_vendor_msg_handler(uint16_t src_addr, uint32_t opcode,
                                    uint8_t *data, uint16_t length)
{
    // Route message to MQTT!
}
```

**Result**: When mesh_mqtt_bridge is linked, vendor messages are automatically forwarded to MQTT. When it's not linked, the default (empty) implementation is used.

### 2. Message Routing Table

The bridge uses a routing table to map opcodes to handlers:

```c
static const message_route_t message_router[] = {
    { VENDOR_OP_IMU_DATA, handle_imu_message, "IMU Data" },
    // Add more message types here
};
```

Each handler:
1. Unpacks the vendor message data
2. Converts to JSON (or other format)
3. Publishes to appropriate MQTT topic

## Basic Usage

```c
#include "ble_mesh_provisioner.h"
#include "wifi_mqtt.h"
#include "mesh_mqtt_bridge.h"

void app_main(void)
{
    // 1. Initialize mesh (independent)
    provisioner_config_t prov_config = { /* ... */ };
    provisioner_init(&prov_config, NULL);
    provisioner_start();

    // 2. Initialize WiFi-MQTT (independent)
    wifi_mqtt_config_t mqtt_config = {
        .wifi_ssid = "MyWiFi",
        .wifi_password = "password",
        .mqtt_broker_uri = "mqtt://192.168.1.100:1883",
    };
    wifi_mqtt_init(&mqtt_config);
    wifi_mqtt_start();

    // 3. Initialize bridge (glue layer)
    bridge_config_t bridge_config = {
        .mqtt_topic_prefix = "mesh",
        .mesh_net_idx = 0,
        .mesh_app_idx = 0,
    };
    mesh_mqtt_bridge_init(&bridge_config);

    // Done! Vendor messages now flow to MQTT automatically
}
```

## MQTT Topic Structure

The bridge publishes to topics with this structure:

```
<prefix>/<message_type>/<node_address>
```

### Examples:

| Node Address | Message Type | MQTT Topic          |
|--------------|--------------|---------------------|
| 0x0010       | IMU Data     | `mesh/imu/0x0010`   |
| 0x0011       | IMU Data     | `mesh/imu/0x0011`   |
| 0x0012       | Sensor       | `mesh/sensor/0x0012`|

### Payload Format (JSON):

```json
{
  "node": "0x0010",
  "time": 12345,
  "accel": {
    "x": 0.5,
    "y": -0.2,
    "z": 9.8
  },
  "gyro": {
    "x": 10,
    "y": -5,
    "z": 0
  }
}
```

## Adding New Message Types

To add a new vendor message type:

### Step 1: Define the opcode

```c
// In mesh_mqtt_bridge.c
#define VENDOR_OP_TEMPERATURE  0xC00002
```

### Step 2: Write a handler function

```c
static void handle_temperature_message(uint16_t src_addr, uint8_t *data, uint16_t length)
{
    if (length != 4) return;

    float temp = *(float*)data;

    char payload[128];
    snprintf(payload, sizeof(payload),
             "{\"node\":\"0x%04x\",\"temp\":%.1f}",
             src_addr, temp);

    char topic[64];
    snprintf(topic, sizeof(topic), "%s/temp/0x%04x",
             g_bridge_config.mqtt_topic_prefix, src_addr);

    wifi_mqtt_publish(topic, payload, 0);
}
```

### Step 3: Add to routing table

```c
static const message_route_t message_router[] = {
    { VENDOR_OP_IMU_DATA, handle_imu_message, "IMU Data" },
    { VENDOR_OP_TEMPERATURE, handle_temperature_message, "Temperature" },  // New!
};
```

**That's it!** No need to modify `ble_mesh_provisioner` or `wifi_mqtt`.

## Testing the Bridge

### 1. Subscribe to MQTT topics

```bash
mosquitto_sub -h localhost -t "mesh/#" -v
```

### 2. Provision a node

The provisioner will automatically provision nodes with UUID prefix `0xDDDD`.

### 3. Watch MQTT messages

You should see:

```
mesh/imu/0x0010 {"node":"0x0010","time":1234,"accel":{"x":0.5,"y":-0.2,"z":9.8},"gyro":{"x":10,"y":-5,"z":0}}
mesh/imu/0x0010 {"node":"0x0010","time":1235,"accel":{"x":0.6,"y":-0.1,"z":9.7},"gyro":{"x":12,"y":-3,"z":1}}
```

## Configuration Options

### Bridge Configuration

```c
typedef struct {
    const char *mqtt_topic_prefix;  // MQTT topic prefix (e.g., "mesh")
    uint16_t mesh_net_idx;          // BLE Mesh network index (usually 0)
    uint16_t mesh_app_idx;          // BLE Mesh app index (usually 0)
} bridge_config_t;
```

## Component Dependencies

This component requires:
- `wifi_mqtt` - For MQTT publishing
- `ble_mesh_provisioner` - For vendor message callbacks (weak function)

Add to your main `CMakeLists.txt`:

```cmake
idf_component_register(
    SRCS "main_bridge.c"
    INCLUDE_DIRS "."
    REQUIRES ble_mesh_provisioner wifi_mqtt mesh_mqtt_bridge
)
```

## Troubleshooting

### Bridge not forwarding messages

**Check 1**: Is the bridge initialized?
```
I (12345) MESH_MQTT_BRIDGE: ✓ Mesh-MQTT Bridge initialized successfully
```

**Check 2**: Is MQTT connected?
```
I (12346) WIFI_MQTT: MQTT connected
```

**Check 3**: Are vendor messages being received?
```
I (12347) MESH_MQTT_BRIDGE: Routing IMU Data message from 0x0010
```

### No MQTT messages appearing

**Check**: MQTT broker address in `main_bridge.c`:
```c
#define MQTT_BROKER_URI "mqtt://192.168.1.100:1883"
```

**Check**: WiFi credentials in `main_bridge.c`:
```c
#define WIFI_SSID     "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
```

### Nodes not being provisioned

This is a provisioner issue, not a bridge issue. The bridge only forwards messages from already-provisioned nodes.

Check provisioner logs:
```
I (12340) PROVISIONER: Scanning for unprovisioned devices...
I (12350) PROVISIONER: Found device with UUID: dd:dd:...
```

## Example Applications

### 1. Basic Bridge (`main_bridge.c`)
- Provisions nodes
- Forwards IMU data to MQTT
- Minimal configuration

### 2. Data Logger
- Subscribe to all mesh topics
- Store data in database
- Use the same bridge component!

### 3. Real-time Dashboard
- Subscribe to `mesh/imu/#`
- Visualize IMU data
- No code changes needed!

## Component File Structure

```
mesh_mqtt_bridge/
├── CMakeLists.txt
├── README.md (this file)
├── include/
│   └── mesh_mqtt_bridge.h    # Public API
└── src/
    └── mesh_mqtt_bridge.c    # Implementation
```

## License

Same as parent project.
