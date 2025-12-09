message(STATUS "EN_RECEIVER_MODE in main component: ${EN_RECEIVER_MODE}")

if(EN_RECEIVER_MODE)
    set(MAIN_SRCS
        "main.c"
    )

    idf_component_register(
        SRCS ${MAIN_SRCS}
        INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/components/Receiver/Communication/inc" "${CMAKE_SOURCE_DIR}/components/Receiver/other/inc" "${CMAKE_SOURCE_DIR}/components/Receiver/tcp_server/inc"
        REQUIRES esp_timer Receiver driver spi_flash esp_common
    )
    
    target_compile_definitions(${COMPONENT_LIB} PRIVATE EN_RECEIVER_MODE=1)

else()
    set(MAIN_SRCS
        #主要启动文件 
        "main.cpp"
        "task_init.c"
        "components_init.c"

        # UI相关的文件
        "UI/ui_telemetry.c"
        "UI/ui_start_animation.c"
    "UI/ui_sync.c"
        "UI/ui_main.c"
        "UI/ui_wifi_settings.c"
        "UI/ui_settings.c"
        "UI/ui_binding.c"
        "UI/ui_image_transfer.c"
        "UI/ui_serial_display.c"
        "UI/ui_calibration.c"
        "UI/ui_test.c"
        "UI/ui_numeric_keypad.c"

        # UI公共组件
        "UI/theme_manager.c"
        "UI/ui_common.c"
        "UI/ui_state_manager.c"

        # app中game的相关文件
        "app/game/game_main.c"
        "app/game/game_tetris.c"
        "app/game/game_snake.c"
        "app/game/game_brickbreaker.c"
        "app/game/game_ball_img.c"

        # app文件
        "app/serial_display.c"
        "app/calibration_manager.c"
        "app/lvgl_main.c"
        "app/power_management.c"
        "app/background_manager.c"
        "app/settings_manager.c"
        "app/status_bar_manager.c"
        "app/lsm6ds_control.c"
        "app/audio_receiver.c"
        "app/auto_pairing.c"
        "app/src/ui_event_queue.c"
        
        # 图像传输模块
        "app/image_transfer/src/display_queue.c"
        "app/image_transfer/src/image_transfer_app.c"
        "app/image_transfer/src/jpeg_decoder_service.c"
        "app/image_transfer/src/lz4_decoder_service.c"
        "app/image_transfer/src/lz4_image_decoder.c"
        "app/image_transfer/src/tcp_server_service.c"
        "app/image_transfer/src/ui_mapping_service.c"

        # app中遥测相关的文件
        "app/Telemetry/src/telemetry_protocol.c"
        "app/Telemetry/src/telemetry_tcp.c"
        "app/Telemetry/src/telemetry_data_converter.c"
        "app/Telemetry/src/telemetry_main.c"
        "app/Telemetry/src/telemetry_receiver.c"
        "app/Telemetry/src/telemetry_sender.c"
        "app/Telemetry/src/tcp_server_hb.c"
        "app/Telemetry/src/tcp_command_server.c"
        
        # 字体相关文件
        "fonts/font_init.c"
        "fonts/symbol.c"
    )

    idf_component_register(
        SRCS ${MAIN_SRCS}
        INCLUDE_DIRS "inc" "UI/inc" "app/inc" "app/game" "fonts" "app/Telemetry/inc" "app/image_transfer/inc"
        REQUIRES lvgl esp_timer lvgl_port Peripherals lz4-dev driver spiffs spi_flash esp_common arduino-esp32 esp_wifi esp_system esp_event esp_netif nvs_flash ArduinoJson app_update ESPAsyncWebServer mavlink elrs
    )
    
    target_link_options(${COMPONENT_LIB} PRIVATE -Wl,--wrap=esp_log_write,--wrap=esp_log_writev)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE EN_RECEIVER_MODE=0)
endif()

add_library(MY_DEFS INTERFACE)

target_compile_definitions(MY_DEFS INTERFACE
    PLATFORM_ESP32
    PLATFORM_ESP32_S3
    RADIO_SX128X
    TARGET_TX
)