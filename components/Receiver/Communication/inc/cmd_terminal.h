#ifndef CMD_TERMINAL_H
#define CMD_TERMINAL_H

#include <stdint.h>
#include <stdbool.h>
#include "tcp_common_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 输出回调函数类型
 * @param s 要输出的字符串
 * @note 上层可在其他源文件中提供强符号实现以覆写（例如通过USB CDC回传）
 */
void cmd_terminal_write(const char* s);

/**
 * @brief 运行时设置JPEG质量的钩子函数
 * @param quality JPEG质量值 (0-100)
 * @return true 设置成功，false 设置失败或未实现
 * @note jpeg模块可提供强符号实现，默认仅提示未生效
 */
bool cmd_terminal_set_jpeg_quality(uint8_t quality);

/**
 * @brief WiFi配置保存到NVS的钩子函数
 * @param ssid WiFi SSID
 * @param password WiFi密码
 * @return true 保存成功，false 保存失败或未实现
 * @note WiFi模块可提供强符号实现以覆写默认行为
 */
bool cmd_terminal_save_wifi_config(const char* ssid, const char* password);

/**
 * @brief 重启确认的钩子函数
 * @return true 确认重启，false 取消重启
 * @note 上层可提供强符号实现以自定义重启确认行为
 */
bool cmd_terminal_confirm_restart(void);

/**
 * @brief 处理扩展命令
 * @param cmd_data 扩展命令负载数据指针
 * @note 用于处理来自USB/SPI的扩展命令
 */
void handle_extended_command(const extended_cmd_payload_t* cmd_data);

/**
 * @brief 处理文本命令行
 * @param line 命令行字符串
 * @note 对外暴露的行命令处理接口（用于USB CDC等直接文本输入）
 */
void cmd_terminal_handle_line(const char* line);

#ifdef __cplusplus
}
#endif

#endif // CMD_TERMINAL_H