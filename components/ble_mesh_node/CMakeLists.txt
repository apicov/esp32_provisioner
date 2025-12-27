# Only build if BLE Mesh Node role is enabled (dual-role provisioner+node)
if(CONFIG_BLE_MESH_NODE)
    idf_component_register(
        SRCS "src/ble_mesh_node.c"
        INCLUDE_DIRS "include"
        PRIV_INCLUDE_DIRS "src"
        REQUIRES bt nvs_flash
    )
else()
    # Provisioner-only mode: skip this component
    idf_component_register()
endif()
