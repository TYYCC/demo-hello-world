# Image Transfer Module CMakeLists.txt

idf_component_register(
    SRCS
        "src/image_transfer_app.c"
        "src/jpeg_decoder_service.c"
        "src/lz4_decoder_service.c"
        "src/lz4_image_decoder.c"
        "src/raw_data_service.c"
        "src/tcp_server_service.c"
        "src/ui_mapping_service.c"
        "src/wifi_image_transfer.c"
        "src/p2p_udp_image_transfer.c"
    
    INCLUDE_DIRS
        "inc"
    
    REQUIRES
        lwip
        esp_netif
        esp_wifi
        freertos
        driver
        esp_timer
        esp_jpeg
        lvgl
        lvgl_port
)

# Add LZ4 library include path
target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../../components/lz4-dev/lib")