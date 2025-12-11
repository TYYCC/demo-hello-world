#include "telemetry_main.h"
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lvgl.h"
#include <stdlib.h>
#include <string.h>
#include "common.h"
#include "ui_telemetry.h"
#include "telemetry_endpoint.h"
#include "CRSFRouter.h"

extern "C" void elrs_set_channel(uint8_t channel, uint16_t value);
extern "C" void elrs_set_channels(const uint16_t *values, uint8_t count);
extern "C" uint16_t elrs_get_channel(uint8_t channel);

static const char* TAG = "telemetry_main";

// 全局变量
static telemetry_status_t service_status = TELEMETRY_STATUS_STOPPED;
static TaskHandle_t telemetry_task_handle = NULL;
static telemetry_data_callback_t data_callback = NULL;
static telemetry_data_t current_data = {0};
static SemaphoreHandle_t data_mutex = NULL;
static TelemetryEndpoint* telemetry_endpoint = NULL;

// 内部函数声明
static void telemetry_data_task(void* pvParameters);

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

    // 创建并注册遥测端点
    if (telemetry_endpoint == NULL) {
        telemetry_endpoint = new TelemetryEndpoint();
        crsfRouter.addEndpoint(telemetry_endpoint);
        ESP_LOGI(TAG, "Telemetry endpoint registered");
    }

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
 * @brief 发送UI控制命令到ELRS
 * 
 * 将UI滑动条的控制值转换为CRSF通道值并通过ELRS API发送
 *
 * @param throttle 油门值 (0-1000)
 * @param direction 方向值 (0-1000)
 * @return 0 成功，-1 失败
 */
int telemetry_service_send_control(int32_t throttle, int32_t direction) {
    if (service_status != TELEMETRY_STATUS_RUNNING) {
        ESP_LOGW(TAG, "Service not running, cannot send control");
        return -1;
    }

    // 限制输入范围
    if (throttle < 0) throttle = 0;
    if (throttle > 1000) throttle = 1000;
    if (direction < 0) direction = 0;
    if (direction > 1000) direction = 1000;

    // 将UI值(0-1000)转换为CRSF值(172-1811)
    // 公式: crsf_value = 172 + (ui_value * 1639) / 1000
    uint16_t ch0 = 172 + (throttle * 1639) / 1000;   // CH0: 油门
    uint16_t ch1 = 172 + (direction * 1639) / 1000;  // CH1: 方向

    // 限制范围在172-1811
    if (ch0 < 172) ch0 = 172;
    if (ch0 > 1811) ch0 = 1811;
    if (ch1 < 172) ch1 = 172;
    if (ch1 > 1811) ch1 = 1811;

    elrs_set_channel(0, ch0);
    elrs_set_channel(1, ch1);

    ESP_LOGD(TAG, "UI Control sent: throttle=%d->%u, direction=%d->%u",
             throttle, ch0, direction, ch1);
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
        memcpy(&current_data, telemetry_data, sizeof(telemetry_data_t));
        if (data_callback) {
            lv_async_call((lv_async_cb_t)data_callback, &current_data);
        }

        xSemaphoreGive(data_mutex);
    } else {
        ESP_LOGW(TAG, "Failed to take data mutex to update telemetry");
    }
}

/**
 * @brief 更新摇杆数据（本地输入）
 *
 * @param throttle 油门值 (0-1000)
 * @param direction 方向值 (0-1000)
 */
void telemetry_service_update_joystick(int32_t throttle, int32_t direction) {
    if (service_status != TELEMETRY_STATUS_RUNNING) {
        return;
    }

    // 限制范围
    if (throttle < 0) throttle = 0;
    if (throttle > 1000) throttle = 1000;
    if (direction < 0) direction = 0;
    if (direction > 1000) direction = 1000;

    // 同步UI滑动条显示
    ui_telemetry_update_sliders(throttle, direction);

    // 发送到ELRS
    telemetry_service_send_control(throttle, direction);
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

    // 清理遥测端点
    if (telemetry_endpoint) {
        // crsfRouter.removeEndpoint(telemetry_endpoint); // No removeEndpoint method
        delete telemetry_endpoint;
        telemetry_endpoint = NULL;
        ESP_LOGI(TAG, "Telemetry endpoint removed");
    }

    ESP_LOGI(TAG, "Telemetry service deinitialized");
}

/**
 * @brief 数据处理任务
 * @param pvParameters 参数
 */
static void telemetry_data_task(void* pvParameters) {

    ESP_LOGI(TAG, "Data task started");

    while (service_status == TELEMETRY_STATUS_RUNNING) {
        // telemetry_service_update_data(&current_data);
        vTaskDelay(pdMS_TO_TICKS(20));// 任务循环频率: 50Hz (20ms)
    }
 
    ESP_LOGI(TAG, "Data task ended");
    telemetry_task_handle = NULL;
    vTaskDelete(NULL);
}