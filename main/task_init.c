// ESP-IDF 核心头文件
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// 项目本地头文件
#include "../app/Telemetry/inc/tcp_server_hb.h"
#include "background_manager.h"
#include "joystick_adc.h"
#include "lsm6ds_control.h"
#include "lvgl_main.h"
#include "power_management.h"
#include "serial_display.h"
#include "task_init.h"
#include "wifi_manager.h"

// 声明音频接收函数
extern esp_err_t audio_receiver_start(void);
extern void audio_receiver_stop(void);

// 声明TCP心跳服务器任务函数
static void tcp_hb_server_task(void* pvParameters);

static const char* TAG = "TASK_INIT";

// 任务句柄存储
static TaskHandle_t s_lvgl_task_handle = NULL;
static TaskHandle_t s_power_task_handle = NULL;
static TaskHandle_t s_monitor_task_handle = NULL;
static TaskHandle_t s_battery_task_handle = NULL;
static TaskHandle_t s_joystick_task_handle = NULL;
static TaskHandle_t s_wifi_task_handle = NULL;
static TaskHandle_t s_audio_receiver_task_handle = NULL;
static TaskHandle_t s_serial_display_task_handle = NULL;
static TaskHandle_t s_tcp_hb_server_task_handle = NULL;

// 摇杆ADC采样任务（200Hz）
static void joystick_adc_task(void* pvParameters) {
    ESP_LOGI(TAG, "Joystick ADC Task started on core %d", xPortGetCoreID());

    if (joystick_adc_init() != ESP_OK) {
        ESP_LOGE(TAG, "Joystick ADC init failed");
        vTaskDelete(NULL);
        return;
    }

    const TickType_t period_ticks = pdMS_TO_TICKS(20); // 50Hz = 20ms，降低频率避免看门狗超时

    joystick_data_t data;

    while (1) {
        joystick_adc_read(&data);
        vTaskDelay(period_ticks);
    }
}

// 电源管理任务包装
static void power_management_task(void* pvParameters) {
    ESP_LOGI(TAG, "Power Management Task started on core %d", xPortGetCoreID());
    power_management_demo();
    vTaskDelete(NULL);
}

// 系统监控任务
static void system_monitor_task(void* pvParameters) {
    ESP_LOGI(TAG, "System Monitor Task started on core %d", xPortGetCoreID());

    while (1) {
        // 系统状态监控
        ESP_LOGI(TAG, "=== System Status ===");
        ESP_LOGI(TAG, "Free heap: %lu bytes", (unsigned long)esp_get_free_heap_size());
        ESP_LOGI(TAG, "Min free heap: %lu bytes", (unsigned long)esp_get_minimum_free_heap_size());
        ESP_LOGI(TAG, "Stack high water mark: %lu bytes",
                 (unsigned long)uxTaskGetStackHighWaterMark(NULL));

        // 任务状态检查
        if (s_lvgl_task_handle) {
            ESP_LOGI(TAG, "LVGL task: Running");
        }
        if (s_power_task_handle) {
            ESP_LOGI(TAG, "Power task: Running");
        }
        if (s_battery_task_handle) {
            ESP_LOGI(TAG, "Battery task: Running");
        }

        ESP_LOGI(TAG, "==================");

        vTaskDelay(pdMS_TO_TICKS(10000)); // 10秒监控一次
    }
}

static void wifi_manager_task(void* pvParameters) {
    ESP_LOGI(TAG, "WiFi Manager Task started on core %d", xPortGetCoreID());

    esp_err_t ret = wifi_manager_init(NULL);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "WiFi manager initialized");
        ret = wifi_manager_start();
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "WiFi start failed: %s", esp_err_to_name(ret));
        }
    } else {
        ESP_LOGW(TAG, "WiFi init failed: %s", esp_err_to_name(ret));
    }

    // Task can terminate after starting WiFi connection process
    vTaskDelete(NULL);
}

