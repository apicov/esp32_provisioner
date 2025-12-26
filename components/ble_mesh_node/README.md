# BLE Mesh Node Component

A reusable ESP-IDF component that implements a BLE Mesh node with Generic OnOff Server model.

## Features

- **Simple API** - Just 4 functions to get started
- **Generic OnOff Server** - Control LEDs, relays, or any on/off device via mesh
- **Automatic Provisioning** - Handles provisioning protocol automatically
- **Persistent Storage** - Remembers provisioning state across reboots (NVS)
- **Application Callbacks** - Integrate with your hardware (LEDs, display, etc.)
- **Educational Comments** - Comprehensive inline documentation

## Quick Start

### 1. Add Component to Your Project

```bash
# Create components directory if it doesn't exist
mkdir -p components

# Symlink or copy this component
ln -s /path/to/ble_mesh_node components/ble_mesh_node
```

### 2. Configure sdkconfig

Add these to your `sdkconfig.defaults`:

```
CONFIG_BT_ENABLED=y
CONFIG_BLE_MESH=y
CONFIG_BLE_MESH_NODE=y
CONFIG_BLE_MESH_CFG_SRV=y
CONFIG_BLE_MESH_GENERIC_ONOFF_SRV=y
CONFIG_BLE_MESH_PB_ADV=y
CONFIG_BLE_MESH_PB_GATT=y
```

### 3. Write Your Application

```c
#include "ble_mesh_node.h"

// Callback when LED state changes
void onoff_changed(uint8_t onoff) {
    gpio_set_level(LED_PIN, onoff);  // Control your LED
}

// Callback when provisioned
void provisioned(uint16_t addr) {
    printf("Provisioned with address: 0x%04x\n", addr);
}

void app_main(void) {
    // Configure the node
    node_config_t config = {
        .device_uuid_prefix = {0xdd, 0xdd},  // Match provisioner's filter
        .callbacks = {
            .provisioned = provisioned,
            .onoff_changed = onoff_changed,
        }
    };

    // Initialize and start
    node_init(&config);
    node_start();

    // Your main loop
    while(1) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
```

### 4. Update CMakeLists.txt

```cmake
idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES ble_mesh_node
)
```

## API Reference

### `node_init(config)`

Initializes the BLE Mesh node.

**Parameters:**
- `config` - Node configuration (UUID prefix, callbacks)

**Returns:** `ESP_OK` on success

### `node_start()`

Starts the node (begins broadcasting beacons if unprovisioned).

**Returns:** `ESP_OK` on success

### `node_get_onoff_state()`

Returns current OnOff state (0 = OFF, 1 = ON).

### `node_set_onoff_state(onoff)`

Sets OnOff state locally and publishes to network.

**Parameters:**
- `onoff` - New state (0 = OFF, 1 = ON)

**Returns:** `ESP_OK` on success

## Provisioning Process

1. **Unprovisioned State**
   - Node broadcasts "Unprovisioned Device Beacons"
   - UUID: `[prefix][MAC][padding]`
   - Provisioner discovers node via UUID filter

2. **Provisioning**
   - Secure provisioning protocol executed
   - Node receives NetKey and unicast address
   - Node joins mesh network

3. **Configuration**
   - Provisioner sends AppKey
   - Provisioner binds AppKey to OnOff Server model
   - Node ready to receive commands

4. **Operational**
   - Responds to OnOff Get/Set commands
   - Controls LED/device via callback
   - State persisted in NVS

## Hardware Examples

### ESP32 DevKit with LED

```c
#define LED_PIN GPIO_NUM_2

void onoff_changed(uint8_t onoff) {
    gpio_set_level(LED_PIN, onoff);
}

void app_main(void) {
    gpio_reset_pin(LED_PIN);
    gpio_set_direction(LED_PIN, GPIO_MODE_OUTPUT);

    node_config_t config = {
        .device_uuid_prefix = {0xdd, 0xdd},
        .callbacks = { .onoff_changed = onoff_changed }
    };

    node_init(&config);
    node_start();
}
```

### M5Stack with Display

See [m5stick/main/m5stick_mesh_node.cpp](../../m5stick/main/m5stick_mesh_node.cpp) for complete example with:
- LCD display showing status
- Button to toggle LED manually
- Visual feedback for provisioning

## Troubleshooting

### Node not discovered by provisioner

- Check UUID prefix matches provisioner's filter
- Verify `CONFIG_BLE_MESH_PB_ADV=y` is set
- Ensure Bluetooth is initialized correctly
- Check logs for errors

### Node loses provisioning after reboot

- Verify NVS is initialized (`nvs_flash_init()`)
- Check NVS partition exists in partition table
- Ensure sufficient NVS space

### OnOff commands not working

- Verify provisioning completed successfully
- Check AppKey was added to node
- Confirm model binding succeeded
- Check callback is registered and implemented

## Architecture

```
┌─────────────────────────────────┐
│   Your Application              │
│   - LED control                 │
│   - Display updates             │
│   - Button handling             │
└────────────┬────────────────────┘
             │ Callbacks
┌────────────▼────────────────────┐
│   ble_mesh_node Component       │
│   - Generic OnOff Server        │
│   - Configuration Server        │
│   - Provisioning handler        │
└────────────┬────────────────────┘
             │
┌────────────▼────────────────────┐
│   ESP-IDF BLE Mesh Stack        │
└────────────┬────────────────────┘
             │
┌────────────▼────────────────────┐
│   Bluetooth Controller          │
└─────────────────────────────────┘
```

## Related Components

- **ble_mesh_provisioner** - Companion provisioner component
- See [LEARNING.md](../../LEARNING.md) for BLE Mesh concepts

## License

Same as ESP-IDF (Apache 2.0)
