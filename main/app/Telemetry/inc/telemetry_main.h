#ifndef TELEMETRY_MAIN_H
#define TELEMETRY_MAIN_H

#include "telemetry_protocol.h" // 引入协议头文件
#include <stdbool.h>
#include <stdint.h>


/**
 * @brief 遥测服务状态枚举
 */
typedef enum {
    TELEMETRY_STATUS_STOPPED = 0,
    TELEMETRY_STATUS_STARTING,
    TELEMETRY_STATUS_RUNNING,
    TELEMETRY_STATUS_STOPPING,
    TELEMETRY_STATUS_ERROR
} telemetry_status_t;

/**
 * @brief 遥测数据结构 - 基于ELRS OTA_LinkStats_s
 * 包含链路统计和遥控通道数据
 */
typedef struct {
    // ============ 链路统计数据 (来自ELRS OTA_LinkStats_s) ============
    uint8_t uplink_rssi_1;        // 天线1 RSSI (dBm + 120偏移, 范围:0-255)
    uint8_t uplink_rssi_2;        // 天线2 RSSI (dBm + 120偏移, 范围:0-255)
    uint8_t antenna_select;       // 当前天线选择 (0或1)
    uint8_t link_quality;         // 链路质量 (0-100%)
    int8_t snr;                   // 信噪比 (dB, 范围:-32到31)
    bool model_match;             // 模型ID匹配标志
    bool diversity_available;     // 双天线可用标志
    uint32_t uptime_ms;           // 链路连接时长 (毫秒)
    
    // ============ 遥控通道数据 (CRSF格式) ============
    uint16_t channels[16];        // 16个通道数据 (0-2047, 992为中位)
    bool channels_valid;          // 通道数据有效性
    
    // ============ 解锁状态 ============
    bool is_armed;                // 解锁状态
    
    // ============ 扩展遥测数据 (可选，用于显示) ============
    float voltage;                // 电压 (V) - 用于显示
    float current;                // 电流 (A) - 用于显示
    float roll;                   // 横滚角 (度)
    float pitch;                  // 俯仰角 (度)
    float yaw;                    // 偏航角 (度)
    float altitude;               // 高度 (米)
} telemetry_data_t;

/**
 * @brief 遥测服务回调函数类型
 */
typedef void (*telemetry_data_callback_t)(const telemetry_data_t* data);

/**
 * @brief 初始化遥测服务
 *
 * @return 成功返回0, 失败返回-1
 */
int telemetry_service_init(void);

/**
 * @brief 启动遥测服务
 *
 * @param callback 数据回调函数（可选，用于UI更新）
 * @return 成功返回0, 失败返回-1
 */
int telemetry_service_start(telemetry_data_callback_t callback);

/**
 * @brief 停止遥测服务
 *
 * @return 成功返回0, 失败返回-1
 */
int telemetry_service_stop(void);

/**
 * @brief 获取遥测服务状态
 *
 * @return 服务状态
 */
telemetry_status_t telemetry_service_get_status(void);

/**
 * @brief 发送控制命令
 *
 * @param throttle 油门值 (0-1000)
 * @param direction 方向值 (0-1000)
 * @return 成功返回0, 失败返回-1
 */
int telemetry_service_send_control(int32_t throttle, int32_t direction);

/**
 * @brief 获取当前遥测数据
 *
 * @param data 输出数据结构指针
 * @return 成功返回0, 失败返回-1
 */
int telemetry_service_get_data(telemetry_data_t* data);

/**
 * @brief (由接收器调用) 更新遥测数据并通知UI
 *
 * @param telemetry_data 从网络接收到的原始遥测数据负载
 */
void telemetry_service_update_data(const telemetry_data_t* telemetry_data);

/**
 * @brief 反初始化遥测服务
 */
void telemetry_service_deinit(void);

/**
 * @brief 注入测试ELRS链路统计数据 (调试函数)
 * 用于验证UI显示逻辑，生成模拟的ELRS链路统计和RC通道数据
 */
void telemetry_service_inject_test_data(void);

/**
 * @brief RSSI值转换工具函数
 * 将存储的RSSI值 (dBm + 120偏移) 转换为实际的dBm值
 * 
 * @param rssi_raw 存储的RSSI原始值 (0-255)
 * @return 实际的RSSI值 (dBm)，范围 -120 to 135
 */
static inline int16_t telemetry_rssi_raw_to_dbm(uint8_t rssi_raw) {
    return (int16_t)rssi_raw - 120;
}

/**
 * @brief dBm转换为百分比 (用于UI显示)
 * 将RSSI dBm值转换为0-100的百分比
 * 
 * @param rssi_dbm RSSI值 (dBm)
 * @return 百分比值 (0-100)
 */
static inline uint8_t telemetry_rssi_dbm_to_percent(int16_t rssi_dbm) {
    // 典型范围: -100dBm (差) 到 -60dBm (好)
    // 映射: -100 -> 0%, -60 -> 100%
    if (rssi_dbm <= -100) return 0;
    if (rssi_dbm >= -60) return 100;
    return (uint8_t)((rssi_dbm + 100) * 100 / 40);
}

#endif // TELEMETRY_MAIN_H
