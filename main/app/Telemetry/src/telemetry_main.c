#include "telemetry_main.h"
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "telemetry_data_converter.h"
#include "telemetry_sender.h"
#include <stdlib.h>
#include <string.h>


static const char* TAG = "telemetry_main";

// 全局变量
static telemetry_status_t service_status = TELEMETRY_STATUS_STOPPED;
static TaskHandle_t telemetry_task_handle = NULL;
static telemetry_data_callback_t data_callback = NULL;
static telemetry_data_t current_data = {0};
static SemaphoreHandle_t data_mutex = NULL;
static QueueHandle_t control_queue = NULL;

// 内部函数声明
static void telemetry_data_task(void* pvParameters);

typedef struct {
    int32_t throttle;
    int32_t direction;
} control_command_t;

int telemetry_service_init(void) {
    if (service_status != TELEMETRY_STATUS_STOPPED) {
        ESP_LOGW(TAG, "Service already initialized");
        return 0;
    }

    // 创建互斥锁
    data_mutex = xSemaphoreCreateMutex();
    if (data_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create data mutex");
        return -1;
    }

    // 创建控制命令队列
    control_queue = xQueueCreate(10, sizeof(control_command_t));
    if (control_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create control queue");
        vSemaphoreDelete(data_mutex);
        return -1;
    }

    // 初始化发送器
    if (telemetry_sender_init() != 0) {
        ESP_LOGE(TAG, "Failed to initialize sender");
        vSemaphoreDelete(data_mutex);
        vQueueDelete(control_queue);
        return -1;
    }

    ESP_LOGI(TAG, "Telemetry service initialized");
    return 0;
}

/**
 * @brief 启动遥测服务
 *
 * @param callback 数据回调函数
 * @return 0 成功，-1 失败
 */
int telemetry_service_start(telemetry_data_callback_t callback) {
    if (service_status == TELEMETRY_STATUS_RUNNING) {
        ESP_LOGW(TAG, "Service already running");
        return 0;
    }

    if (service_status != TELEMETRY_STATUS_STOPPED) {
        ESP_LOGE(TAG, "Service in invalid state");
        return -1;
    }

    service_status = TELEMETRY_STATUS_STARTING;
    data_callback = callback;

    // 启动数据处理任务
    if (xTaskCreate(telemetry_data_task, "telemetry_data", 4096, NULL, 4, &telemetry_task_handle) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create data task");
        service_status = TELEMETRY_STATUS_ERROR;
        return -1;
    }

    service_status = TELEMETRY_STATUS_RUNNING;
    ESP_LOGI(TAG, "Telemetry service started");
    return 0;
}

/**
 * @brief 停止遥测服务
 *
 * @return 0 成功，-1 失败
 */
int telemetry_service_stop(void) {
    if (service_status == TELEMETRY_STATUS_STOPPED || service_status == TELEMETRY_STATUS_STOPPING) {
        ESP_LOGW(TAG, "Service already stopped or stopping");
        return 0;
    }

    service_status = TELEMETRY_STATUS_STOPPING;

    // 停止发送器
    telemetry_sender_deactivate();

    // 等待任务自然退出
    int wait_count = 0;
    while (telemetry_task_handle != NULL && wait_count < 50) {
        vTaskDelay(pdMS_TO_TICKS(100));
        wait_count++;
    }

    // 如果任务仍然存在，强制删除
    if (telemetry_task_handle != NULL && eTaskGetState(telemetry_task_handle) != eDeleted) {
        vTaskDelete(telemetry_task_handle);
        telemetry_task_handle = NULL;
    }

    // 清空队列
    if (control_queue) {
        xQueueReset(control_queue);
    }

    service_status = TELEMETRY_STATUS_STOPPED;
    data_callback = NULL;

    ESP_LOGI(TAG, "Telemetry service stopped");
    return 0;
}

/**
 * @brief 获取遥测服务状态
 *
 * @return 服务状态
 */
telemetry_status_t telemetry_service_get_status(void) { return service_status; }

/**
 * @brief 发送控制命令
 *
 * @param throttle 油门
 * @param direction 方向
 * @return 0 成功，-1 失败
 */
int telemetry_service_send_control(int32_t throttle, int32_t direction) {
    if (service_status != TELEMETRY_STATUS_RUNNING) {
        ESP_LOGW(TAG, "Service not running");
        return -1;
    }

    control_command_t cmd = {.throttle = throttle, .direction = direction};

    if (xQueueSend(control_queue, &cmd, pdMS_TO_TICKS(100)) != pdPASS) {
        ESP_LOGW(TAG, "Failed to send control command");
        return -1;
    }

    ESP_LOGI(TAG, "Control command sent: throttle=%d, direction=%d", throttle, direction);
    return 0;
}

/**
 * @brief 更新遥测数据
 *
 * @param telemetry_data 遥测数据
 */