// 电池监测任务（现在由后台管理模块处理，此任务主要用于日志记录）
static void battery_monitor_task(void* pvParameters) {
    ESP_LOGI(TAG, "Battery Monitor Task started on core %d", xPortGetCoreID());

    // 等待系统初始化完成
    vTaskDelay(pdMS_TO_TICKS(5000)); // 增加等待时间，确保UI完全初始化

    while (1) {
        // 获取后台电池信息用于日志记录
        background_battery_info_t battery_info;
        esp_err_t ret = background_manager_get_battery(&battery_info);

        if (ret == ESP_OK && battery_info.is_valid) {
            ESP_LOGI(TAG, "Battery: %dmV, %d%%, Low: %d, Critical: %d", battery_info.voltage_mv,
                     battery_info.percentage, battery_info.is_low_battery,
                     battery_info.is_critical);

            // 检查低电量警告
            if (battery_info.is_critical) {
                ESP_LOGW(TAG, "CRITICAL BATTERY LEVEL: %d%%", battery_info.percentage);
            } else if (battery_info.is_low_battery) {
                ESP_LOGW(TAG, "LOW BATTERY LEVEL: %d%%", battery_info.percentage);
            }
        } else {
            ESP_LOGW(TAG, "Failed to get battery info from background manager");
        }

        // 每10秒记录一次日志
        vTaskDelay(pdMS_TO_TICKS(10000)); // 10秒
    }
}

// 任务初始化函数实现
esp_err_t init_lvgl_task(void) {
    if (s_lvgl_task_handle != NULL) {
        ESP_LOGW(TAG, "LVGL task already running");
        return ESP_OK;
    }

    BaseType_t result = xTaskCreatePinnedToCore(lvgl_main_task,      // 任务函数
                                                "LVGL_Main",         // 任务名称
                                                TASK_STACK_LARGE,    // 堆栈大小 (8KB)
                                                NULL,                // 参数
                                                TASK_PRIORITY_HIGH,  // 高优先级
                                                &s_lvgl_task_handle, // 任务句柄
                                                1                    // 绑定到Core 1 (用户核心)
    );

    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create LVGL task");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "LVGL task created successfully on Core 1");
    return ESP_OK;
}

esp_err_t init_joystick_adc_task(void) {
    if (s_joystick_task_handle != NULL) {
        ESP_LOGW(TAG, "Joystick ADC task already running");
        return ESP_OK;
    }

    BaseType_t result = xTaskCreatePinnedToCore(joystick_adc_task,    // 任务函数
                                                "Joystick_ADC",       // 任务名称
                                                TASK_STACK_MEDIUM,    // 堆栈大小 (4KB，避免栈溢出)
                                                NULL,                 // 参数
                                                TASK_PRIORITY_NORMAL, // 普通优先级
                                                &s_joystick_task_handle, // 任务句柄
                                                0);                      // 绑定到Core 0

    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create Joystick ADC task");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Joystick ADC task created successfully on Core 0");
    return ESP_OK;
}

esp_err_t init_power_management_task(void) {
    if (s_power_task_handle != NULL) {
        ESP_LOGW(TAG, "Power management task already running");
        return ESP_OK;
    }

    BaseType_t result = xTaskCreatePinnedToCore(power_management_task, // 任务函数
                                                "Power_Mgmt",          // 任务名称
                                                TASK_STACK_MEDIUM,     // 堆栈大小 (4KB)
                                                NULL,                  // 参数
                                                TASK_PRIORITY_LOW,     // 低优先级
                                                &s_power_task_handle,  // 任务句柄
                                                0                      // 绑定到Core 0 (系统核心)
    );

    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create power management task");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Power management task created successfully on Core 0");
    return ESP_OK;
}

esp_err_t init_system_monitor_task(void) {
    if (s_monitor_task_handle != NULL) {
        ESP_LOGW(TAG, "System monitor task already running");
        return ESP_OK;
    }

    BaseType_t result = xTaskCreatePinnedToCore(system_monitor_task,    // 任务函数
                                                "Sys_Monitor",          // 任务名称
                                                TASK_STACK_SMALL,       // 堆栈大小 (2KB)
                                                NULL,                   // 参数
                                                TASK_PRIORITY_LOW,      // 低优先级
                                                &s_monitor_task_handle, // 任务句柄
                                                0                       // 绑定到Core 0
    );

    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create system monitor task");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "System monitor task created successfully on Core 0");
    return ESP_OK;
}

esp_err_t init_wifi_manager_task(void) {
    if (s_wifi_task_handle != NULL) {
        ESP_LOGW(TAG, "WiFi manager task already running");
        return ESP_OK;
    }

    BaseType_t result = xTaskCreatePinnedToCore(wifi_manager_task,    // 任务函数
                                                "WiFi_Manager",       // 任务名称
                                                TASK_STACK_MEDIUM,    // 堆栈大小 (4KB)
                                                NULL,                 // 参数
                                                TASK_PRIORITY_NORMAL, // 普通优先级
                                                &s_wifi_task_handle,  // 任务句柄
                                                0                     // 绑定到Core 0
    );

    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create WiFi manager task");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "WiFi manager task created successfully on Core 0");
    return ESP_OK;
}

