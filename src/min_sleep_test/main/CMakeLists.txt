idf_component_register(
    SRCS "min_sleep_main.c"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_spi esp_driver_i2c esp_driver_gpio driver log freertos
)
