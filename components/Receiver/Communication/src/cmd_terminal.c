#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tcp_common_protocol.h"

static const char* TAG = "cmd_terminal";

// 输出回调：上层可在其他源文件中提供强符号实现以覆写（例如通过USB CDC回传）
__attribute__((weak)) void cmd_terminal_write(const char* s) {
    if (s) {
        ESP_LOGI(TAG, "%s", s);
    }
}

// 运行时设置JPEG质量的钩子：jpeg模块可提供强符号实现，默认仅提示未生效
__attribute__((weak)) bool cmd_terminal_set_jpeg_quality(uint8_t quality) {
    ESP_LOGW(TAG, "未实现运行时修改JPEG质量，期望质量=%u (需要在jpeg模块中实现覆盖)", quality);
    return false;
}

// WiFi配置保存回调：WiFi模块可提供强符号实现以覆写默认行为
bool cmd_terminal_save_wifi_config(const char* ssid, const char* password) {
    if (!ssid || !password) {
        ESP_LOGW(TAG, "WiFi配置参数无效");
        return false;
    }
    
    ESP_LOGI(TAG, "默认WiFi配置保存：SSID=%s (需要WiFi模块提供强符号实现)", ssid);
    
    // 默认实现：尝试保存到NVS（使用与WiFi自动配对模块相同的命名空间）
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("wifi_pairing", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "无法打开NVS命名空间: %s", esp_err_to_name(err));
        return false;
    }
    
    // 保存SSID
    err = nvs_set_str(nvs_handle, "ssid", ssid);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "保存SSID失败: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return false;
    }
    
    // 保存密码
    err = nvs_set_str(nvs_handle, "password", password);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "保存密码失败: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return false;
    }
    
    // 保存有效标志（与WiFi自动配对模块兼容）
    err = nvs_set_u8(nvs_handle, "valid", 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "保存有效标志失败: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return false;
    }
    
    // 提交更改
    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "提交NVS更改失败: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return false;
    }
    
    nvs_close(nvs_handle);
    ESP_LOGI(TAG, "WiFi配置已保存到NVS");
    return true;
}

// 重启确认回调：上层可提供强符号实现以自定义重启行为
__attribute__((weak)) bool cmd_terminal_confirm_restart(void) {
    ESP_LOGI(TAG, "默认重启确认：将在3秒后重启...");
    vTaskDelay(pdMS_TO_TICKS(3000));
    return true;
}

static void respondf(const char* fmt, ...) {
    char buf[512];  // 增加缓冲区大小以支持更长的help输出
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    cmd_terminal_write(buf);
}

static void str_tolower(char* s) {
    if (!s) return;
    for (; *s; ++s) {
        *s = (char)tolower((unsigned char)*s);
    }
}

static void trim_trailing_newline(char* s) {
    if (!s) return;
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r')) {
        s[n - 1] = '\0';
        n--;
    }
}

// 获取任务详细信息
static void print_task_info(void) {
    UBaseType_t task_count = uxTaskGetNumberOfTasks();
    respondf("总任务数: %u", (unsigned)task_count);
    
#if (configUSE_TRACE_FACILITY == 1) && (configGENERATE_RUN_TIME_STATS == 1)
    // 分配内存用于任务状态数组
    TaskStatus_t* task_array = (TaskStatus_t*)malloc(task_count * sizeof(TaskStatus_t));
    if (!task_array) {
        respondf("内存不足，无法获取详细任务信息");
        return;
    }
    
    // 获取任务状态信息
    UBaseType_t actual_count = uxTaskGetSystemState(task_array, task_count, NULL);
    
    respondf("任务详情:");
    respondf("%-16s %-8s %-8s %-8s", "任务名", "状态", "优先级", "栈剩余");
    respondf("----------------------------------------");
    
    for (UBaseType_t i = 0; i < actual_count; i++) {
        const char* state_str;
        switch (task_array[i].eCurrentState) {
            case eRunning:   state_str = "运行中"; break;
            case eReady:     state_str = "就绪"; break;
            case eBlocked:   state_str = "阻塞"; break;
            case eSuspended: state_str = "挂起"; break;
            case eDeleted:   state_str = "已删除"; break;
            default:         state_str = "未知"; break;
        }
        
        respondf("%-16s %-8s %-8lu %-8u", 
                task_array[i].pcTaskName,
                state_str,
                (unsigned long)task_array[i].uxCurrentPriority,
                (unsigned)task_array[i].usStackHighWaterMark);
    }
    
    free(task_array);
#else
    respondf("详细任务信息功能未启用");
    respondf("需要在 FreeRTOS 配置中启用:");
    respondf("- configUSE_TRACE_FACILITY = 1");
    respondf("- configGENERATE_RUN_TIME_STATS = 1");
    respondf("当前仅显示任务总数: %u", (unsigned)task_count);
#endif
}

