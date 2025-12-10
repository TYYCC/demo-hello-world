/**
 * @file telemetry_structures_reference.h
 * @brief ELRS 遥测数据结构参考和工具函数
 * 
 * 本文件汇总了所有关键的遥测相关结构体定义，
 * 来自于 ELRS 组件库的不同模块。
 * 
 * 包含的结构体:
 * - 链路统计 (LinkStats, LinkStatistics)
 * - 遥测数据包 (Telemetry Packets)
 * - RSSI/SNR/LQ 相关定义
 * - RC 通道数据
 * - OTA 数据包格式
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== 1. 链路统计结构 ==================== */

/**
 * @brief CRSF 标准链路统计数据包
 * 
 * 大小: 10字节
 * 
 * 这是 CRSF 协议定义的标准链路统计包，包含上下行RSSI、SNR和链路质量。
 */
typedef struct __attribute__((packed)) {
    uint8_t uplink_RSSI_1;          /**< 上行链路 RSSI 天线1 (dBm * -1) */
    uint8_t uplink_RSSI_2;          /**< 上行链路 RSSI 天线2 (dBm * -1) */
    uint8_t uplink_Link_quality;    /**< 上行链路质量 / 包成功率 (0-100 %) */
    int8_t uplink_SNR;              /**< 上行链路 SNR (dB) */
    uint8_t active_antenna;         /**< 活跃天线 (0=天线1, 1=天线2) */
    uint8_t rf_Mode;                /**< RF模式 (0=4fps, 1=50fps, 2=150fps) */
    uint8_t uplink_TX_Power;        /**< 上行发送功率等级 (0-6) */
    uint8_t downlink_RSSI_1;        /**< 下行链路 RSSI (dBm * -1) */
    uint8_t downlink_Link_quality;  /**< 下行链路质量 / 包成功率 (0-100 %) */
    int8_t downlink_SNR;            /**< 下行链路 SNR (dB) */
} crsfLinkStatistics_t;

_Static_assert(sizeof(crsfLinkStatistics_t) == 10, "crsfLinkStatistics_t size must be 10");

/**
 * @brief ELRS 扩展链路统计数据包
 * 
 * 大小: 11字节
 * 
 * 基于 CRSF 标准结构，添加了第二个下行天线的 RSSI 支持，
 * 提供更完整的多天线分集信息。
 */
typedef struct __attribute__((packed)) {
    /* 继承 crsfLinkStatistics_t 的所有字段 */
    uint8_t uplink_RSSI_1;          /**< 上行链路 RSSI 天线1 (dBm * -1) */
    uint8_t uplink_RSSI_2;          /**< 上行链路 RSSI 天线2 (dBm * -1) */
    uint8_t uplink_Link_quality;    /**< 上行链路质量 (0-100 %) */
    int8_t uplink_SNR;              /**< 上行 SNR (dB) */
    uint8_t active_antenna;         /**< 活跃天线 */
    uint8_t rf_Mode;                /**< RF 模式 */
    uint8_t uplink_TX_Power;        /**< 发送功率等级 */
    uint8_t downlink_RSSI_1;        /**< 下行 RSSI 天线1 */
    uint8_t downlink_Link_quality;  /**< 下行链路质量 */
    int8_t downlink_SNR;            /**< 下行 SNR */
    
    /* ELRS 特有字段 */
    uint8_t downlink_RSSI_2;        /**< 下行链路 RSSI 天线2 (dBm * -1) - ELRS 扩展 */
} elrsLinkStatistics_t;

_Static_assert(sizeof(elrsLinkStatistics_t) == 11, "elrsLinkStatistics_t size must be 11");

/**
 * @brief OTA 无线协议链路统计 (紧凑格式)
 * 
 * 大小: 4字节
 * 
 * 用于无线传输中的链路统计数据，使用位字段压缩以节省带宽。
 * 这个结构体嵌入在 OTA 数据包的下行数据中。
 */
