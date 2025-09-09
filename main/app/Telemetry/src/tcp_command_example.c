/**
 * @file tcp_command_example.c
 * @brief TCP命令服务器使用示例
 * @author TidyCraze
 * @date 2025-01-27
 */

#include "tcp_command_server.h"
#include "telemetry_protocol.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char* TAG = "TCP_CMD_EXAMPLE";

/**
 * @brief 命令处理回调函数示例
 * 
 * @param frame_type 帧类型
 * @param payload 负载数据
 * @param payload_len 负载长度
 * @param client_index 客户端索引
 * @return command_result_t 处理结果
 */
static command_result_t example_command_handler(uint8_t frame_type, const uint8_t* payload, 
                                              size_t payload_len, uint32_t client_index) {
    command_result_t result = {0};
    
    ESP_LOGI(TAG, "收到命令: 类型=0x%02X, 长度=%d, 客户端=%d", frame_type, payload_len, client_index);
    
    switch (frame_type) {
        case FRAME_TYPE_RC: {
            // 处理遥控命令
            if (payload_len >= 1) {
                uint8_t channel_count = payload[0];
                ESP_LOGI(TAG, "遥控命令: %d个通道", channel_count);
                
                if (payload_len >= 1 + channel_count * 2) {
                    const uint16_t* channels = (const uint16_t*)&payload[1];
                    for (int i = 0; i < channel_count; i++) {
                        ESP_LOGI(TAG, "通道%d: %d", i, channels[i]);
                    }
                    
                    result.status = ACK_STATUS_SUCCESS;
                    // 可以返回一些状态信息
                    static uint8_t rc_response[] = {0x01}; // 表示遥控命令已执行
                    result.response_data = rc_response;
                    result.response_len = sizeof(rc_response);
                } else {
                    result.status = ACK_STATUS_INVALID_PARAM;
                }
            } else {
                result.status = ACK_STATUS_INVALID_PARAM;
            }
            break;
        }
        
        case FRAME_TYPE_EXT_CMD: {
            // 处理扩展命令
            if (payload_len >= 2) {
                uint8_t cmd_id = payload[0];
                uint8_t param_len = payload[1];
                const uint8_t* params = (payload_len > 2) ? &payload[2] : NULL;
                
                ESP_LOGI(TAG, "扩展命令: ID=0x%02X, 参数长度=%d", cmd_id, param_len);
                
        case FRAME_TYPE_SPECIAL_CMD:
            ESP_LOGI(TAG, "收到特殊命令帧");
            // 处理特殊命令
            result.status = ACK_STATUS_SUCCESS;
            break;
            
        case FRAME_TYPE_IMAGE_TRANSFER:
            ESP_LOGI(TAG, "收到图传命令帧");
            // 处理图传命令
            result.status = ACK_STATUS_SUCCESS;
            static uint8_t image_response[] = {0x01};
            result.response_data = image_response;
            result.response_len = 1;
            break;
                
                switch (cmd_id) {
                    case EXT_CMD_ID_SET_PWM_FREQ: {
                        if (param_len >= 2) {
                            uint16_t freq = (params[1] << 8) | params[0];
                            ESP_LOGI(TAG, "设置PWM频率: %d Hz", freq);
                            result.status = ACK_STATUS_SUCCESS;
                            
                            // 返回设置后的实际频率
                            static uint8_t pwm_response[2];
                            pwm_response[0] = freq & 0xFF;
                            pwm_response[1] = (freq >> 8) & 0xFF;
                            result.response_data = pwm_response;
                            result.response_len = 2;
                        } else {
                            result.status = ACK_STATUS_INVALID_PARAM;
                        }
                        break;
                    }
                    
                    case EXT_CMD_ID_MODE_SWITCH: {
                        if (param_len >= 1) {
                            uint8_t mode = params[0];
                            ESP_LOGI(TAG, "模式切换: %d", mode);
                            result.status = ACK_STATUS_SUCCESS;
                            
                            // 返回当前模式
                            static uint8_t mode_response[] = {mode};
                            result.response_data = mode_response;
                            result.response_len = 1;
                        } else {
                            result.status = ACK_STATUS_INVALID_PARAM;
                        }
                        break;
                    }
                    
                    case EXT_CMD_ID_REQUEST_TELEMETRY: {
                        ESP_LOGI(TAG, "请求遥测数据");
                        
                        // 创建遥测数据并发送给客户端
                        telemetry_data_payload_t telemetry = {
                            .voltage_mv = 12000,    // 12V
                            .current_ma = 1500,     // 1.5A
                            .roll_deg = 100,        // 1.00度
                            .pitch_deg = -50,       // -0.50度
                            .yaw_deg = 18000,       // 180.00度
                            .altitude_cm = 15000    // 150米
                        };
                        
                        // 发送遥测数据到客户端
                        tcp_command_server_send_to_client(client_index, FRAME_TYPE_TELEMETRY, 
                                                         (const uint8_t*)&telemetry, sizeof(telemetry));
                        
                        result.status = ACK_STATUS_SUCCESS;
                        static uint8_t telemetry_response[] = {0x01}; // 表示遥测数据已发送
                        result.response_data = telemetry_response;
                        result.response_len = 1;
                        break;
                    }
                    
                    case EXT_CMD_ID_LIGHT_CONTROL: {
                        if (param_len >= 2) {
                            uint8_t light_id = params[0];
                            uint8_t brightness = params[1];
                            ESP_LOGI(TAG, "灯光控制: ID=%d, 亮度=%d", light_id, brightness);
                            result.status = ACK_STATUS_SUCCESS;
                            
                            // 返回设置结果
                            static uint8_t light_response[2];
                            light_response[0] = light_id;
                            light_response[1] = brightness;
                            result.response_data = light_response;
                            result.response_len = 2;
                        } else {
                            result.status = ACK_STATUS_INVALID_PARAM;
                        }
                        break;
                    }
                    
                    default:
                        ESP_LOGW(TAG, "未知扩展命令ID: 0x%02X", cmd_id);
                        result.status = ACK_STATUS_UNSUPPORTED;
                        break;
                }
            } else {
                result.status = ACK_STATUS_INVALID_PARAM;
            }
            break;
        }
        
        case FRAME_TYPE_HEARTBEAT: {
            // 处理心跳包
            ESP_LOGI(TAG, "收到心跳包");
            result.status = ACK_STATUS_SUCCESS;
            
            // 返回设备状态
            static uint8_t heartbeat_response[] = {0x01}; // 设备正常
            result.response_data = heartbeat_response;
            result.response_len = 1;
            break;
        }
        
        default:
            ESP_LOGW(TAG, "未知帧类型: 0x%02X", frame_type);
            result.status = ACK_STATUS_UNSUPPORTED;
            break;
    }
    
    return result;
}

