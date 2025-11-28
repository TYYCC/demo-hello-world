set(ELRS_SRCS
    "lib/Backpack/devBackpack.cpp"
    "lib/BUTTON/devButton.cpp"
    "lib/CONFIG/config.cpp"
    "lib/CRC/crc.cpp"
    "lib/CrsfProtocol/CRSFConnector.cpp"
    "lib/CrsfProtocol/CRSFEndpoint.cpp"
    "lib/CrsfProtocol/CRSFParser.cpp"
    "lib/CrsfProtocol/CRSFRouter.cpp"
    "lib/DEVICE/device.cpp"
    "lib/elrs_eeprom/elrs_eeprom.cpp"
    "lib/FEC/FEC.cpp"
    "lib/FEC/hamming.cpp"
    "lib/FHSS/FHSS.cpp"
    "lib/FHSS/random.cpp"
    "lib/Handset/AutoDetect.cpp"
    "lib/Handset/CRSFHandset.cpp"
    "lib/Handset/devHandset.cpp"
    "lib/Handset/PPMHandset.cpp"
    "lib/HWTIMER/ESP32_hwTimer.cpp"
    "lib/LBT/LBT.cpp"
    "lib/LED/devRGB.cpp"
    "lib/logging/logging.cpp"
    "lib/MAVLink/ardupilot_custom_telemetry.cpp"
    "lib/MAVLink/MAVLink.cpp"
    "lib/MSP/msp.cpp"
    "lib/OPTIONS/EspFlashStream.cpp"
    "lib/OPTIONS/hardware.cpp"
    "lib/OPTIONS/options.cpp"
    "lib/OTA/OTA.cpp"
    "lib/POWERMGNT/POWERMGNT.cpp"
    "lib/RFAMP/RFAMP_hal.cpp"
    "lib/SPIEx/SPIEx.cpp"
    "lib/StubbornReceiver/stubborn_receiver.cpp"
    "lib/StubbornSender/stubborn_sender.cpp"
    "lib/SX1280Driver/SX1280_hal.cpp"
    "lib/SX1280Driver/SX1280.cpp"
    "lib/TelemetryProtocol/dummy.c"
    "lib/tx-crsf/devTXLUA.cpp"
    "lib/tx-crsf/TXModuleEndpoint.cpp"
    "lib/tx-crsf/TXOTAConnector.cpp"
    "lib/tx-crsf/TXModuleParameters.cpp"
    "lib/tx-crsf/TXUSBConnector.cpp"
    "lib/VTX/devVTX.cpp"
    "lib/WIFI/devWIFI.cpp"
    "lib/WIFI/lr1121.cpp"
    "lib/WIFI/wifiJoystick.cpp"
    "src/common.cpp"
    "src/rxtx_common.cpp"
    "src/dynpower.cpp"
    "src/tx_main.cpp"
)
set(ELRS_INCS
    "lib/SX12xxDriverCommon"
    "lib/SX1280Driver"
    "lib/CRC"
    "lib/FEC"
    "lib/OPTIONS"
    "lib/RFAMP"
    "lib/SPIEx"
    "lib/TelemetryProtocol"
    "lib/tx-crsf"
    "lib/CrsfProtocol"
    "lib/DEVICE"
    "lib/Handset"
    "lib/CONFIG"
    "lib/FHSS"
    "lib/elrs_eeprom"
    "lib/MSP"
    "lib/OTA"
    "lib/POWERMGNT"
    "lib/FIFO"
    "lib/StubbornSender"
    "lib/LQCALC"
    "lib/LBT"
    "lib/MeanAccumulator"
    "lib/WIFI"
    "lib/VTX"
    "lib/StubbornReceiver"
    "lib/HWTIMER"
    "lib/Backpack"
    "lib/logging"
    "lib/BUTTON"
    "lib/MAVLink"
    "include"
    "include/target"
    "src"
    "src/rx-serial"
)
    idf_component_register(
    SRCS ${ELRS_SRCS}
    INCLUDE_DIRS ${ELRS_INCS}
    REQUIRES arduino-esp32 esp_wifi esp_system esp_event esp_netif nvs_flash ArduinoJson app_update ESPAsyncWebServer asynctcp esp_timer mavlink
)

target_link_libraries(${COMPONENT_LIB} PUBLIC MY_DEFS)

# 获取 Git 最新提交哈希
execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_COMMIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

set(FIRMWARE_VERSION "1.2.3")
