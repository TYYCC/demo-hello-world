/**
 * @file auto_pairing.h
 * @brief 自动配对功能头文件
 * @author TidyCraze
 * @date 2025-01-15
 */

#ifndef AUTO_PAIRING_H
#define AUTO_PAIRING_H

#ifdef __cplusplus
extern "C" {
#endif

// 配对状态
typedef enum {
    PAIRING_STATE_IDLE,
    PAIRING_STATE_WAITING,
    PAIRING_STATE_CONNECTING,
    PAIRING_STATE_SUCCESS,
    PAIRING_STATE_FAILED,
    PAIRING_STATE_RESTARTING
} pairing_state_t;

/**
 * @brief 启动自动配对功能
 */
void auto_pairing_start(void);

/**
 * @brief 停止自动配对功能
 */
void auto_pairing_stop(void);

/**
 * @brief 获取配对状态
 * @return pairing_state_t 当前配对状态
 */
pairing_state_t auto_pairing_get_state(void);

/**
 * @brief 检查配对是否正在进行
 * @return true if pairing is active, false otherwise
 */
bool auto_pairing_is_active(void);

#ifdef __cplusplus
}
#endif

#endif // AUTO_PAIRING_H
