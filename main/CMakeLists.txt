# Main component for Splines module template

idf_component_register(
    SRCS
        "main.cpp"
        "lgfx_config.cpp"
    INCLUDE_DIRS
        "."
    REQUIRES
        driver
        esp_timer
        esp_psram
        esp_task_wdt
        LovyanGFX
        spi_flash
)
