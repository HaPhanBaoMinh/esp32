idf_component_register(
    SRCS "board.cpp"
    INCLUDE_DIRS "include"
    REQUIRES
        esp_system
        spi_flash
        heap
        esp_driver_gpio
        esp_driver_i2c
        esp_driver_ledc
        esp_driver_spi
        esp_lcd
        esp_lcd_ili9341
        esp_lcd_touch_ft5x06
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)