typedef struct __attribute__((packed)) {
    uint8_t uplink_RSSI_1 : 7;      /**< 上行 RSSI 天线1 (7位, dBm * -1) */
    uint8_t antenna : 1;            /**< 当前活跃天线 (1位) */
    
    uint8_t uplink_RSSI_2 : 7;      /**< 上行 RSSI 天线2 (7位, dBm * -1) */
    uint8_t modelMatch : 1;         /**< 模型匹配标志 (1位) */
    
    uint8_t lq : 7;                 /**< 链路质量 (7位, 0-100 %) */
    uint8_t trueDiversityAvailable : 1;  /**< 真实多样性可用 (1位) */
    
    int8_t SNR;                     /**< 信噪比 (带符号, dB) */
} OTA_LinkStats_s;

_Static_assert(sizeof(OTA_LinkStats_s) == 4, "OTA_LinkStats_s size must be 4");

/**
 * @brief ELRS 状态参数 (包统计)
 * 
 * 用于 CRSF FRAMETYPE_ELRS_STATUS (0x2E) 帧
 */
typedef struct __attribute__((packed)) {
    uint8_t pktsBad;                /**< 坏包计数 */
    uint16_t pktsGood;              /**< 好包计数 (Big-Endian) */
    uint8_t flags;                  /**< 状态标志 */
    char msg[1];                    /**< 空终止的状态消息字符串 */
} elrsStatusParameter_t;

/* ==================== 2. RC 通道数据 ==================== */

/**
 * @brief CRSF 打包的 16 通道 RC 数据
 * 
 * 大小: 22字节
 * 
 * 使用位字段将 16 个通道的数据压缩到 22 字节中，
 * 每个通道 11 位精度。
 * 
 * 通道值映射:
 * - 191   = 1000 µs
 * - 992   = 1500 µs (中心)
 * - 1792  = 2000 µs
 * - 范围: 0-2047
 */
typedef struct __attribute__((packed)) {
    unsigned ch0 : 11;              /**< RC 通道 0 (11位) */
    unsigned ch1 : 11;              /**< RC 通道 1 (11位) */
    unsigned ch2 : 11;              /**< RC 通道 2 (11位) */
    unsigned ch3 : 11;              /**< RC 通道 3 (11位) */
    unsigned ch4 : 11;              /**< RC 通道 4 (11位) */
    unsigned ch5 : 11;              /**< RC 通道 5 (11位) */
    unsigned ch6 : 11;              /**< RC 通道 6 (11位) */
    unsigned ch7 : 11;              /**< RC 通道 7 (11位) */
    unsigned ch8 : 11;              /**< RC 通道 8 (11位) */
    unsigned ch9 : 11;              /**< RC 通道 9 (11位) */
    unsigned ch10 : 11;             /**< RC 通道 10 (11位) */
    unsigned ch11 : 11;             /**< RC 通道 11 (11位) */
    unsigned ch12 : 11;             /**< RC 通道 12 (11位) */
    unsigned ch13 : 11;             /**< RC 通道 13 (11位) */
    unsigned ch14 : 11;             /**< RC 通道 14 (11位) */
    unsigned ch15 : 11;             /**< RC 通道 15 (11位) */
} crsf_channels_t;

_Static_assert(sizeof(crsf_channels_t) == 22, "crsf_channels_t size must be 22");

/**
 * @brief OTA 4x10位 通道编码 (4个通道, 每个10位)
 * 
 * 大小: 5字节
 * 
 * 用于 OTA 数据包中的通道数据，将 4 个 10 位通道压缩为 5 字节。
 */
typedef struct __attribute__((packed)) {
    uint8_t raw[5];                 /**< 4个10位通道的原始编码 */
} OTA_Channels_4x10;

/* ==================== 3. OTA 数据包结构 ==================== */

/**
 * @brief OTA 同步包数据
 * 
 * 大小: 6字节
 */
