set(RECEIVER_SRCS
    "src/tcp_common_protocol.c"
    "src/spi_slave_receiver.c"
    "src/usb_device_receiver.c"
    "src/jpeg_stream_encoder.c"
    "src/cmd_terminal.c"
    "src/usb_interface.c"
    "src/settings_manager.c"
    "src/led_status_manager.c"
    "src/wifi_pairing_manager.c"
    "src/task.c"
    "tcp_hb/src/tcp_client_hb.c"
    "tcp_telemetry/src/tcp_client_telemetry.c"
    "tcp_telemetry/src/pwm_controller.c"
)

idf_component_register(
    SRCS ${RECEIVER_SRCS}
    INCLUDE_DIRS "inc" "tcp_hb/inc" "tcp_telemetry/inc"
    REQUIRES log driver esp_tinyusb esp_new_jpeg nvs_flash spi_flash Peripherals
)