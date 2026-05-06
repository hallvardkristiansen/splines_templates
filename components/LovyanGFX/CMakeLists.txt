# LovyanGFX ESP-IDF Component for ESP32-S3
# Compiles necessary source files for ESP32-S3 platform

idf_component_register(
    SRCS
        # Core LovyanGFX sources
        "lgfx/v1/LGFXBase.cpp"
        "lgfx/v1/LGFX_Sprite.cpp"
        "lgfx/v1/lgfx_fonts.cpp"

        # Panel base classes
        "lgfx/v1/panel/Panel_Device.cpp"
        "lgfx/v1/panel/Panel_LCD.cpp"

        # Utility sources
        "lgfx/v1/misc/pixelcopy.cpp"
        "lgfx/v1/misc/common_function.cpp"
        "lgfx/v1/misc/SpriteBuffer.cpp"

        # ESP32 platform sources (common to all ESP32 variants)
        "lgfx/v1/platforms/esp32/common.cpp"
        "lgfx/v1/platforms/esp32/Bus_SPI.cpp"
        "lgfx/v1/platforms/esp32/Light_PWM.cpp"

    INCLUDE_DIRS
        "."
        "lgfx"

    REQUIRES
        driver
        spi_flash
        esp_timer
        efuse
)
