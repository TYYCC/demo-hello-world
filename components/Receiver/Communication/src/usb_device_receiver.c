#include "usb_device_receiver.h"
#include "sdkconfig.h"

#include "tinyusb.h"
#include "tusb.h"
#include "tusb_cdc_acm.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tcp_common_protocol.h"
#include "cmd_terminal.h"
#include <string.h>

static const char* TAG = "usb_rx";

static TaskHandle_t s_usb_task = NULL;
static uint8_t s_rx_chunk[USB_RX_CHUNK_SIZE];
static uint8_t* s_parse_buf = NULL;
static size_t s_parse_len = 0;
static bool s_usb_connected = false;

// USB CDC 连接状态回调
static void usb_line_state_changed_callback(int itf, cdcacm_event_t* event) {
    if (event->type == CDC_EVENT_LINE_STATE_CHANGED) {
        bool dtr = event->line_state_changed_data.dtr;
        bool rts = event->line_state_changed_data.rts;
        s_usb_connected = (dtr && rts);
        ESP_LOGI(TAG, "USB连接状态: DTR=%d, RTS=%d, 连接=%s", dtr, rts,
                 s_usb_connected ? "已连接" : "断开");
    }
}

static void parse_and_dispatch(const uint8_t* data, size_t len) {
    if (!data || !s_parse_buf || len == 0)
        return;

    // 判断模式：若缓冲区以协议帧头(0xAA55)起始，则按二进制帧解析；否则按ASCII行命令解析
    if (len >= 2 && data[0] == FRAME_HEADER_1 && data[1] == FRAME_HEADER_2) {
        size_t pos = 0;
        while (pos + MIN_FRAME_SIZE <= len) {
            if (pos + sizeof(protocol_header_t) > len)
                break;
            
            // 检查帧头
            if (data[pos] != FRAME_HEADER_1 || data[pos + 1] != FRAME_HEADER_2) {
                // 在二进制模式下遇到非帧头字节，跳过1字节
                pos++;
                continue;
            }
            
            // 获取协议头信息
            protocol_header_t* header = (protocol_header_t*)&data[pos];
            size_t frame_size = sizeof(protocol_header_t) + header->length + sizeof(uint16_t);
            
            if (pos + frame_size > len)
                break; // 帧不完整，等待更多数据

            // 验证帧
            if (validate_frame(&data[pos], frame_size)) {
                switch (header->frame_type) {
                case FRAME_TYPE_COMMAND:
                    // 处理命令帧（如遥控数据）
                    ESP_LOGI(TAG, "Received command frame via USB");
                    break;
                case FRAME_TYPE_HEARTBEAT:
                    // 处理心跳帧
                    ESP_LOGI(TAG, "Received heartbeat frame via USB");
                    break;
                case FRAME_TYPE_EXTENDED:
                    // 处理扩展帧
                    ESP_LOGI(TAG, "Received extended frame via USB");
                    if (header->length >= sizeof(extended_cmd_payload_t)) {
                        handle_extended_command((const extended_cmd_payload_t*)&data[pos + sizeof(protocol_header_t)]);
                    }
                    break;
                default:
                    ESP_LOGW(TAG, "Unknown frame type: 0x%02X", header->frame_type);
                    break;
                }
            } else {
                ESP_LOGW(TAG, "Frame validation failed via USB");
            }
            pos += frame_size;
        }
        if (pos < len) {
            size_t remain = len - pos;
            if (remain > USB_RX_BUFFER_SIZE)
                remain = USB_RX_BUFFER_SIZE;
            memmove(s_parse_buf, &data[pos], remain);
            s_parse_len = remain;
        } else {
            s_parse_len = 0;
        }
    } else {
        // 文本模式：按行(\r/\n)拆分并分发到命令终端
        size_t start = 0;
        while (start < len) {
            size_t i = start;
            size_t line_end = (size_t)-1;
            for (; i < len; ++i) {
                if (data[i] == '\n' || data[i] == '\r') {
                    line_end = i;
                    break;
                }
            }
            if (line_end == (size_t)-1) {
                break; // 无完整行，等待更多数据
            }
            size_t line_len = line_end - start;
            char linebuf[192];
            if (line_len >= sizeof(linebuf))
                line_len = sizeof(linebuf) - 1;
            memcpy(linebuf, &data[start], line_len);
            linebuf[line_len] = '\0';
            cmd_terminal_handle_line(linebuf);
            // 跳过换行符（支持CRLF/ LFCR）
            size_t skip = 1;
            if (line_end + 1 < len) {
                if ((data[line_end] == '\r' && data[line_end + 1] == '\n') ||
                    (data[line_end] == '\n' && data[line_end + 1] == '\r')) {
                    skip = 2;
                }
            }
            start = line_end + skip;
        }
        // 将未完成的最后一行保留到解析缓冲
        if (start < len) {
            size_t remain = len - start;
            if (remain > USB_RX_BUFFER_SIZE)
                remain = USB_RX_BUFFER_SIZE;
            memmove(s_parse_buf, &data[start], remain);
            s_parse_len = remain;
        } else {
            s_parse_len = 0;
        }
    }
}

