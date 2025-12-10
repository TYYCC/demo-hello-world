// ESP-IDF 核心头文件
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// 项目本地头文件
#include "../app/inc/auto_pairing.h"
#include "background_manager.h"
#include "joystick_adc.h"
#include "lsm6ds_control.h"
#include "lvgl_main.h"
#include "serial_display.h"
#include "task_init.h"
#include "wifi_manager.h"
#include "key.h"
#include "ui.h"
#include "telemetry_main.h"

// 声明音频接收函数
extern esp_err_t audio_receiver_start(void);
extern void audio_receiver_stop(void);

static const char* TAG = "TASK_INIT";

// 任务句柄存储
static TaskHandle_t s_lvgl_task_handle = NULL;
static TaskHandle_t s_monitor_task_handle = NULL;
static TaskHandle_t s_joystick_task_handle = NULL;
static TaskHandle_t s_wifi_task_handle = NULL;
static TaskHandle_t s_serial_display_task_handle = NULL;

// 摇杆ADC采样任务（200Hz）
static void joystick_adc_task(void* pvParameters) {
    ESP_LOGI(TAG, "Joystick ADC Task started on core %d", xPortGetCoreID());

    if (joystick_adc_init() != ESP_OK) {
        ESP_LOGE(TAG, "Joystick ADC init failed");
        vTaskDelete(NULL);
        return;
    }

    if (key_init() != ESP_OK) {
        ESP_LOGE(TAG, "key init failed");
        vTaskDelete(NULL);
        return;
    }

    const TickType_t period_ticks = pdMS_TO_TICKS(20); // 50Hz = 20ms，降低频率避免看门狗超时

    joystick_data_t data;

    while (1) {
        key_scan();
        joystick_adc_read(&data);
       
           // 将摇杆数据（-100~100）映射到UI范围（0~1000）
           // Y轴作为油门：-100 -> 0, 0 -> 500, 100 -> 1000
           // X轴作为方向：-100 -> 0, 0 -> 500, 100 -> 1000
           int32_t throttle = (data.norm_joy1_y + 100) * 500 / 100;
           int32_t direction = (data.norm_joy1_x + 100) * 500 / 100;
       
           // 通过遥测服务更新摇杆数据（会更新UI和发送ELRS）
           telemetry_service_update_joystick(throttle, direction);
       
           vTaskDelay(period_ticks);
    }
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

// 任务初始化函数实现
esp_err_t init_lvgl_task(void) {
    if (s_lvgl_task_handle != NULL) {
        ESP_LOGW(TAG, "LVGL task already running");
        return ESP_OK;
    }

    BaseType_t result = xTaskCreatePinnedToCore(lvgl_main_task,      // 任务函数
                                                "LVGL_Main",         // 任务名称
                                                TASK_STACK_WIFI,    // 堆栈大小 (12KB)
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

esp_err_t init_telemetry_service(void) {
    ESP_LOGI(TAG, "Initializing telemetry service...");

    // 初始化遥测服务
    if (telemetry_service_init() != 0) {
        ESP_LOGE(TAG, "Failed to initialize telemetry service");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Telemetry service initialized successfully");
    return ESP_OK;
}

esp_err_t init_all_tasks(void) {
    ESP_LOGI(TAG, "Initializing all tasks...");

    esp_err_t ret;
    ui_start_animation_update_state(UI_STAGE_STARTING_SERVICES);
    ui_start_animation_set_progress((float)4 / UI_STAGE_DONE * 100);
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
    ui_start_animation_update_state(UI_STAGE_ALMOST_READY);
    ui_start_animation_set_progress((float)5 / UI_STAGE_DONE * 100);                                                                                                 

    // 初始化遥测服务
    ret = init_telemetry_service();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init telemetry service");
        return ret;
    }

    // 初始化串口显示任务（后台服务）
    ret = init_serial_display_task();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init serial display task");
        return ret;
    }

    ui_start_animation_update_state(UI_STAGE_FINALIZING);
    ui_start_animation_set_progress((float)6 / UI_STAGE_DONE * 100);
    vTaskDelay(pdMS_TO_TICKS(200)); // 确保动画有时间更新
    // 启动动画完成，切换到主界面
    ui_start_animation_update_state(UI_STAGE_DONE);
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

    if (s_wifi_task_handle) {
        vTaskDelete(s_wifi_task_handle);
        s_wifi_task_handle = NULL;
        ESP_LOGI(TAG, "WiFi manager task stopped");
    }

    if (s_serial_display_task_handle) {
        serial_display_stop(); // 先停止串口显示服务
        vTaskDelete(s_serial_display_task_handle);
        s_serial_display_task_handle = NULL;
        ESP_LOGI(TAG, "Serial display task stopped");
    }

    if (s_lsm6ds3_control_task != NULL) {
        vTaskDelete(s_lsm6ds3_control_task);
        s_lsm6ds3_control_task = NULL;
        ESP_LOGI(TAG, "LSM6DS3 control task stopped");
    }

    ESP_LOGI(TAG, "All tasks stopped");
    return ESP_OK;
}

void list_running_tasks(void) {
    ESP_LOGI(TAG, "=== Running Tasks ===");
    ESP_LOGI(TAG, "LVGL Task: %s", s_lvgl_task_handle ? "Running" : "Stopped");
    ESP_LOGI(TAG, "Monitor Task: %s", s_monitor_task_handle ? "Running" : "Stopped");
    ESP_LOGI(TAG, "Joystick Task: %s", s_joystick_task_handle ? "Running" : "Stopped");
    ESP_LOGI(TAG, "WiFi Task: %s", s_wifi_task_handle ? "Running" : "Stopped");
    ESP_LOGI(TAG, "Serial Display Task: %s", s_serial_display_task_handle ? "Running" : "Stopped");
    ESP_LOGI(TAG, "==================");
}

// 任务句柄获取函数
TaskHandle_t get_lvgl_task_handle(void) { return s_lvgl_task_handle; }
TaskHandle_t get_monitor_task_handle(void) { return s_monitor_task_handle; }
TaskHandle_t get_joystick_task_handle(void) { return s_joystick_task_handle; }
TaskHandle_t get_wifi_task_handle(void) { return s_wifi_task_handle; }
TaskHandle_t get_serial_display_task_handle(void) { return s_serial_display_task_handle; }