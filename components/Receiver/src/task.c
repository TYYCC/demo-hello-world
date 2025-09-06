/*
 * @Author: tidycraze 2595256284@qq.com
 * @Date: 2025-09-05 10:04:45
 * @LastEditors: tidycraze 2595256284@qq.com
 * @LastEditTime: 2025-09-05 16:13:10
 * @FilePath: \demo-hello-world\components\Receiver\src\task.c
 * @Description: 任务实现
 * 
 */

#include "task.h"
#include "tcp_client_hb.h"
#include "tcp_client_telemetry.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

static const char *TAG = "Task";

// TCP任务管理器状态
static bool s_tcp_manager_initialized = false;
static bool s_tcp_modules_running = false;
static TaskHandle_t s_tcp_task_handle = NULL;
static EventGroupHandle_t s_tcp_event_group = NULL;

// 事件位定义
#define TCP_WIFI_CONNECTED_BIT    BIT0
#define TCP_WIFI_DISCONNECTED_BIT BIT1
#define TCP_STOP_TASK_BIT         BIT2

// TCP配置
static const char *s_server_ip = "192.168.76.229";

// 内部函数声明
static void wifi_event_callback(wifi_pairing_state_t state, const char* ssid);
static void tcp_task_function(void *pvParameters);
static esp_err_t start_tcp_modules(void);
static esp_err_t stop_tcp_modules(void);
static void pwm_control_callback(const remote_control_data_t* rc_data);

/**
 * @brief 初始化TCP任务管理器
 */
esp_err_t tcp_task_manager_init(void) {
    if (s_tcp_manager_initialized) {
        ESP_LOGW(TAG, "TCP任务管理器已初始化");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "初始化TCP任务管理器...");
    
    // 创建事件组
    s_tcp_event_group = xEventGroupCreate();
    if (!s_tcp_event_group) {
        ESP_LOGE(TAG, "创建TCP事件组失败");
        return ESP_ERR_NO_MEM;
    }
    
    s_tcp_manager_initialized = true;
    ESP_LOGI(TAG, "TCP任务管理器初始化完成");
    return ESP_OK;
}

/**
 * @brief 启动TCP任务管理器
 */
esp_err_t tcp_task_manager_start(void) {
    if (!s_tcp_manager_initialized) {
        ESP_LOGE(TAG, "TCP任务管理器未初始化");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (s_tcp_task_handle) {
        ESP_LOGW(TAG, "TCP任务已在运行");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "启动TCP任务管理器...");
    
    // 创建TCP任务
    BaseType_t ret = xTaskCreate(
        tcp_task_function,
        "tcp_task",
        8192,  // 增大栈大小
        NULL,
        5,
        &s_tcp_task_handle
    );
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "创建TCP任务失败");
        return ESP_ERR_NO_MEM;
    }
    
    ESP_LOGI(TAG, "TCP任务管理器启动成功");
    return ESP_OK;
}

/**
 * @brief 启动带WIFI事件集成的TCP任务管理器
 * @param wifi_config WIFI配对配置
 * @return ESP_OK 成功，其他值表示错误
 */
esp_err_t tcp_task_manager_start_with_wifi(const wifi_pairing_config_t* wifi_config) {
    if (!wifi_config) {
        ESP_LOGE(TAG, "WIFI配置参数为空");
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "启动带WIFI事件集成的TCP任务管理器...");
    
    // 1. 初始化WIFI配对管理器
    esp_err_t ret = wifi_pairing_manager_init(wifi_config, wifi_event_callback);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WIFI配对管理器初始化失败: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // 2. 初始化TCP任务管理器
    ret = tcp_task_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "TCP任务管理器初始化失败: %s", esp_err_to_name(ret));
        wifi_pairing_manager_deinit();
        return ret;
    }
    
    // 3. 启动WIFI配对管理器
    ret = wifi_pairing_manager_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WIFI配对管理器启动失败: %s", esp_err_to_name(ret));
        tcp_task_manager_stop();
        wifi_pairing_manager_deinit();
        return ret;
    }
    
    // 4. 启动TCP任务管理器
    ret = tcp_task_manager_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "TCP任务管理器启动失败: %s", esp_err_to_name(ret));
        wifi_pairing_manager_stop();
        wifi_pairing_manager_deinit();
        return ret;
    }
    
    ESP_LOGI(TAG, "带WIFI事件集成的TCP任务管理器启动成功");
    return ESP_OK;
}

