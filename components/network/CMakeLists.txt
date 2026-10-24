idf_component_register(
    SRCS "network_service.cpp"
    INCLUDE_DIRS "include"
    REQUIRES esp_wifi esp_netif esp_event esp_timer nvs_flash
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
