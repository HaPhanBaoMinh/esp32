idf_component_register(
    SRCS
        "app_manager.cpp"
        "app_registry.cpp"
        "display_policy.cpp"
        "schedule.cpp"
        "settings_model.cpp"
    INCLUDE_DIRS "include"
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)

