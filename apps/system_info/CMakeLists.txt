idf_component_register(
    SRCS "system_info_app.cpp"
    INCLUDE_DIRS "include"
    REQUIRES app_core board network ui lvgl esp_system heap log
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)

