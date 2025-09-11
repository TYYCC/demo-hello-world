/*
 * @Author: tidycraze 2595256284@qq.com
 * @Date: 2025-09-10 13:37:58
 * @LastEditors: tidycraze 2595256284@qq.com
 * @LastEditTime: 2025-09-10 13:45:12
 * @FilePath: \demo-hello-world\main\app\image_transfer\inc\display_queue.h
 * @Description: 显示队列模块头文件，用于管理解码后的图像帧队列
 * 
 */
#ifndef DISPLAY_QUEUE_H
#define DISPLAY_QUEUE_H

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "image_transfer_protocol.h"

// 帧消息魔术数
#define FRAME_MSG_MAGIC 0x4652414D  // 'FRAM'

// 定义帧消息结构
typedef struct {
    uint32_t magic;          // 魔术数，标识帧消息
    frame_type_t type;       // 帧类型（JPEG或LZ4）
    uint16_t width;          // 帧宽度
    uint16_t height;         // 帧高度
    uint32_t payload_len;    // 有效载荷数据长度
    void *frame_buffer;      // 指向PSRAM中帧缓冲区的指针（RGB565LE格式）
} frame_msg_t;

/**
 * @brief 初始化显示队列
 * @return 队列句柄，如果初始化失败返回NULL
 */
QueueHandle_t display_queue_init(void);

/**
 * @brief 反初始化显示队列
 * @param queue 队列句柄
 */
void display_queue_deinit(QueueHandle_t queue);

/**
 * @brief 将帧消息加入队列
 * @param queue 队列句柄
 * @param frame_msg 帧消息指针
 * @return 成功加入队列返回true，否则返回false
 */
bool display_queue_enqueue(QueueHandle_t queue, frame_msg_t *frame_msg);

/**
 * @brief 从队列中取出帧消息
 * @param queue 队列句柄
 * @param frame_msg 用于接收帧消息的指针
 * @param timeout 超时时间
 * @return 成功取出帧消息返回true，否则返回false
 */
bool display_queue_dequeue(QueueHandle_t queue, frame_msg_t *frame_msg, TickType_t timeout);

/**
 * @brief 释放帧消息中的缓冲区资源
 * @param frame_msg 帧消息指针
 */
void display_queue_free_frame(frame_msg_t *frame_msg);

#endif // DISPLAY_QUEUE_H
