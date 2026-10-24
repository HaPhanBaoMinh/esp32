idf_component_register(
    SRCS "clock_app.cpp"
    INCLUDE_DIRS "include"
    REQUIRES app_core ui lvgl log
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)