typedef struct __attribute__((packed)) {
    uint8_t fhssIndex;              /**< FHSS 跳频索引 */
    uint8_t nonce;                  /**< 一次性数字 */
    uint8_t rfRateEnum;             /**< 射频速率枚举 */
    uint8_t switchEncMode : 1;      /**< 开关编码模式 */
    uint8_t newTlmRatio : 3;        /**< 新遥测比率 */
    uint8_t geminiMode : 1;         /**< Gemini 模式 */
    uint8_t otaProtocol : 2;        /**< OTA 协议版本 */
    uint8_t free : 1;               /**< 保留位 */
    uint8_t UID4;                   /**< UID 字节 4 */
    uint8_t UID5;                   /**< UID 字节 5 */
} OTA_Sync_s;

_Static_assert(sizeof(OTA_Sync_s) == 6, "OTA_Sync_s size must be 6");

/**
 * @brief OTA 4字节数据包格式
 * 
 * 大小: 8字节
 * 
 * 紧凑的 OTA 数据包格式，用于带宽限制的应用。
 */
typedef struct __attribute__((packed)) {
    uint8_t type : 2;               /**< 数据包类型 (0=RCDATA, 1=DATA, 2=SYNC) */
    uint8_t crcHigh : 6;            /**< CRC 高 6 位 */
    
    union {
        struct {                    /**< PACKET_TYPE_RCDATA */
            OTA_Channels_4x10 ch;
            uint8_t switches : 7;
            uint8_t isArmed : 1;
        } rc;
        
        struct {                    /**< PACKET_TYPE_DATA 下行 */
            uint8_t packageIndex : 7;
            uint8_t stubbornAck : 1;
            union {
                struct {
                    OTA_LinkStats_s stats;
                    uint8_t payload[1];  /* ELRS4_DATA_DL_BYTES_PER_CALL - 4 */
                } ul_link_stats;
                uint8_t payload[5];
            };
        } data_dl;
        
        OTA_Sync_s sync;            /**< PACKET_TYPE_SYNC */
    };
    
    uint8_t crcLow;                 /**< CRC 低 8 位 */
} OTA_Packet4_s;

_Static_assert(sizeof(OTA_Packet4_s) == 8, "OTA_Packet4_s size must be 8");

/**
 * @brief OTA 8字节数据包格式 (全分辨率)
 * 
 * 大小: 13字节
 * 
 * 扩展的 OTA 数据包格式，支持更多通道和数据。
 */
typedef struct __attribute__((packed)) {
    union {
        struct {                    /**< PACKET_TYPE_RCDATA */
            uint8_t packetType : 2;
            uint8_t stubbornAck : 1;
            uint8_t uplinkPower : 3;
            uint8_t isHighAux : 1;
            uint8_t isArmed : 1;
            OTA_Channels_4x10 chLow;   /* 通道 0-3 */
            OTA_Channels_4x10 chHigh;  /* AUX2-5 或 AUX6-9 */
        } rc;
        
        struct {                    /**< PACKET_TYPE_SYNC */
            uint8_t packetType;
            OTA_Sync_s sync;
            uint8_t free[4];
        } sync;
        
        struct {                    /**< PACKET_TYPE_DATA 下行 */
            uint8_t packetType : 2;
            uint8_t stubbornAck : 1;
            uint8_t packageIndex : 5;
            union {
                struct {
                    OTA_LinkStats_s stats;
                    uint8_t payload[6];  /* ELRS8_DATA_DL_BYTES_PER_CALL - 4 */
                } ul_link_stats;
                uint8_t payload[10];
            };
        } data_dl;
    };
    
    uint16_t crc;                   /**< CRC16 (Little-Endian) */
} OTA_Packet8_s;

_Static_assert(sizeof(OTA_Packet8_s) == 13, "OTA_Packet8_s size must be 13");

/**
 * @brief OTA 数据包 (可以是 4 字节或 8 字节格式)
 */
