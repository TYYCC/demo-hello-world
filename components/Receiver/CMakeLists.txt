set(RECEIVER_SRCS
    "Communication/src/cmd_terminal.c"
    "Communication/src/spi_slave_receiver.c"
    "Communication/src/settings_manager.c"
    "Communication/src/usb_interface.c"
    "Communication/src/usb_device_receiver.c"
    "Communication/src/usb_serial_bridge.cpp"

    "image/src/jpeg_stream_encoder.c"

    "other/src/task.c"
    "other/src/led_status_manager.c"
    "other/src/wifi_pairing_manager.c"

    "tcp_hb/src/tcp_client_hb.c"

    "tcp_server/src/tcp_server.c"
    "tcp_server/src/tcp_common_protocol.c"

    "tcp_telemetry/src/tcp_client_telemetry.c"
    "tcp_telemetry/src/pwm_controller.c"
)

idf_component_register(
    SRCS ${RECEIVER_SRCS}
    INCLUDE_DIRS "tcp_hb/inc" "tcp_telemetry/inc" "tcp_server/inc" "other/inc" "Communication/inc" "image/inc"
    REQUIRES log driver esp_new_jpeg nvs_flash spi_flash Peripherals arduino-esp32
)