idf_component_register(
    SRCS "nvs_settings_store.cpp"
    INCLUDE_DIRS "include"
    REQUIRES app_core nvs_flash
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)