static void handle_text_command(char* line) {
    if (!line) return;

    trim_trailing_newline(line);

    // 不区分大小写比较准备副本
    char cmd_copy[128];
    strncpy(cmd_copy, line, sizeof(cmd_copy) - 1);
    cmd_copy[sizeof(cmd_copy) - 1] = '\0';
    str_tolower(cmd_copy);

    // 第一个token作为命令
    char* saveptr = NULL;
    char* cmd = strtok_r(cmd_copy, " \t", &saveptr);
    if (!cmd) return;

    if (strcmp(cmd, "help") == 0 || strcmp(cmd, "?") == 0) {
        respondf("可用命令:\n"
                 "  help                - 显示帮助\n"
                 "  heap                - 打印空闲堆内存\n"
                 "  tasks               - 打印任务数量\n"
                 "  taskinfo            - 显示详细任务信息\n"
                 "  version             - 打印IDF版本\n"
                 "  echo <text>         - 回显文本\n"
                 "  jpegq <0-100>       - 设置JPEG质量\n"
                 "  wifi <ssid> <pwd>   - 配置WiFi并保存到NVS\n"
                 "  wifir <ssid> <pwd>  - 配置WiFi并立即重启\n"
                 "  restart             - 软件重启\n"
                 "  reboot              - 软件重启(同restart)");
        return;
    }

    if (strcmp(cmd, "heap") == 0) {
        respondf("Free heap: %lu bytes", (unsigned long)esp_get_free_heap_size());
        return;
    }

    if (strcmp(cmd, "tasks") == 0) {
        respondf("Tasks: %u", (unsigned)uxTaskGetNumberOfTasks());
        return;
    }

    if (strcmp(cmd, "taskinfo") == 0) {
        print_task_info();
        return;
    }

    if (strcmp(cmd, "version") == 0) {
        respondf("IDF: %s", esp_get_idf_version());
        return;
    }

    if (strncmp(cmd, "echo", 4) == 0) {
        // 保留原始大小写与空格：在原始行中找第一个空格后的内容
        const char* p = strchr(line, ' ');
        if (p) {
            while (*p == ' ' || *p == '\t') p++;
            respondf("%s", p);
        } else {
            respondf("");
        }
        return;
    }

    if (strncmp(cmd, "jpegq", 5) == 0) {
        const char* p = strchr(line, ' ');
        if (!p) {
            respondf("用法: jpegq <0-100>");
            return;
        }
        while (*p == ' ' || *p == '\t') p++;
        int q = atoi(p);
        if (q < 0) q = 0;
        if (q > 100) q = 100;
        bool ok = cmd_terminal_set_jpeg_quality((uint8_t)q);
        respondf("设置JPEG质量=%d, %s", q, ok ? "成功" : "未生效(需模块支持)");
        return;
    }

    if (strcmp(cmd, "wifi") == 0 || strcmp(cmd, "wifir") == 0) {
        bool reboot_after = (strcmp(cmd, "wifir") == 0);
        
        // 解析SSID和密码参数 - 使用原始输入保持大小写
        char* original_saveptr = NULL;
        char* original_cmd = strtok_r(line, " \t", &original_saveptr);
        char* ssid = strtok_r(NULL, " \t", &original_saveptr);
        char* password = strtok_r(NULL, " \t", &original_saveptr);
        
        if (!ssid || !password) {
            respondf("用法: %s <ssid> <password>", cmd);
            return;
        }
        
        // 保存WiFi配置到NVS
        bool save_ok = cmd_terminal_save_wifi_config(ssid, password);
        if (!save_ok) {
            respondf("WiFi配置保存失败");
            return;
        }
        
        respondf("WiFi配置已保存: SSID=%s", ssid);
        
        if (reboot_after) {
            respondf("正在重启以应用新配置...");
            if (cmd_terminal_confirm_restart()) {
                esp_restart();
            }
        } else {
            respondf("是否立即重启以应用配置? (输入 restart 命令重启)");
        }
        return;
    }

    if (strcmp(cmd, "restart") == 0 || strcmp(cmd, "reboot") == 0) {
        respondf("准备重启系统...");
        if (cmd_terminal_confirm_restart()) {
            esp_restart();
        } else {
            respondf("重启已取消");
        }
        return;
    }

    respondf("未知命令: %s (输入 help 获取帮助)", line);
}

// 覆盖弱符号：解析来自USB/SPI的扩展命令
void handle_extended_command(const extended_cmd_payload_t* cmd_data) {
    if (!cmd_data) return;

    // 约定: cmd_id == 0x01 表示文本终端命令，params中为ASCII命令行
    if (cmd_data->cmd_id == 0x01) {
        size_t len = cmd_data->param_len;
        if (len > sizeof(cmd_data->params)) {
            len = sizeof(cmd_data->params);
        }
        char line[sizeof(cmd_data->params) + 1];
        memcpy(line, cmd_data->params, len);
        line[len] = '\0';
        handle_text_command(line);
    } else {
        ESP_LOGI(TAG, "收到扩展命令: id=0x%02X, len=%u", cmd_data->cmd_id, (unsigned)cmd_data->param_len);
    }
}

void cmd_terminal_handle_line(const char* line) {
    if (!line) return;
    char buf[666];
    strncpy(buf, line, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    handle_text_command(buf);
}