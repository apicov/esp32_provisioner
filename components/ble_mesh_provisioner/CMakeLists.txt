idf_component_register(
    SRCS "src/ble_mesh_provisioner.c"
         "src/ble_mesh_callbacks.c"
         "src/ble_mesh_storage.c"
         "src/ble_mesh_auto_config.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src"
    REQUIRES bt nvs_flash
)
