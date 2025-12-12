set(ELRS_SRCS
    # Core/common
    "src/common.cpp"
    "src/dynpower.cpp"
    "src/rxtx_common.cpp"
    "src/rx_main.cpp"
    "lib/CRC/crc.cpp"
    "lib/CONFIG/config.cpp"
    "lib/CrsfProtocol/CRSFConnector.cpp"
    "lib/CrsfProtocol/CRSFEndpoint.cpp"
    "lib/CrsfProtocol/CRSFParser.cpp"
    "lib/CrsfProtocol/CRSFRouter.cpp"
    "lib/DEVICE/device.cpp"
    "lib/FEC/FEC.cpp"
    "lib/FEC/hamming.cpp"
    "lib/FHSS/FHSS.cpp"
    "lib/FHSS/random.cpp"
    "lib/HWTIMER/ESP32_hwTimer.cpp"
    "lib/LBT/LBT.cpp"
    "lib/logging/logging.cpp"
    "lib/OTA/OTA.cpp"
    "lib/POWERMGNT/POWERMGNT.cpp"
    "lib/RFAMP/RFAMP_hal.cpp"
    "lib/SPIEx/SPIEx.cpp"
    "lib/SX1280Driver/SX1280.cpp"
    "lib/SX1280Driver/SX1280_hal.cpp"
    "lib/TelemetryProtocol/dummy.c"
    "lib/elrs_eeprom/elrs_eeprom.cpp"

    # RX radio/CRSF plumbing
    "lib/rx-crsf/devRXLUA.cpp"
    "lib/rx-crsf/RXEndpoint.cpp"
    "lib/rx-crsf/RXOTAConnector.cpp"
    "lib/rx-crsf/RXParameters.cpp"
    "lib/OPTIONS/EspFlashStream.cpp"
    "lib/OPTIONS/hardware.cpp"
    "lib/OPTIONS/options.cpp"
    "lib/MSP/msp.cpp"
    "lib/StubbornReceiver/stubborn_receiver.cpp"
    "lib/StubbornSender/stubborn_sender.cpp"

    # Sensors / peripherals
    "lib/AnalogVbat/devAnalogVbat.cpp"
    "lib/BUTTON/devButton.cpp"
    "lib/Baro/baro_base.cpp"
    "lib/Baro/baro_bmp085.cpp"
    "lib/Baro/baro_bmp280.cpp"
    "lib/Baro/baro_spl06.cpp"
    "lib/Baro/devBaro.cpp"
    "lib/LED/devLED.cpp"
    "lib/LED/devRGB.cpp"
    "lib/LED/esp32rgb.cpp"
    "lib/ServoOutput/DShotRMT.cpp"
    "lib/ServoOutput/devServoOutput.cpp"
    "lib/SerialUpdate/devSerialUpdate.cpp"
    "lib/SerialUpdate/slip.cpp"
    "lib/SerialUpdate/stub_commands.cpp"
    "lib/SerialUpdate/stub_flasher.cpp"
    "lib/SerialUpdate/stub_write_flash.cpp"
    "lib/THERMAL/devThermal.cpp"
    "lib/THERMAL/lm75a.cpp"
    "lib/THERMAL/rpm.cpp"
    "lib/THERMAL/thermal.cpp"

    # VTX over SPI/MSP (RX build)
    "lib/MSPVTX/devMSPVTX.cpp"
    "lib/MSPVTX/freqTable.cpp"
    "lib/VTXSPI/devVTXSPI.cpp"

    # WiFi / radio glue
    "lib/WIFI/devWIFI.cpp"
    "lib/WIFI/lr1121.cpp"

    # RX serial protocol drivers
    "src/rx-serial/SerialAirPort.cpp"
    "src/rx-serial/SerialCRSF.cpp"
    "src/rx-serial/SerialDisplayport.cpp"
    "src/rx-serial/SerialGPS.cpp"
    "src/rx-serial/SerialHoTT_TLM.cpp"
    "src/rx-serial/SerialIO.cpp"
    "src/rx-serial/SerialMavlink.cpp"
    "src/rx-serial/SerialSBUS.cpp"
    "src/rx-serial/SerialSmartAudio.cpp"
    "src/rx-serial/SerialSUMD.cpp"
    "src/rx-serial/SerialTramp.cpp"
    "src/rx-serial/devSerialIO.cpp"
)
set(ELRS_INCS
    "lib/SX12xxDriverCommon"
    "lib/SX1280Driver"
    "lib/CRC"
    "lib/CRSF2MSP"
    "lib/FEC"
    "lib/OPTIONS"
    "lib/RFAMP"
    "lib/SPIEx"
    "lib/TelemetryProtocol"
    "lib/rx-crsf"
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
    "lib/VTXSPI"
    "lib/StubbornReceiver"
    "lib/HWTIMER"
    "lib/Backpack"
    "lib/logging"
    "lib/BUTTON"
    "lib/MAVLink"
    "lib/MSPVTX"
    "lib/ServoOutput"
    "lib/PFD"
    "include"
    "include/target"
    "src"
    "src/rx-serial"
    "lib/AnalogVbat"
    "lib/Baro"
    "lib/LED"
    "lib/SerialUpdate"
    "lib/THERMAL"
    "lib/PWM"
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
