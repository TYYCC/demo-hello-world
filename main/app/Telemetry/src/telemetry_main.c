#include "telemetry_main.h"
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "telemetry_data_converter.h"
#include "telemetry_receiver.h"
#include "telemetry_sender.h"
#include <stdlib.h>
#include <string.h>


static const char* TAG = "telemetry_main";

// 全局变量
static telemetry_status_t service_status = TELEMETRY_STATUS_STOPPED;
static TaskHandle_t telemetry_task_handle = NULL;
static TaskHandle_t server_task_handle = NULL;
static telemetry_data_callback_t data_callback = NULL;
static telemetry_data_t current_data = {0};
static SemaphoreHandle_t data_mutex = NULL;
static QueueHandle_t control_queue = NULL;

// 内部函数声明
static void telemetry_server_task(void* pvParameters);
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

    // 初始化接收器和发送器
    if (telemetry_receiver_init() != 0) {
        ESP_LOGE(TAG, "Failed to initialize receiver");
        vSemaphoreDelete(data_mutex);
        vQueueDelete(control_queue);
        return -1;
    }

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

    // 启动接收器
    if (telemetry_receiver_start() != 0) {
        ESP_LOGE(TAG, "Failed to start receiver");
        service_status = TELEMETRY_STATUS_ERROR;
        return -1;
    }

    // 启动服务器任务
    if (xTaskCreate(telemetry_server_task, "telemetry_server", 4096, NULL, 5, &server_task_handle) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create server task");
        telemetry_receiver_stop();
        service_status = TELEMETRY_STATUS_ERROR;
        return -1;
    }

    // 启动数据处理任务
    if (xTaskCreate(telemetry_data_task, "telemetry_data", 4096, NULL, 4, &telemetry_task_handle) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create data task");
        if (server_task_handle) {
            vTaskDelete(server_task_handle);
            server_task_handle = NULL;
        }
        telemetry_receiver_stop();
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

    // 停止接收器和发送器
    telemetry_receiver_stop();
    telemetry_sender_deactivate();

    // 等待任务自然退出
    int wait_count = 0;
    while ((server_task_handle != NULL || telemetry_task_handle != NULL) && wait_count < 50) {
        vTaskDelay(pdMS_TO_TICKS(100));
        wait_count++;
    }

    // 如果任务仍然存在，强制删除
    if (server_task_handle != NULL && eTaskGetState(server_task_handle) != eDeleted) {
        vTaskDelete(server_task_handle);
        server_task_handle = NULL;
    }

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
 * @brief 服务器任务
 *
 * @param pvParameters 参数
 */
static void telemetry_server_task(void* pvParameters) {
    ESP_LOGI(TAG, "Server task started");

    while (service_status == TELEMETRY_STATUS_RUNNING) {
        // 这个函数会阻塞，直到一个客户端完成连接和断开的整个过程
        telemetry_receiver_accept_connections();
    }

    ESP_LOGI(TAG, "Server task ended");
    server_task_handle = NULL;
    vTaskDelete(NULL);
}

/**
 * @brief 数据任务
 *
 * @param pvParameters 参数
 */
static void telemetry_data_task(void* pvParameters) {
    control_command_t cmd;

    ESP_LOGI(TAG, "Data task started");

    while (service_status == TELEMETRY_STATUS_RUNNING) {
        // 0. 更新传感器数据
        if (telemetry_data_converter_update() != ESP_OK) {
            ESP_LOGW(TAG, "Failed to update sensor data");
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