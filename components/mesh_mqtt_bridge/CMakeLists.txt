idf_component_register(
    SRCS "src/mesh_mqtt_bridge.c"
    INCLUDE_DIRS "include"
    REQUIRES wifi_mqtt ble_mesh_provisioner
)