esp_err_t init_battery_monitor_task(void) {
    if (s_battery_task_handle != NULL) {
        ESP_LOGW(TAG, "Battery monitor task already running");
        return ESP_OK;
    }

    BaseType_t result = xTaskCreatePinnedToCore(battery_monitor_task,   // 任务函数
                                                "Battery_Monitor",      // 任务名称
                                                TASK_STACK_MEDIUM,      // 堆栈大小 (4KB)
                                                NULL,                   // 参数
                                                TASK_PRIORITY_LOW,      // 低优先级
                                                &s_battery_task_handle, // 任务句柄
                                                0                       // 绑定到Core 0
    );

    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create battery monitor task");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Battery monitor task created successfully on Core 0");
    return ESP_OK;
}

// 音频接收任务包装
static void audio_receiver_task(void* pvParameters) {
    ESP_LOGI(TAG, "Audio Receiver Task started on core %d", xPortGetCoreID());

    // 等待WiFi连接
    vTaskDelay(pdMS_TO_TICKS(5000));

    esp_err_t ret = audio_receiver_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start audio receiver: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Audio receiver started successfully on TCP port 7557");
    }

    // 任务保持运行，监控音频接收状态
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(30000)); // 30秒检查一次
        ESP_LOGI(TAG, "Audio receiver running normally");
    }
}

// 串口显示任务包装
static void serial_display_task(void* pvParameters) {
    ESP_LOGI(TAG, "Serial Display Task started on core %d", xPortGetCoreID());

    // 等待WiFi连接
    vTaskDelay(pdMS_TO_TICKS(5000));

    esp_err_t ret = serial_display_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init serial display: %s", esp_err_to_name(ret));
        vTaskDelete(NULL);
        return;
    }

    if (!serial_display_start(8080)) {
        ESP_LOGE(TAG, "Failed to start serial display server on port 8080");
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Serial display server started successfully on TCP port 8080");

    // 任务保持运行，监控串口显示状态
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(30000)); // 30秒检查一次
        if (serial_display_is_running()) {
            ESP_LOGI(TAG, "Serial display server running normally");
        } else {
            ESP_LOGW(TAG, "Serial display server stopped, attempting restart");
            serial_display_start(8080);
        }
    }
}

esp_err_t init_audio_receiver_task(void) {
    if (s_audio_receiver_task_handle != NULL) {
        ESP_LOGW(TAG, "Audio receiver task already running");
        return ESP_OK;
    }

    BaseType_t result = xTaskCreatePinnedToCore(audio_receiver_task,           // 任务函数
                                                "Audio_Receiver",              // 任务名称
                                                TASK_STACK_LARGE,              // 堆栈大小 (8KB)
                                                NULL,                          // 参数
                                                TASK_PRIORITY_NORMAL,          // 普通优先级
                                                &s_audio_receiver_task_handle, // 任务句柄
                                                1                              // 绑定到Core 1
    );

    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create audio receiver task");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Audio receiver task created successfully on Core 1");
    return ESP_OK;
}

esp_err_t init_serial_display_task(void) {
    if (s_serial_display_task_handle != NULL) {
        ESP_LOGW(TAG, "Serial display task already running");
        return ESP_OK;
    }

    BaseType_t result = xTaskCreatePinnedToCore(serial_display_task,           // 任务函数
                                                "Serial_Display",              // 任务名称
                                                TASK_STACK_MEDIUM,             // 堆栈大小 (4KB)
                                                NULL,                          // 参数
                                                TASK_PRIORITY_NORMAL,          // 普通优先级
                                                &s_serial_display_task_handle, // 任务句柄
                                                0                              // 绑定到Core 0
    );

    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create serial display task");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Serial display task created successfully on Core 0");
    return ESP_OK;
}

esp_err_t init_tcp_hb_server_task(void) {
    if (s_tcp_hb_server_task_handle != NULL) {
        ESP_LOGW(TAG, "TCP heartbeat server task already running");
        return ESP_OK;
    }

    BaseType_t result = xTaskCreatePinnedToCore(tcp_hb_server_task,           // 任务函数
                                                "TCP_HB_Server",              // 任务名称
                                                TASK_STACK_MEDIUM,            // 堆栈大小 (4KB)
                                                NULL,                         // 参数
                                                TASK_PRIORITY_LOW,            // 低优先级
                                                &s_tcp_hb_server_task_handle, // 任务句柄
                                                0                             // 绑定到Core 0
    );

    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create TCP heartbeat server task");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "TCP heartbeat server task created successfully on Core 0");
    return ESP_OK;
}

