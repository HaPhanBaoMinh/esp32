idf_component_register(
    SRCS "ui_shell.cpp"
    INCLUDE_DIRS "include"
    REQUIRES board esp_lvgl_port lvgl
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
