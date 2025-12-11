#include "telemetry_endpoint.h"
#include "esp_log.h"
#include "telemetry_main.h"
#include <arpa/inet.h>
#include <cmath>

static const char* TAG = "TelemetryEndpoint";

TelemetryEndpoint::TelemetryEndpoint()
    : CRSFEndpoint(CRSF_ADDRESS_FLIGHT_CONTROLLER)  // 监听来自飞控的消息
{
}

void TelemetryEndpoint::handleMessage(const crsf_header_t *message)
{
    if (!message) return;

    // 获取扩展消息头
    const crsf_ext_header_t *extMessage = (const crsf_ext_header_t *)message;

    // 检查是否是发往我们的消息（广播或直接发给遥控器）
    if (extMessage->dest_addr != CRSF_ADDRESS_BROADCAST &&
        extMessage->dest_addr != CRSF_ADDRESS_RADIO_TRANSMITTER &&
        extMessage->dest_addr != CRSF_ADDRESS_CRSF_TRANSMITTER) {
        return;
    }

    switch (message->type) {
        case CRSF_FRAMETYPE_BATTERY_SENSOR: {
            const crsf_sensor_battery_t* battery = (const crsf_sensor_battery_t*)(message + 1);
            processBatterySensor(battery);
            break;
        }
        case CRSF_FRAMETYPE_ATTITUDE: {
            const crsf_sensor_attitude_t* attitude = (const crsf_sensor_attitude_t*)(message + 1);
            processAttitudeSensor(attitude);
            break;
        }
        case CRSF_FRAMETYPE_BARO_ALTITUDE: {
            const crsf_sensor_baro_vario_t* baro = (const crsf_sensor_baro_vario_t*)(message + 1);
            processBaroAltitude(baro);
            break;
        }
        case CRSF_FRAMETYPE_LINK_STATISTICS: {
            const crsfLinkStatistics_t* linkStats = (const crsfLinkStatistics_t*)(message + 1);
            processLinkStatistics(linkStats);
            break;
        }
        default:
            // 其他遥测类型可以在这里添加
            break;
    }
}

void TelemetryEndpoint::processBatterySensor(const crsf_sensor_battery_t* battery)
{
    if (!battery) return;

    // 获取当前遥测数据
    telemetry_data_t current_data;
    if (telemetry_service_get_data(&current_data) != 0) {
        return;
    }

    // 更新电池数据
    // CRSF电压单位: mV * 100, 所以需要转换
    current_data.voltage = (float)ntohs(battery->voltage) / 100.0f / 1000.0f;  // mV * 100 -> V
    current_data.current = (float)ntohs(battery->current) / 100.0f / 1000.0f;  // mA * 100 -> A

    ESP_LOGD(TAG, "Battery: %.2fV, %.2fA", current_data.voltage, current_data.current);

    // 更新遥测数据
    telemetry_service_update_data(&current_data);
}

void TelemetryEndpoint::processAttitudeSensor(const crsf_sensor_attitude_t* attitude)
{
    if (!attitude) return;

    // 获取当前遥测数据
    telemetry_data_t current_data;
    if (telemetry_service_get_data(&current_data) != 0) {
        return;
    }

    // 更新姿态数据
    // CRSF姿态单位: 弧度 * 10000, 转换为度
    current_data.roll = (float)ntohs(attitude->roll) / 10000.0f * 180.0f / M_PI;
    current_data.pitch = (float)ntohs(attitude->pitch) / 10000.0f * 180.0f / M_PI;
    current_data.yaw = (float)ntohs(attitude->yaw) / 10000.0f * 180.0f / M_PI;

    ESP_LOGD(TAG, "Attitude: R%.2f P%.2f Y%.2f", current_data.roll, current_data.pitch, current_data.yaw);

    // 更新遥测数据
    telemetry_service_update_data(&current_data);
}

void TelemetryEndpoint::processBaroAltitude(const crsf_sensor_baro_vario_t* baro)
{
    if (!baro) return;

    // 获取当前遥测数据
    telemetry_data_t current_data;
    if (telemetry_service_get_data(&current_data) != 0) {
        return;
    }

    // 更新高度数据
    uint16_t altitude_raw = ntohs(baro->altitude);
    if (altitude_raw & 0x8000) {
        // 高位设置，表示米为单位
        current_data.altitude = (float)(altitude_raw & 0x7FFF);
    } else {
        // 分米为单位 + 10000dm偏移
        current_data.altitude = (float)(altitude_raw - 10000) / 10.0f;
    }

    ESP_LOGD(TAG, "Altitude: %.1fm", current_data.altitude);

    // 更新遥测数据
    telemetry_service_update_data(&current_data);
}

void TelemetryEndpoint::processLinkStatistics(const crsfLinkStatistics_t* linkStats)
{
    if (!linkStats) return;

    // 获取当前遥测数据
    telemetry_data_t current_data;
    if (telemetry_service_get_data(&current_data) != 0) {
        return;
    }

    // 更新链路统计数据
    current_data.uplink_rssi_1 = linkStats->uplink_RSSI_1;
    current_data.uplink_rssi_2 = linkStats->uplink_RSSI_2;
    current_data.link_quality = linkStats->uplink_Link_quality;
    current_data.snr = linkStats->uplink_SNR;
    current_data.antenna_select = linkStats->active_antenna;

    ESP_LOGD(TAG, "LinkStats: RSSI1=%d, LQ=%d%%, SNR=%d",
             current_data.uplink_rssi_1, current_data.link_quality, current_data.snr);

    // 更新遥测数据
    telemetry_service_update_data(&current_data);
}