esp_err_t init_all_tasks(void) {
    ESP_LOGI(TAG, "Initializing all tasks...");

    esp_err_t ret;

    // 初始化LVGL任务
    ret = init_lvgl_task();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init LVGL task");
        return ret;
    }

    // 初始化后台管理模块
    ret = background_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init background manager");
        return ret;
    }

    // 启动后台管理任务
    ret = background_manager_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start background manager task");
        return ret;
    }

    // 初始化电池监测任务
    ret = init_battery_monitor_task();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init battery monitor task");
        return ret;
    }

    // 初始化WiFi管理任务
    ret = init_wifi_manager_task();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init WiFi manager task");
        return ret;
    }

    // 初始化摇杆ADC采样任务（200Hz）
    ret = init_joystick_adc_task();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init Joystick ADC task");
        return ret;
    }

    ret = init_lsm6ds3_control_task();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init LSM6DS3 control task");
        return ret;
    }

    // 初始化音频接收任务（后台服务）
    ret = init_audio_receiver_task();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init audio receiver task");
        return ret;
    }

    // 初始化串口显示任务（后台服务）
    ret = init_serial_display_task();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init serial display task");
        return ret;
    }

    // 初始化TCP心跳服务器任务（后台服务）
    ret = init_tcp_hb_server_task();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init TCP heartbeat server task");
        return ret;
    }

    ESP_LOGI(TAG, "All tasks initialized successfully");
    return ESP_OK;
}

esp_err_t stop_all_tasks(void) {
    ESP_LOGI(TAG, "Stopping all tasks...");

    // 停止后台管理任务
    background_manager_stop();
    background_manager_deinit();
    ESP_LOGI(TAG, "Background manager stopped");

    if (s_lvgl_task_handle) {
        vTaskDelete(s_lvgl_task_handle);
        s_lvgl_task_handle = NULL;
        ESP_LOGI(TAG, "LVGL task stopped");
    }

    if (s_power_task_handle) {
        vTaskDelete(s_power_task_handle);
        s_power_task_handle = NULL;
        ESP_LOGI(TAG, "Power management task stopped");
    }

    if (s_monitor_task_handle) {
        vTaskDelete(s_monitor_task_handle);
        s_monitor_task_handle = NULL;
        ESP_LOGI(TAG, "System monitor task stopped");
    }

    if (s_joystick_task_handle) {
        vTaskDelete(s_joystick_task_handle);
        s_joystick_task_handle = NULL;
        ESP_LOGI(TAG, "Joystick ADC task stopped");
    }

    if (s_battery_task_handle) {
        vTaskDelete(s_battery_task_handle);
        s_battery_task_handle = NULL;
        ESP_LOGI(TAG, "Battery monitor task stopped");
    }

    if (s_wifi_task_handle) {
        vTaskDelete(s_wifi_task_handle);
        s_wifi_task_handle = NULL;
        ESP_LOGI(TAG, "WiFi manager task stopped");
    }

    if (s_audio_receiver_task_handle) {
        audio_receiver_stop(); // 先停止音频接收服务
        vTaskDelete(s_audio_receiver_task_handle);
        s_audio_receiver_task_handle = NULL;
        ESP_LOGI(TAG, "Audio receiver task stopped");
    }

    if (s_serial_display_task_handle) {
        serial_display_stop(); // 先停止串口显示服务
        vTaskDelete(s_serial_display_task_handle);
        s_serial_display_task_handle = NULL;
        ESP_LOGI(TAG, "Serial display task stopped");
    }

    if (s_tcp_hb_server_task_handle) {
        tcp_server_hb_stop(); // 先停止TCP心跳服务器
        vTaskDelete(s_tcp_hb_server_task_handle);
        s_tcp_hb_server_task_handle = NULL;
        ESP_LOGI(TAG, "TCP heartbeat server task stopped");
    }

    if (s_lsm6ds3_control_task != NULL) {
        vTaskDelete(s_lsm6ds3_control_task);
        s_lsm6ds3_control_task = NULL;
        ESP_LOGI(TAG, "LSM6DS3 control task stopped");
    }

    ESP_LOGI(TAG, "All tasks stopped");
    return ESP_OK;
}

// TCP心跳服务器回调函数
static void tcp_hb_heartbeat_callback(uint32_t client_index,
                                      const tcp_server_hb_payload_t* payload) {
    ESP_LOGI(TAG, "TCP心跳包接收: 客户端=%d, 状态=%d, 时间戳=%u", client_index,
             payload->device_status, payload->timestamp);

    // 每次收到心跳包都更新状态栏显示连接状态
    extern esp_err_t status_bar_manager_set_tcp_client_status(bool has_client_connected);
    status_bar_manager_set_tcp_client_status(true);
}

