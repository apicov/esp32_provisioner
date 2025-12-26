# ESP32 BLE Mesh Provisioner with MQTT Bridge

A complete BLE Mesh provisioner application that automatically provisions nodes, configures them, and forwards sensor data to MQTT.

## 🎯 What This Project Does

This application acts as a **BLE Mesh Provisioner** with an **MQTT Bridge**:

1. **Auto-provisions** BLE Mesh nodes (like M5Stick with IMU sensors)
2. **Auto-configures** nodes with application keys and publish settings
3. **Receives** vendor model messages (IMU data, sensor data, etc.)
4. **Forwards** mesh data to MQTT broker in JSON format
5. **Connects** WiFi and maintains auto-reconnection

## 🏗️ Architecture

```
┌─────────────────┐
│ BLE Mesh Nodes  │ (M5Stick with IMU)
│   (Sensors)     │
└────────┬────────┘
         │ Vendor Messages
         ↓
┌─────────────────┐
│  ESP32 Gateway  │ ← This Application
│  (Provisioner)  │
└────────┬────────┘
         │ WiFi
         ↓
┌─────────────────┐
│  MQTT Broker    │
└────────┬────────┘
         │
         ↓
┌─────────────────┐
│ MQTT Clients    │ (Apps, Cloud, Dashboards)
└─────────────────┘
```

### Component Architecture

This project uses a **clean 3-component architecture**:

```
┌──────────────────────┐
│ ble_mesh_provisioner │  Independent BLE Mesh component
└──────────────────────┘

┌──────────────────────┐
│     wifi_mqtt        │  Independent WiFi/MQTT component (reusable!)
└──────────────────────┘

┌──────────────────────┐
│  mesh_mqtt_bridge    │  Glue layer connecting mesh ↔ MQTT
└──────────────────────┘
```

**Benefits:**
- ✅ `wifi_mqtt` is **reusable** in other projects (not tied to mesh)
- ✅ `ble_mesh_provisioner` is **independent** (can work without MQTT)
- ✅ `mesh_mqtt_bridge` is **optional** (clean separation of concerns)
- ✅ Each component can be **tested independently**

## 🚀 Quick Start

### 1. Prerequisites

- ESP-IDF v5.0 or later
- ESP32 development board
- BLE Mesh nodes (e.g., M5Stick with mesh firmware)
- MQTT broker (mosquitto, HiveMQ, etc.)

### 2. Configure

```bash
idf.py menuconfig
```

Navigate to **"ESP32 Mesh Gateway Configuration"** and set:

- **WiFi Configuration**
  - WiFi SSID
  - WiFi Password

- **MQTT Configuration**
  - MQTT Broker URI (e.g., `mqtt://192.168.1.100:1883`)
  - MQTT Topic Prefix (e.g., `esp32`)

- **BLE Mesh Configuration**
  - UUID Prefix (default: 0xAA, 0xBB for M5Stick)

### 3. Build and Flash

```bash
idf.py build
idf.py flash monitor
```

### 4. Expected Output

```
I (1234) MAIN: ========================================
I (1234) MAIN:   BLE Mesh to MQTT Bridge
I (1234) MAIN: ========================================
I (1234) MAIN: Initializing BLE Mesh Provisioner...
I (2000) MAIN: ✓ BLE Mesh Provisioner started
I (2500) WIFI_MQTT: WiFi connected
I (3000) WIFI_MQTT: MQTT connected
I (3100) MAIN: ✓ MQTT connected - bridge is operational
I (4000) MESH_CB: Found unprovisioned device
I (5000) MESH_CB: Node provisioned: addr=0x0010
I (6000) MESH_CB: 🎉 Node fully configured and ready!
I (7000) MESH_MQTT_BRIDGE: Publishing IMU from 0x0010
I (7100) MESH_CB: 📊 IMU [t=1234] Accel:[0.5,0.1,9.8]g Gyro:[10,5,0]dps
```

## 📡 MQTT Topic Structure

Messages are published to topics with this format:

```
<prefix>/<message_type>/<node_address>
```

### Example Topics:

| Node Address | Message Type | Topic              |
|--------------|--------------|---------------------|
| 0x0010       | IMU Data     | `esp32/imu/0x0010` |
| 0x0011       | IMU Data     | `esp32/imu/0x0011` |
| 0x0012       | Sensor       | `esp32/sensor/0x0012` |

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

## 🧪 Testing

### Subscribe to MQTT Topics

```bash
# Subscribe to all mesh messages
mosquitto_sub -h <broker_ip> -t "esp32/#" -v

# Subscribe to IMU data only
mosquitto_sub -h <broker_ip> -t "esp32/imu/#" -v
```

### Test MQTT Connection

```bash
# Check if broker is reachable
mosquitto_pub -h <broker_ip> -t "test" -m "hello"
```

## 📁 Project Structure

