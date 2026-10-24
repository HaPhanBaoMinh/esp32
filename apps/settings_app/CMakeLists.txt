idf_component_register(
    SRCS "settings_app.cpp"
    INCLUDE_DIRS "include"
    REQUIRES app_core board settings ui lvgl esp_timer log
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