static void tcp_hb_connection_callback(uint32_t client_index, bool connected) {
    extern esp_err_t status_bar_manager_set_tcp_client_status(bool has_client_connected);

    if (connected) {
        ESP_LOGI(TAG, "TCP客户端 %d 已连接", client_index);
        // 有客户端连接时显示连接图标
        status_bar_manager_set_tcp_client_status(true);
    } else {
        ESP_LOGI(TAG, "TCP客户端 %d 已断开", client_index);

        // 检查是否还有其他客户端连接
        uint32_t active_count = tcp_server_hb_get_active_client_count();
        if (active_count > 0) {
            // 还有其他客户端连接，保持连接状态
            status_bar_manager_set_tcp_client_status(true);
        } else {
            // 没有客户端连接了，显示断开状态
            status_bar_manager_set_tcp_client_status(false);
        }
    }
}

// TCP心跳服务器任务包装
static void tcp_hb_server_task(void* pvParameters) {
    ESP_LOGI(TAG, "TCP Heartbeat Server Task started on core %d", xPortGetCoreID());

    // 初始化TCP心跳服务器
    if (!tcp_server_hb_init(NULL)) {
        ESP_LOGE(TAG, "Failed to initialize TCP heartbeat server");
        vTaskDelete(NULL);
        return;
    }

    // 启动TCP心跳服务器
    if (!tcp_server_hb_start("tcp_hb_server", 4096, TASK_PRIORITY_LOW, tcp_hb_heartbeat_callback,
                             tcp_hb_connection_callback)) {
        ESP_LOGE(TAG, "Failed to start TCP heartbeat server");
        tcp_server_hb_destroy();
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "TCP heartbeat server started successfully on port %d",
             TCP_SERVER_HB_DEFAULT_PORT);

    // 任务保持运行，监控服务器状态
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(30000)); // 30秒检查一次

        // 检查服务器状态
        tcp_server_hb_state_t state = tcp_server_hb_get_state();
        if (state == TCP_SERVER_HB_STATE_RUNNING) {
            const tcp_server_hb_stats_t* stats = tcp_server_hb_get_stats();
            ESP_LOGI(TAG, "TCP HB Server: 活跃客户端=%d, 心跳包=%d", stats->active_clients,
                     stats->heartbeat_received_count);
        } else {
            ESP_LOGW(TAG, "TCP heartbeat server state: %d", state);
        }
    }
}

void list_running_tasks(void) {
    ESP_LOGI(TAG, "=== Running Tasks ===");
    ESP_LOGI(TAG, "LVGL Task: %s", s_lvgl_task_handle ? "Running" : "Stopped");
    ESP_LOGI(TAG, "Power Task: %s", s_power_task_handle ? "Running" : "Stopped");
    ESP_LOGI(TAG, "Monitor Task: %s", s_monitor_task_handle ? "Running" : "Stopped");
    ESP_LOGI(TAG, "Joystick Task: %s", s_joystick_task_handle ? "Running" : "Stopped");
    ESP_LOGI(TAG, "Battery Task: %s", s_battery_task_handle ? "Running" : "Stopped");
    ESP_LOGI(TAG, "WiFi Task: %s", s_wifi_task_handle ? "Running" : "Stopped");
    ESP_LOGI(TAG, "Audio Receiver Task: %s", s_audio_receiver_task_handle ? "Running" : "Stopped");
    ESP_LOGI(TAG, "Serial Display Task: %s", s_serial_display_task_handle ? "Running" : "Stopped");
    ESP_LOGI(TAG, "TCP HB Server Task: %s", s_tcp_hb_server_task_handle ? "Running" : "Stopped");
    ESP_LOGI(TAG, "==================");
}

// 任务句柄获取函数
TaskHandle_t get_lvgl_task_handle(void) { return s_lvgl_task_handle; }
TaskHandle_t get_power_task_handle(void) { return s_power_task_handle; }
TaskHandle_t get_monitor_task_handle(void) { return s_monitor_task_handle; }
TaskHandle_t get_battery_task_handle(void) { return s_battery_task_handle; }
TaskHandle_t get_joystick_task_handle(void) { return s_joystick_task_handle; }
TaskHandle_t get_wifi_task_handle(void) { return s_wifi_task_handle; }
TaskHandle_t get_audio_receiver_task_handle(void) { return s_audio_receiver_task_handle; }
TaskHandle_t get_serial_display_task_handle(void) { return s_serial_display_task_handle; }
TaskHandle_t get_tcp_hb_server_task_handle(void) { return s_tcp_hb_server_task_handle; }