/**
 * @brief 停止TCP任务管理器
 */
esp_err_t tcp_task_manager_stop(void) {
    if (!s_tcp_manager_initialized) {
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "停止TCP任务管理器...");
    
    // 设置停止标志
    if (s_tcp_event_group) {
        xEventGroupSetBits(s_tcp_event_group, TCP_STOP_TASK_BIT);
    }
    
    // 等待任务结束
    if (s_tcp_task_handle) {
        // 等待任务真正结束，最多等待5秒
        uint32_t wait_count = 0;
        while (eTaskGetState(s_tcp_task_handle) != eDeleted && wait_count < 50) {
            vTaskDelay(pdMS_TO_TICKS(100));
            wait_count++;
        }
        s_tcp_task_handle = NULL;
    }
    
    // 停止TCP模块
    stop_tcp_modules();
    
    // 清理资源
    if (s_tcp_event_group) {
        vEventGroupDelete(s_tcp_event_group);
        s_tcp_event_group = NULL;
    }
    
    s_tcp_manager_initialized = false;
    ESP_LOGI(TAG, "TCP任务管理器停止完成");
    return ESP_OK;
}

/**
 * @brief WIFI事件回调函数
 */
static void wifi_event_callback(wifi_pairing_state_t state, const char* ssid) {
    if (!s_tcp_event_group) {
        return;
    }
    
    switch (state) {
        case WIFI_PAIRING_STATE_CONNECTED:
            ESP_LOGI(TAG, "WIFI已连接: %s，准备启动TCP连接", ssid ? ssid : "Unknown");
            xEventGroupSetBits(s_tcp_event_group, TCP_WIFI_CONNECTED_BIT);
            xEventGroupClearBits(s_tcp_event_group, TCP_WIFI_DISCONNECTED_BIT);
            break;
            
        case WIFI_PAIRING_STATE_DISCONNECTED:
        case WIFI_PAIRING_STATE_IDLE:
            ESP_LOGI(TAG, "WIFI已断开，停止TCP连接");
            xEventGroupSetBits(s_tcp_event_group, TCP_WIFI_DISCONNECTED_BIT);
            xEventGroupClearBits(s_tcp_event_group, TCP_WIFI_CONNECTED_BIT);
            break;
            
        default:
            // 其他状态不处理
            break;
    }
}

/**
 * @brief TCP任务主函数
 */
