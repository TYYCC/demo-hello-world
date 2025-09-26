#ifndef LSM6DS_CONTROL_H
#define LSM6DS_CONTROL_H
#include "esp_err.h"
#include "lsm6ds3.h"

typedef struct {
    float pitch;
    float roll;
    float yaw;
} attitude_data_t;

extern TaskHandle_t s_lsm6ds3_control_task;

/**
 * @brief 初始化LSM6DS3控制任务
 */
esp_err_t init_lsm6ds3_control_task(void);

/**
 * @brief 安全地获取姿态数据
 * @param data 指向 attitude_data_t 结构体的指针，用于存储获取的数据
 */
void lsm6ds_control_get_attitude(attitude_data_t* data);

/**
 * @brief 获取LSM6DS3控制任务句柄
 */
TaskHandle_t get_lsm6ds3_control_task_handle(void);

#endif // LSM6DS_CONTROL_H