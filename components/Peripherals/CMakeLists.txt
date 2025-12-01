set(PERIPHERALS_SRCS "")

if(EN_RECEIVER_MODE)
    set(PERIPHERALS_SRCS 
        "src/ws2812.c"
    )
    
else()
    set(PERIPHERALS_SRCS
        "src/st7789.c"
        "src/st7789_esp_lcd.c"
        "src/ws2812.c"
        "src/lsm6ds3.c"
        "src/bsp_i2c.c"
        "src/gt911.c"
        "src/joystick_adc.c"
        "src/key.c"
        "src/wifi_manager.c"
        "src/battery_monitor.c"
        "src/i2s_tdm.c"
    )
endif()
    idf_component_register(
    SRCS ${PERIPHERALS_SRCS}
    INCLUDE_DIRS "inc"
    REQUIRES lvgl log esp_wifi esp_event esp_netif nvs_flash driver esp_lcd Fusion
)