static void usb_rx_task(void* arg) {
    ESP_LOGI(TAG, "USB CDC 接收任务启动");

    if (!s_parse_buf) {
        ESP_LOGE(TAG, "Parse buffer not initialized");
        vTaskDelete(NULL);
        return;
    }

    // 等待USB连接建立
    while (!tusb_cdc_acm_initialized(TINYUSB_CDC_ACM_0)) {
        ESP_LOGI(TAG, "等待USB CDC初始化完成...");
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    ESP_LOGI(TAG, "USB CDC已初始化，开始接收数据");

    while (1) {
        size_t n = 0;
        esp_err_t ret = tinyusb_cdcacm_read(TINYUSB_CDC_ACM_0, s_rx_chunk, sizeof(s_rx_chunk), &n);
        if (ret == ESP_OK && n > 0) {
            // 接收到USB数据
            if (s_parse_len + (size_t)n > USB_RX_BUFFER_SIZE) {
                size_t to_copy = USB_RX_BUFFER_SIZE;
                if ((size_t)n < USB_RX_BUFFER_SIZE) {
                    memmove(s_parse_buf, &s_parse_buf[s_parse_len + n - USB_RX_BUFFER_SIZE],
                            USB_RX_BUFFER_SIZE - (size_t)n);
                    memcpy(&s_parse_buf[USB_RX_BUFFER_SIZE - (size_t)n], s_rx_chunk, (size_t)n);
                } else {
                    memcpy(s_parse_buf, &s_rx_chunk[n - USB_RX_BUFFER_SIZE], USB_RX_BUFFER_SIZE);
                }
                s_parse_len = to_copy;
            } else {
                memcpy(&s_parse_buf[s_parse_len], s_rx_chunk, (size_t)n);
                s_parse_len += (size_t)n;
            }
            parse_and_dispatch(s_parse_buf, s_parse_len);
        } else {
            vTaskDelay(pdMS_TO_TICKS(5));
        }
        // 无论是否有数据，适当让出 CPU，防止长期占用导致 WDT 触发
        vTaskDelay(1);
    }
}

esp_err_t usb_receiver_init(void) {
    // Allocate parse buffer from PSRAM to save internal RAM
    s_parse_buf = (uint8_t*)heap_caps_malloc(USB_RX_BUFFER_SIZE, MALLOC_CAP_SPIRAM);
    if (!s_parse_buf) {
        ESP_LOGE(TAG, "Failed to allocate parse buffer from PSRAM");
        return ESP_ERR_NO_MEM;
    }
    // ESP32-S3内置USB接口，不需要外部PHY
    const tinyusb_config_t tusb_cfg = {
        .device_descriptor = NULL,        // 使用默认设备描述符
        .string_descriptor = NULL,        // 使用默认字符串描述符
        .external_phy = false,            // ESP32-S3使用内置USB PHY
        .configuration_descriptor = NULL, // 使用默认配置描述符
        .self_powered = false,            // 总线供电模式
        .vbus_monitor_io = -1,            // ESP32-S3不需要外部VBUS监控
    };
    esp_err_t ret = tinyusb_driver_install(&tusb_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "tinyusb_driver_install 失败: %s", esp_err_to_name(ret));
        free(s_parse_buf);
        s_parse_buf = NULL;
        return ret;
    }

    tinyusb_config_cdcacm_t acm_cfg = {
        .usb_dev = TINYUSB_USBDEV_0,
        .cdc_port = TINYUSB_CDC_ACM_0,
        .rx_unread_buf_sz = 2048, // 扩大内部未读缓冲
        .callback_rx = NULL,
        .callback_rx_wanted_char = NULL,
        .callback_line_state_changed = usb_line_state_changed_callback,
        .callback_line_coding_changed = NULL,
    };
    ret = tusb_cdc_acm_init(&acm_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "tinyusb_cdcacm_init 失败: %s", esp_err_to_name(ret));
        free(s_parse_buf);
        s_parse_buf = NULL;
        return ret;
    }

    ESP_LOGI(TAG, "USB CDC 初始化完成");

    // 等待USB连接建立
    ESP_LOGI(TAG, "等待USB设备连接...");

    // 给USB设备一些时间来枚举
    vTaskDelay(pdMS_TO_TICKS(1000));
    return ESP_OK;
}

void usb_receiver_start(void) {
    if (s_usb_task)
        return;
    // 降低任务优先级并固定到 CPU1，减少对 CPU0 空闲任务的影响
    xTaskCreatePinnedToCore(usb_rx_task, "usb_rx", 4096, NULL, 4, &s_usb_task, 1);
}

void usb_receiver_stop(void) {
    if (s_usb_task) {
        vTaskDelete(s_usb_task);
        s_usb_task = NULL;
    }
    // tinyusb_driver_uninstall 函数在当前版本中不可用 (IDF-1474)
    // 由于没有卸载函数，直接返回
    if (s_parse_buf) {
        free(s_parse_buf);
        s_parse_buf = NULL;
    }
}

// 供命令终端使用的输出函数：通过USB CDC回传到主机
void cmd_terminal_write(const char* s) {
    if (!s || !s_usb_connected)
        return;
    size_t len = strlen(s);
    // 将整段字符串写入CDC队列并flush
    tinyusb_cdcacm_write_queue(TINYUSB_CDC_ACM_0, (const uint8_t*)s, len);
    tinyusb_cdcacm_write_flush(TINYUSB_CDC_ACM_0, 0);
    // 追加换行，便于在终端阅读
    const char crlf[] = "\r\n";
    tinyusb_cdcacm_write_queue(TINYUSB_CDC_ACM_0, (const uint8_t*)crlf, sizeof(crlf) - 1);
    tinyusb_cdcacm_write_flush(TINYUSB_CDC_ACM_0, 0);
}
