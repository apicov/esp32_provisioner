# Provisioner Main Files

## Active Main File

**`main_bridge.c`** - This is the ONLY file compiled by CMakeLists.txt

### Purpose
BLE Mesh Provisioner with MQTT Bridge - provisions mesh nodes and bridges mesh data to MQTT.

### Build Configuration
See `CMakeLists.txt`:
```cmake
idf_component_register(SRCS "main_bridge.c" ...)
```

### Components Used
- `ble_mesh_provisioner` - Provisioning logic
- `wifi_mqtt` - WiFi and MQTT client
- `mesh_mqtt_bridge` - Mesh ↔ MQTT translation

## Removed Files

The following files were removed as dead code:
- `esp32_provisioner.c` - Old ESP-IDF example code, never compiled