void telemetry_service_update_data(const telemetry_data_t* telemetry_data) {
    if (telemetry_data == NULL || service_status != TELEMETRY_STATUS_RUNNING) {
        return;
    }

    if (xSemaphoreTake(data_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        // 直接复制新的ELRS格式遥测数据
        memcpy(&current_data, telemetry_data, sizeof(telemetry_data_t));

        // 调用回调函数更新UI
        if (data_callback) {
            // 在持有锁的情况下调用回调，以确保数据一致性
            data_callback(&current_data);
        }

        xSemaphoreGive(data_mutex);
    } else {
        ESP_LOGW(TAG, "Failed to take data mutex to update telemetry");
    }
}

/**
 * @brief 获取遥测数据
 *
 * @param data 遥测数据
 * @return 0 成功，-1 失败
 */
int telemetry_service_get_data(telemetry_data_t* data) {
    if (data == NULL) {
        return -1;
    }

    if (xSemaphoreTake(data_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        *data = current_data;
        xSemaphoreGive(data_mutex);
        return 0;
    }

    return -1;
}

/**
 * @brief 反初始化遥测服务
 */
void telemetry_service_deinit(void) {
    telemetry_service_stop();

    if (data_mutex) {
        vSemaphoreDelete(data_mutex);
        data_mutex = NULL;
    }

    if (control_queue) {
        vQueueDelete(control_queue);
        control_queue = NULL;
    }

    ESP_LOGI(TAG, "Telemetry service deinitialized");
}

/**
 * @brief 数据处理任务
 *
 * @param pvParameters 参数
 */
static void telemetry_data_task(void* pvParameters) {
    control_command_t cmd;
    uint32_t test_data_timer = 0;

    ESP_LOGI(TAG, "Data task started");

    while (service_status == TELEMETRY_STATUS_RUNNING) {
        // 0. 更新传感器数据
        if (telemetry_data_converter_update() != ESP_OK) {
            ESP_LOGW(TAG, "Failed to update sensor data");
        }

        // 0.5 定期注入测试ELRS数据 (每500ms) - 用于演示UI
        test_data_timer++;
        if (test_data_timer >= 25) {  // 25 * 20ms = 500ms
            telemetry_service_inject_test_data();
            test_data_timer = 0;
        }

        // 1. 处理来自UI的控制命令 (使用非阻塞接收)
        // 注：控制命令现在通过ELRS协议的RC通道传输
        if (xQueueReceive(control_queue, &cmd, 0) == pdPASS) {
            if (xSemaphoreTake(data_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                // 控制数据现在由ELRS RC通道处理
                // TODO: 将throttle/direction映射到RC通道数据
                xSemaphoreGive(data_mutex);
            }
        }

        // 2. 处理发送器逻辑 (发送心跳和遥控数据)
        telemetry_sender_process();

        // 将任务频率提高到50Hz，以获得更流畅的控制
        vTaskDelay(pdMS_TO_TICKS(20));
    }
 
    ESP_LOGI(TAG, "Data task ended");
    telemetry_task_handle = NULL;
    vTaskDelete(NULL);
}

/**
 * @brief 注入测试ELRS链路统计数据 (用于调试UI显示)
 * 这是一个临时函数，用于验证UI显示逻辑是否正确
 */
void telemetry_service_inject_test_data(void) {
    if (xSemaphoreTake(data_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }

    // 生成模拟的ELRS链路统计数据
    // RSSI: -80dBm (较好的信号)
    current_data.uplink_rssi_1 = (uint8_t)(-80 + 120);  // = 40
    current_data.uplink_rssi_2 = (uint8_t)(-85 + 120);  // = 35
    
    // 链路质量: 95%
    current_data.link_quality = 95;
    
    // 信噪比: 10dB
    current_data.snr = 10;
    
    // 天线信息
    current_data.antenna_select = 0;  // 使用天线1
    current_data.diversity_available = true;
    current_data.model_match = true;
    
    // 生成随机的RC通道数据 (模拟摇杆输入)
    // 通道0-1: 摇杆油门和方向
    current_data.channels[0] = 992 + esp_random() % 100 - 50;  // CH0: 油门，中位±50
    current_data.channels[1] = 992 + esp_random() % 100 - 50;  // CH1: 方向，中位±50
    
    // 其他通道设为中位
    for (int i = 2; i < 16; i++) {
        current_data.channels[i] = 992;
    }
    current_data.channels_valid = true;
    current_data.is_armed = true;
    
    // 扩展数据
    current_data.voltage = 12.0f + (esp_random() % 10) / 10.0f;  // 11.9-12.9V
    current_data.current = 5.0f + (esp_random() % 30) / 10.0f;   // 5.0-8.0A
    current_data.altitude = 100.0f + (esp_random() % 50);        // 100-150m
    
    xSemaphoreGive(data_mutex);
    
    // 触发UI更新回调
    if (data_callback) {
        data_callback(&current_data);
    }
    
    ESP_LOGI(TAG, "Test data injected: RSSI1=%d dBm, LQ=%d%%, SNR=%d dB",
             telemetry_rssi_raw_to_dbm(current_data.uplink_rssi_1),
             current_data.link_quality,
             current_data.snr);
}