/**
 * @brief 连接状态变更回调函数示例
 * 
 * @param client_index 客户端索引
 * @param connected 连接状态
 */
static void example_connection_callback(uint32_t client_index, bool connected) {
    if (connected) {
        ESP_LOGI(TAG, "客户端 %d 已连接", client_index);
    } else {
        ESP_LOGI(TAG, "客户端 %d 已断开", client_index);
    }
}

/**
 * @brief 定时发送遥测数据任务
 */
static void telemetry_sender_task(void* pvParameters) {
    (void)pvParameters;
    
    telemetry_data_payload_t telemetry;
    uint32_t counter = 0;
    
    while (1) {
        // 模拟遥测数据
        telemetry.voltage_mv = 12000 + (counter % 1000);
        telemetry.current_ma = 1500 + (counter % 500);
        telemetry.roll_deg = (counter % 3600) - 1800;  // -18.00 到 +18.00 度
        telemetry.pitch_deg = (counter % 1800) - 900;  // -9.00 到 +9.00 度
        telemetry.yaw_deg = (counter * 10) % 36000;    // 0 到 360.00 度
        telemetry.altitude_cm = 10000 + (counter % 5000); // 100-150米
        
        // 广播遥测数据到所有连接的客户端
        uint32_t sent_count = tcp_command_server_broadcast(FRAME_TYPE_TELEMETRY, 
                                                          (const uint8_t*)&telemetry, 
                                                          sizeof(telemetry));
        
        if (sent_count > 0) {
            ESP_LOGI(TAG, "遥测数据已发送给 %d 个客户端", sent_count);
        }
        
        counter++;
        vTaskDelay(pdMS_TO_TICKS(5000)); // 每5秒发送一次
    }
}

/**
 * @brief 初始化并启动TCP命令服务器示例
 */
void tcp_command_example_init(void) {
    ESP_LOGI(TAG, "初始化TCP命令服务器示例");
    
    // 配置服务器参数
    tcp_cmd_server_config_t config = {
        .server_port = 8080,
        .max_clients = 3,
        .ack_timeout_ms = 5000,
        .recv_timeout_ms = 10000
    };
    
    // 初始化服务器
    if (!tcp_command_server_init(&config)) {
        ESP_LOGE(TAG, "TCP命令服务器初始化失败");
        return;
    }
    
    // 启动服务器
    if (!tcp_command_server_start("tcp_cmd_srv", 8192, 5, 
                                 example_command_handler, 
                                 example_connection_callback)) {
        ESP_LOGE(TAG, "TCP命令服务器启动失败");
        return;
    }
    
    // 创建遥测数据发送任务
    xTaskCreate(telemetry_sender_task, "telemetry_sender", 4096, NULL, 4, NULL);
    
    ESP_LOGI(TAG, "TCP命令服务器示例启动成功，监听端口: %d", config.server_port);
}

/**
 * @brief 停止TCP命令服务器示例
 */
void tcp_command_example_deinit(void) {
    ESP_LOGI(TAG, "停止TCP命令服务器示例");
    tcp_command_server_stop();
    tcp_command_server_destroy();
}

/**
 * @brief 打印服务器状态
 */
void tcp_command_example_print_status(void) {
    tcp_command_server_print_status();
}