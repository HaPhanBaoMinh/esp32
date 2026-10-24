idf_component_register(
    SRCS "ota_service.cpp"
    INCLUDE_DIRS "include"
    REQUIRES app_update esp_https_ota esp_http_client mbedtls
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)