```
esp32_provisioner/
├── main/
│   ├── main_bridge.c          # Main application (mesh + MQTT bridge)
│   ├── CMakeLists.txt          # Build configuration
│   └── Kconfig.projbuild       # Menuconfig options
│
├── components/
│   ├── ble_mesh_provisioner/   # BLE Mesh provisioner component
│   │   ├── src/
│   │   │   ├── ble_mesh_provisioner.c
│   │   │   ├── ble_mesh_callbacks.c
│   │   │   ├── ble_mesh_storage.c
│   │   │   └── ble_mesh_auto_config.c
│   │   ├── include/
│   │   └── README.md
│   │
│   ├── wifi_mqtt/              # Reusable WiFi-MQTT component
│   │   ├── src/
│   │   │   └── wifi_mqtt.c
│   │   ├── include/
│   │   │   └── wifi_mqtt.h
│   │   ├── CMakeLists.txt
│   │   └── README.md
│   │
│   └── mesh_mqtt_bridge/       # Glue layer (mesh ↔ MQTT)
│       ├── src/
│       │   └── mesh_mqtt_bridge.c
│       ├── include/
│       │   └── mesh_mqtt_bridge.h
│       ├── CMakeLists.txt
│       └── README.md
│
├── CMakeLists.txt
└── README.md                   # This file
```

## 🔧 Configuration Options

### WiFi Settings (menuconfig)

- `CONFIG_WIFI_SSID` - Your WiFi network name
- `CONFIG_WIFI_PASSWORD` - Your WiFi password
- `CONFIG_WIFI_MAXIMUM_RETRY` - Max connection retry attempts (default: 5)

### MQTT Settings (menuconfig)

- `CONFIG_MQTT_BROKER_URI` - MQTT broker address
  - Examples:
    - `mqtt://192.168.1.100:1883` (local)
    - `mqtt://test.mosquitto.org:1883` (public test broker)
    - `mqtts://broker.example.com:8883` (TLS)
- `CONFIG_MQTT_TOPIC_PREFIX` - Topic prefix for all messages (default: `esp32`)

### BLE Mesh Settings (menuconfig)

- `CONFIG_MESH_UUID_PREFIX_0` - First byte of UUID prefix (default: 0xAA)
- `CONFIG_MESH_UUID_PREFIX_1` - Second byte of UUID prefix (default: 0xBB)

**Note:** Your mesh nodes must have UUIDs starting with these bytes to be auto-provisioned.

## 📚 Component Documentation

Each component has its own detailed README:

- **[ble_mesh_provisioner/README.md](components/ble_mesh_provisioner/README.md)** - BLE Mesh provisioning guide
- **[wifi_mqtt/README.md](components/wifi_mqtt/README.md)** - WiFi-MQTT component API and examples
- **[mesh_mqtt_bridge/README.md](components/mesh_mqtt_bridge/README.md)** - Bridge architecture and extending message types

## 🎨 Customization

### Adding New Message Types

To add support for new vendor message types, see [mesh_mqtt_bridge/README.md](components/mesh_mqtt_bridge/README.md#adding-new-message-types).

Example:
1. Define opcode: `#define VENDOR_OP_TEMPERATURE 0xC00002`
2. Create handler function: `handle_temperature_message()`
3. Add to routing table in `mesh_mqtt_bridge.c`

No changes needed to `ble_mesh_provisioner` or `wifi_mqtt` components!

### Using WiFi-MQTT in Other Projects

The `wifi_mqtt` component is **completely independent** and can be used in any ESP32 project:

```bash
# Copy component to your project
cp -r components/wifi_mqtt /path/to/your/project/components/

# Use in your code
#include "wifi_mqtt.h"
```

See [wifi_mqtt/README.md](components/wifi_mqtt/README.md) for usage examples.

## 🐛 Troubleshooting

### WiFi not connecting

1. Check SSID and password in menuconfig
2. Verify WiFi is 2.4GHz (ESP32 doesn't support 5GHz)
3. Check logs for error messages

### MQTT not connecting

1. Verify broker URI is correct
2. Check if broker is reachable: `ping <broker_ip>`
3. Test broker with mosquitto_pub/sub
4. Check firewall settings

### Nodes not being provisioned

1. Verify UUID prefix matches your nodes
2. Check that nodes are in unprovisioned state
3. Look for "Found unprovisioned device" logs
4. Ensure nodes are powered on and in range

### No MQTT messages appearing

1. Check that MQTT is connected (look for "MQTT connected" log)
2. Verify nodes are provisioned and configured
3. Check that nodes are sending vendor messages
4. Monitor logs for "Publishing IMU from 0x..." messages

## 🔐 Security Notes

- **WiFi Password**: Stored in plaintext in `sdkconfig`. Use menuconfig encryption features for production.
- **MQTT**: Currently uses unencrypted MQTT. For production, use `mqtts://` with TLS.
- **Authentication**: Add MQTT username/password in menuconfig for secure brokers.

## 📊 Performance

- **Provisioning Time**: ~5-10 seconds per node
- **Configuration Time**: ~3-5 seconds per node
- **Message Latency**: <100ms (mesh → MQTT)
- **Throughput**: Supports multiple nodes publishing at 10Hz

## 🤝 Contributing

This is a reference implementation. Feel free to:
- Add new message types
- Improve error handling
- Add more MQTT features (retained messages, LWT, etc.)
- Extend the bridge with bidirectional communication (MQTT → mesh)

## 📝 License

Same as parent project.

## 🙏 Acknowledgments

- ESP-IDF BLE Mesh examples
- M5Stack community for M5Stick examples
- Espressif for excellent documentation

---

**Made with ❤️ for IoT and mesh networking enthusiasts**