typedef union {
    OTA_Packet4_s std;              /**< 标准 4 字节格式 */
    OTA_Packet8_s full;             /**< 完整 8 字节格式 */
} OTA_Packet_s;

/* ==================== 4. 其他遥测传感器数据 ==================== */

/**
 * @brief CRSF 电池传感器数据
 * 大小: 7字节
 */
typedef struct __attribute__((packed)) {
    unsigned voltage : 16;          /**< 电压 (mV * 100, Big-Endian) */
    unsigned current : 16;          /**< 电流 (mA * 100, Big-Endian) */
    unsigned capacity : 24;         /**< 容量 (mAh) */
    unsigned remaining : 8;         /**< 剩余百分比 (%) */
} crsf_sensor_battery_t;

/**
 * @brief CRSF GPS 数据
 * 大小: 15字节
 */
typedef struct __attribute__((packed)) {
    int32_t latitude;               /**< 纬度 / 10,000,000 */
    int32_t longitude;              /**< 经度 / 10,000,000 */
    uint16_t groundspeed;           /**< 地速 (km/h / 10) */
    uint16_t gps_heading;           /**< GPS 航向 (度 / 100) */
    uint16_t altitude;              /**< 高度 (米, -1000m 偏移) */
    uint8_t satellites_in_use;      /**< 可用卫星数 */
} crsf_sensor_gps_t;

/**
 * @brief CRSF 姿态传感器数据
 * 大小: 6字节
 */
typedef struct __attribute__((packed)) {
    int16_t pitch;                  /**< 俯仰 (弧度 * 10000) */
    int16_t roll;                   /**< 滚转 (弧度 * 10000) */
    int16_t yaw;                    /**< 偏航 (弧度 * 10000) */
} crsf_sensor_attitude_t;

/**
 * @brief CRSF 气压/高度计数据
 * 大小: 4字节
 */
typedef struct __attribute__((packed)) {
    uint16_t altitude;              /**< 高度 (分米 + 10000dm, Big-Endian) */
    int16_t verticalspd;            /**< 垂直速度 (cm/s, Big-Endian) */
} crsf_sensor_baro_vario_t;

/**
 * @brief CRSF 升降率数据
 * 大小: 2字节
 */
typedef struct __attribute__((packed)) {
    int16_t verticalspd;            /**< 垂直速度 (cm/s, Big-Endian) */
} crsf_sensor_vario_t;

/* ==================== 5. CRSF 帧类型定义 ==================== */

/**
 * @brief CRSF 帧类型枚举
 */
typedef enum {
    CRSF_FRAMETYPE_GPS = 0x02,
    CRSF_FRAMETYPE_VARIO = 0x07,
    CRSF_FRAMETYPE_BATTERY_SENSOR = 0x08,
    CRSF_FRAMETYPE_BARO_ALTITUDE = 0x09,
    CRSF_FRAMETYPE_AIRSPEED = 0x0A,
    CRSF_FRAMETYPE_HEARTBEAT = 0x0B,
    CRSF_FRAMETYPE_RPM = 0x0C,
    CRSF_FRAMETYPE_TEMP = 0x0D,
    CRSF_FRAMETYPE_CELLS = 0x0E,
    CRSF_FRAMETYPE_LINK_STATISTICS = 0x14,    /**< ⭐ 链路统计帧 */
    CRSF_FRAMETYPE_RC_CHANNELS_PACKED = 0x16,
    CRSF_FRAMETYPE_ATTITUDE = 0x1E,
    CRSF_FRAMETYPE_FLIGHT_MODE = 0x21,
    CRSF_FRAMETYPE_DEVICE_PING = 0x28,
    CRSF_FRAMETYPE_DEVICE_INFO = 0x29,
    CRSF_FRAMETYPE_PARAMETER_SETTINGS_ENTRY = 0x2B,
    CRSF_FRAMETYPE_PARAMETER_READ = 0x2C,
    CRSF_FRAMETYPE_PARAMETER_WRITE = 0x2D,
    CRSF_FRAMETYPE_ELRS_STATUS = 0x2E,        /**< ELRS 状态帧 */
    CRSF_FRAMETYPE_COMMAND = 0x32,
    CRSF_FRAMETYPE_HANDSET = 0x3A,
    CRSF_FRAMETYPE_KISS_REQ = 0x78,
    CRSF_FRAMETYPE_KISS_RESP = 0x79,
    CRSF_FRAMETYPE_MSP_REQ = 0x7A,
    CRSF_FRAMETYPE_MSP_RESP = 0x7B,
    CRSF_FRAMETYPE_MSP_WRITE = 0x7C,
    CRSF_FRAMETYPE_ARDUPILOT_RESP = 0x80,
} crsf_frame_type_e;

