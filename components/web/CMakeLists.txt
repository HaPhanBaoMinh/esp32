idf_component_register(
    SRCS "web_server.cpp"
    INCLUDE_DIRS "include"
    EMBED_TXTFILES "www/index.html"
    REQUIRES app_core network esp_http_server app_update esp_timer esp_system heap
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