static void tcp_task_function(void *pvParameters) {
    ESP_LOGI(TAG, "TCP任务启动，等待WIFI连接...");
    
    // 将当前任务添加到看门狗
    esp_task_wdt_add(NULL);
    
    uint32_t status_print_counter = 0;
    uint32_t telemetry_send_counter = 0;
    
    while (1) {
        // 重置看门狗
        esp_task_wdt_reset();
        
        // 等待事件
        EventBits_t bits = xEventGroupWaitBits(
            s_tcp_event_group,
            TCP_WIFI_CONNECTED_BIT | TCP_WIFI_DISCONNECTED_BIT | TCP_STOP_TASK_BIT,
            pdFALSE,  // 不清除事件位
            pdFALSE,  // 等待任意一个事件
            pdMS_TO_TICKS(1000)  // 1秒超时，给看门狗足够的重置时间
        );
        
        // 检查是否需要停止任务
        if (bits & TCP_STOP_TASK_BIT) {
            ESP_LOGI(TAG, "收到停止信号，退出TCP任务");
            xEventGroupClearBits(s_tcp_event_group, TCP_STOP_TASK_BIT);
            break;
        }
        
        // 处理WIFI连接事件
        if (bits & TCP_WIFI_CONNECTED_BIT) {
            if (!s_tcp_modules_running) {
                ESP_LOGI(TAG, "WIFI已连接，启动TCP模块");
                
                // 添加延时，确保WiFi连接稳定
                vTaskDelay(pdMS_TO_TICKS(2000));
                
                if (start_tcp_modules() == ESP_OK) {
                    s_tcp_modules_running = true;
                    ESP_LOGI(TAG, "TCP模块启动成功");
                } else {
                    ESP_LOGE(TAG, "TCP模块启动失败，将在下次WiFi连接时重试");
                }
            }
            // 清除已处理的连接事件位
            xEventGroupClearBits(s_tcp_event_group, TCP_WIFI_CONNECTED_BIT);
        }
        
        // 处理WIFI断开事件
        if (bits & TCP_WIFI_DISCONNECTED_BIT) {
            if (s_tcp_modules_running) {
                ESP_LOGI(TAG, "WIFI已断开，停止TCP模块");
                stop_tcp_modules();
                s_tcp_modules_running = false;
                ESP_LOGI(TAG, "TCP模块已停止");
            }
            // 清除已处理的断开事件位
            xEventGroupClearBits(s_tcp_event_group, TCP_WIFI_DISCONNECTED_BIT);
        }
        
        // 如果TCP模块正在运行，执行周期性任务
        if (s_tcp_modules_running) {
            // 每30秒打印一次状态信息
            if (++status_print_counter >= 30) { // 30秒 / 1秒 = 30次
                tcp_client_hb_print_status();
                tcp_client_telemetry_print_status();
                status_print_counter = 0;
            }
            
            // 每5秒发送一次遥测数据
            if (++telemetry_send_counter >= 5) { // 5秒 / 1秒 = 5次
                // 更新模拟遥测数据
                // tcp_client_telemetry_update_sim_data();
                telemetry_send_counter = 0;
            }
            
            // 检查连接健康状态（降低检查频率以减少日志输出）
            if (status_print_counter % 10 == 0) { // 每10秒检查一次
                if (!tcp_client_hb_is_connection_healthy()) {
                    ESP_LOGW(TAG, "心跳连接异常");
                }
                
                if (!tcp_client_telemetry_is_connection_healthy()) {
                    ESP_LOGW(TAG, "遥测连接异常");
                }
            }
        }
    }
    
    // 任务退出前清理
    stop_tcp_modules();
    s_tcp_modules_running = false;
    s_tcp_task_handle = NULL;
    
    // 从看门狗中移除任务
    esp_task_wdt_delete(NULL);
    
    ESP_LOGI(TAG, "TCP任务已退出");
    vTaskDelete(NULL);
}

/**
 * @brief 启动TCP模块
 */
static esp_err_t start_tcp_modules(void) {
    ESP_LOGI(TAG, "启动TCP模块，服务器IP: %s", s_server_ip);
    
    // 初始化心跳模块（使用默认端口7878）
    if (!tcp_client_hb_init(s_server_ip, 0)) {
        ESP_LOGE(TAG, "心跳模块初始化失败");
        return ESP_FAIL;
    }
    
    // 初始化遥测模块（使用默认端口6667）
    if (!tcp_client_telemetry_init(s_server_ip, 0)) {
        ESP_LOGE(TAG, "遥测模块初始化失败");
        return ESP_FAIL;
    }
    
    // 注册PWM控制回调函数
    tcp_client_telemetry_register_rc_callback(pwm_control_callback);
    ESP_LOGI(TAG, "PWM控制回调函数已注册");
    
    // 启动心跳模块
    if (!tcp_client_hb_start("tcp_hb_task", 4096, 5)) {
        ESP_LOGE(TAG, "心跳模块启动失败");
        return ESP_FAIL;
    }
    
    // 启动遥测模块
    if (!tcp_client_telemetry_start("tcp_telemetry_task", 4096, 5)) {
        ESP_LOGE(TAG, "遥测模块启动失败");
        return ESP_FAIL;
    }
    
    return ESP_OK;
}

