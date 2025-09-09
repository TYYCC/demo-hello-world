#ifndef P2P_UDP_IMAGE_TRANSFER_H
#define P2P_UDP_IMAGE_TRANSFER_H

#include "esp_err.h"
#include "esp_jpeg_common.h"
#include "stdbool.h"

#ifdef __cplusplus
extern "C" {
#endif

// UDP协议配置
#define P2P_UDP_PORT 6789
#define P2P_UDP_MAX_PACKET_SIZE 1400        // MTU减去头部开销
#define P2P_UDP_MAX_FRAME_SIZE (200 * 1024) // 最大JPEG帧大小
#define P2P_UDP_MAX_PACKETS_PER_FRAME (P2P_UDP_MAX_FRAME_SIZE / (P2P_UDP_MAX_PACKET_SIZE - 32))
#define P2P_UDP_ACK_TIMEOUT_MS 100 // ACK超时时间
#define P2P_UDP_MAX_RETRIES 3      // 最大重传次数

// Wi-Fi P2P配置
#define P2P_WIFI_SSID_PREFIX "ESP32_P2P_"
#define P2P_WIFI_PASSWORD "12345678"
#define P2P_WIFI_CHANNEL 6

// 数据包类型
typedef enum {
    P2P_UDP_PACKET_TYPE_FRAME_START = 0x01, // 帧开始包
    P2P_UDP_PACKET_TYPE_FRAME_DATA = 0x02,  // 帧数据包
    P2P_UDP_PACKET_TYPE_FRAME_END = 0x03,   // 帧结束包
    P2P_UDP_PACKET_TYPE_ACK = 0x04,         // 确认包
    P2P_UDP_PACKET_TYPE_NACK = 0x05,        // 否认包
    P2P_UDP_PACKET_TYPE_HEARTBEAT = 0x06,   // 心跳包
} p2p_udp_packet_type_t;

// 数据包头部结构 (固定32字节)
typedef struct __attribute__((packed)) {
    uint32_t magic;         // 魔数标识: 0x50325055 ("P2PU")
    uint8_t packet_type;    // 包类型
    uint8_t version;        // 协议版本
    uint16_t sequence_num;  // 序列号
    uint32_t frame_id;      // 帧ID
    uint16_t packet_id;     // 当前包在帧中的ID
    uint16_t total_packets; // 该帧总包数
    uint32_t frame_size;    // 帧总大小
    uint16_t data_size;     // 当前包数据大小
    uint16_t checksum;      // 数据校验和
    uint32_t timestamp;     // 时间戳
    uint8_t reserved[4];    // 保留字段
} p2p_udp_packet_header_t;

// 帧信息结构
typedef struct {
    uint32_t frame_id;         // 帧ID
    uint32_t frame_size;       // 帧大小
    uint16_t total_packets;    // 总包数
    uint16_t received_packets; // 已接收包数
    uint8_t* frame_buffer;     // 帧缓冲区
    bool* packet_received;     // 包接收状态数组
    uint32_t last_update_time; // 最后更新时间
    bool is_complete;          // 帧是否完整
} p2p_udp_frame_info_t;

// P2P连接状态
typedef enum {
    P2P_STATE_IDLE = 0,
    P2P_STATE_AP_STARTING,
    P2P_STATE_AP_RUNNING,
    P2P_STATE_STA_CONNECTING,
    P2P_STATE_STA_CONNECTED,
    P2P_STATE_ERROR
} p2p_connection_state_t;

// 连接模式
typedef enum {
    P2P_MODE_AP = 0, // 作为热点
    P2P_MODE_STA = 1 // 作为客户端
} p2p_connection_mode_t;

// 事件回调函数类型
typedef void (*p2p_udp_image_callback_t)(uint8_t* img_buf, int width, int height, jpeg_pixel_format_t format);
typedef void (*p2p_udp_status_callback_t)(p2p_connection_state_t state, const char* info);

/**
 * @brief 初始化P2P UDP图传系统
 * @param mode 连接模式 (AP或STA)
 * @param image_callback 图像接收回调函数
 * @param status_callback 状态变化回调函数
 * @return esp_err_t
 */
esp_err_t p2p_udp_image_transfer_init(p2p_connection_mode_t mode, p2p_udp_image_callback_t image_callback,
                                      p2p_udp_status_callback_t status_callback);

/**
 * @brief 启动P2P UDP图传服务
 * @return esp_err_t
 */
esp_err_t p2p_udp_image_transfer_start(void);

/**
 * @brief 停止P2P UDP图传服务
 */
void p2p_udp_image_transfer_stop(void);

/**
 * @brief 反初始化P2P UDP图传系统
 */
void p2p_udp_image_transfer_deinit(void);

/**
 * @brief 发送JPEG图像数据
 * @param jpeg_data JPEG数据指针
 * @param jpeg_size JPEG数据大小
 * @return esp_err_t
 */
esp_err_t p2p_udp_send_image(const uint8_t* jpeg_data, uint32_t jpeg_size);

/**
 * @brief 作为STA连接到指定的P2P热点
 * @param ap_ssid 热点SSID
 * @param ap_password 热点密码
 * @return esp_err_t
 */
esp_err_t p2p_udp_connect_to_ap(const char* ap_ssid, const char* ap_password);

/**
 * @brief 获取当前连接状态
 * @return p2p_connection_state_t
 */
p2p_connection_state_t p2p_udp_get_connection_state(void);

/**
 * @brief 获取本地IP地址
 * @param ip_str 存储IP地址字符串的缓冲区
 * @param max_len 缓冲区最大长度
 * @return esp_err_t
 */
esp_err_t p2p_udp_get_local_ip(char* ip_str, size_t max_len);

/**
 * @brief 获取连接统计信息
 * @param tx_packets 发送包数
 * @param rx_packets 接收包数
 * @param lost_packets 丢失包数
 * @param retx_packets 重传包数
 */
void p2p_udp_get_stats(uint32_t* tx_packets, uint32_t* rx_packets, uint32_t* lost_packets, uint32_t* retx_packets);

/**
 * @brief 获取当前解码帧率
 * @return float
 */
float p2p_udp_get_fps(void);

/**
 * @brief 重置统计信息
 */
void p2p_udp_reset_stats(void);

#ifdef __cplusplus
}
#endif

#endif // P2P_UDP_IMAGE_TRANSFER_H
