/**
 * @file ui_binding.h
 * @brief 配对模式UI界面头文件
 * @author TidyCraze
 * @date 2025-12-09
 */

#ifndef UI_BINDING_H
#define UI_BINDING_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

/**
 * @brief 创建配对界面
 * @param parent 父对象，通常是 lv_scr_act()
 */
void ui_binding_create(lv_obj_t* parent);

/**
 * @brief 销毁配对界面
 */
void ui_binding_destroy(void);

/**
 * @brief 开始配对模式
 */
void ui_binding_start(void);

/**
 * @brief 设置配对成功状态
 */
void ui_binding_success(void);

/**
 * @brief 检查配对模式是否处于活跃状态
 * @return true 配对正在进行，false 未进行
 */
bool ui_binding_is_active(void);

/**
 * @brief 从外部事件触发配对完成
 * 由SDK在检测到接收机确认绑定时调用
 */
void ui_binding_notify_success(void);

/**
 * @brief 从外部事件触发配对失败
 * 由SDK在绑定失败时调用
 */
void ui_binding_notify_failure(void);

/**
 * @brief 初始化UI配对模块
 * 应在LVGL初始化后、任何UI操作之前调用
 */
void ui_binding_module_init(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_BINDING_H */