/* ==================== 6. 重要常数 ==================== */

/** @defgroup TELEMETRY_CONSTANTS 遥测常数 */
/** @{ */

#define ELRS4_DATA_DL_BYTES_PER_CALL 5         /**< ELRS 4字节模式下行字节数 */
#define ELRS8_DATA_DL_BYTES_PER_CALL 10        /**< ELRS 8字节模式下行字节数 */
#define ELRS4_PACKET_SIZE 8                    /**< OTA 4字节包大小 */
#define ELRS8_PACKET_SIZE 13                   /**< OTA 8字节包大小 */

#define CRSF_CHANNEL_VALUE_MIN 172             /**< 987 µs */
#define CRSF_CHANNEL_VALUE_1000 191            /**< 1000 µs */
#define CRSF_CHANNEL_VALUE_MID 992             /**< 1500 µs (中心) */
#define CRSF_CHANNEL_VALUE_2000 1792           /**< 2000 µs */
#define CRSF_CHANNEL_VALUE_MAX 1811            /**< 2012 µs */

#define RADIO_SNR_SCALE 4                      /**< SNR 缩放因子 */
#define CRSF_CRC_POLY 0xd5                     /**< CRSF CRC 多项式 */

/** @} */

/* ==================== 7. 辅助函数和宏 ==================== */

/**
 * @brief 将 RSSI 值转换为 dBm
 * @param rssi_value RSSI 原始值 (0-255)
 * @return dBm 值 (负数)
 */
static inline int16_t rssi_to_dbm(uint8_t rssi_value) {
    return -(int16_t)rssi_value;
}

/**
 * @brief 将 dBm 值转换为 RSSI 值
 * @param dbm dBm 值 (负数)
 * @return RSSI 值 (0-255)
 */
static inline uint8_t dbm_to_rssi(int16_t dbm) {
    return (uint8_t)(-dbm);
}

/**
 * @brief CRSF 通道值转换为微秒
 * @param crsf_val CRSF 通道值 (172-1811)
 * @return 微秒值 (988-2012)
 */
static inline uint16_t crsf_to_us(uint16_t crsf_val) {
    // 简化版本
    return (crsf_val - CRSF_CHANNEL_VALUE_MIN) * 1024 / 1640 + 988;
}

/**
 * @brief 微秒值转换为 CRSF 通道值
 * @param us_val 微秒值 (988-2012)
 * @return CRSF 通道值 (172-1811)
 */
static inline uint16_t us_to_crsf(uint16_t us_val) {
    return (us_val - 988) * 1640 / 1024 + CRSF_CHANNEL_VALUE_MIN;
}

/* ==================== 8. 全局变量声明 ==================== */

#ifdef TARGET_RX
/**
 * @brief 全局链路统计数据
 * 
 * 由接收端更新，包含最新的链路统计信息
 */
extern elrsLinkStatistics_t linkStats;
#endif

#ifdef __cplusplus
}
#endif

#endif /* TELEMETRY_STRUCTURES_REFERENCE_H */