/**
 * @brief 停止TCP模块
 */
static esp_err_t stop_tcp_modules(void) {
    ESP_LOGI(TAG, "停止TCP模块");
    
    // 停止心跳和遥测客户端
    tcp_client_hb_stop();
    tcp_client_telemetry_stop();
    
    return ESP_OK;
}

/**
 * @brief PWM控制回调函数
 * @param rc_data 遥控数据指针
 */
static void pwm_control_callback(const remote_control_data_t* rc_data) {
    if (!rc_data) {
        ESP_LOGW(TAG, "PWM回调: 遥控数据为空");
        return;
    }
    
    ESP_LOGI(TAG, "PWM控制数据解析:");
    ESP_LOGI(TAG, "  通道数量: %d", rc_data->channel_count);
    
    // 遍历所有通道并计算占空比
    for (int i = 0; i < rc_data->channel_count && i < 8; i++) {
        // 将通道值(0-1000)转换为占空比百分比
        float duty_cycle = (rc_data->channel_values[i] / 1000.0f) * 100.0f;
        ESP_LOGI(TAG, "  通道%d: 值=%d, 占空比=%.1f%%", 
                 i + 1, rc_data->channel_values[i], duty_cycle);
    }
}

/**
 * @brief 传统的TCP任务函数（保持向后兼容）
 */
void tcp_task(void) {
    ESP_LOGW(TAG, "使用传统TCP任务函数，建议使用事件驱动的TCP任务管理器");
    
    // 使用指定的服务器配置
    const char *server_ip = "192.168.76.229";
    
    // 初始化心跳模块（使用默认端口7878）
    if (!tcp_client_hb_init(server_ip, 0)) {
        ESP_LOGE(TAG, "心跳模块初始化失败");
        return;
    }
    
    // 初始化遥测模块（使用默认端口6667）
    if (!tcp_client_telemetry_init(server_ip, 0)) {
        ESP_LOGE(TAG, "遥测模块初始化失败");
        return;
    }
    
    ESP_LOGI(TAG, "TCP模块初始化完成，服务器IP: %s", server_ip);
    
    // 启动心跳模块
    if (!tcp_client_hb_start("tcp_hb_task", 4096, 5)) {
        ESP_LOGE(TAG, "心跳模块启动失败");
        return;
    }
    
    // 启动遥测模块
    if (!tcp_client_telemetry_start("tcp_telemetry_task", 4096, 5)) {
        ESP_LOGE(TAG, "遥测模块启动失败");
        return;
    }
    
    ESP_LOGI(TAG, "TCP心跳和遥测模块启动成功");

    uint32_t status_print_counter = 0;
    uint32_t telemetry_send_counter = 0;

    while (1) {
        // 每30秒打印一次状态信息
        if (++status_print_counter >= 300) { // 30秒 / 100ms = 300次
            tcp_client_hb_print_status();
            tcp_client_telemetry_print_status();
            status_print_counter = 0;
        }
        
        // 每5秒发送一次遥测数据
        if (++telemetry_send_counter >= 50) { // 5秒 / 100ms = 50次
            // 更新模拟遥测数据
            // tcp_client_telemetry_update_sim_data();
            telemetry_send_counter = 0;
        }
        
        // 检查心跳连接健康状态
        if (!tcp_client_hb_is_connection_healthy()) {
            ESP_LOGW(TAG, "心跳连接异常");
        }
        
        // 检查遥测连接健康状态
        if (!tcp_client_telemetry_is_connection_healthy()) {
            ESP_LOGW(TAG, "遥测连接异常");
        }
        
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
