set(PERIPHERALS_SRCS 
    "src/ws2812.c"
)
    
idf_component_register(
    SRCS ${PERIPHERALS_SRCS}
    INCLUDE_DIRS "inc"
    REQUIRES log esp_wifi esp_event esp_netif nvs_flash driver
)
