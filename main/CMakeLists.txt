set(MAIN_SRCS
    "main.cpp"
)

idf_component_register(
    SRCS ${MAIN_SRCS}
    INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/components/Receiver/Communication/inc" "${CMAKE_SOURCE_DIR}/components/Receiver/other/inc" "${CMAKE_SOURCE_DIR}/components/Receiver/tcp_server/inc"
    REQUIRES esp_timer Receiver driver spi_flash esp_common arduino-esp32 elrs
)

add_library(MY_DEFS INTERFACE)

target_compile_definitions(MY_DEFS INTERFACE
    PLATFORM_ESP32
    PLATFORM_ESP32_S3
    RADIO_SX128X
    TARGET_RX
)