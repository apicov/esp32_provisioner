idf_component_register(SRCS "main_bridge.c"
                    INCLUDE_DIRS "."
                    REQUIRES ble_mesh_provisioner wifi_mqtt mesh_mqtt_bridge)
