idf_component_register(
    SRCS "src/ble_mesh_node.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src"
    REQUIRES bt nvs_flash
)
