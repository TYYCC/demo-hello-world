/*
 * @Author: tidycraze 2595256284@qq.com
 * @Date: 2025-09-10 13:37:58
 * @LastEditors: tidycraze 2595256284@qq.com
 * @LastEditTime: 2025-09-10 13:45:12
 * @FilePath: \demo-hello-world\main\app\image_transfer\src\display_queue.c
 * @Description: 显示队列模块实现，用于管理解码后的图像帧队列
 * 
 */
#include "display_queue.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

static const char *TAG = "display_queue";

/**
 * @brief 初始化显示队列
 * @return 队列句柄，如果初始化失败返回NULL
 */
QueueHandle_t display_queue_init(void) {
    // 创建队列，大小为4个帧消息
    QueueHandle_t queue = xQueueCreate(4, sizeof(frame_msg_t));
    if (queue == NULL) {
        ESP_LOGE(TAG, "Failed to create display queue");
        return NULL;
    }
    ESP_LOGI(TAG, "Display queue created successfully");
    return queue;
}

/**
 * @brief 反初始化显示队列
 * @param queue 队列句柄
 */
void display_queue_deinit(QueueHandle_t queue) {
    if (queue != NULL) {
        // 释放队列中剩余的帧
        frame_msg_t msg;
        while (xQueueReceive(queue, &msg, 0) == pdTRUE) {
            display_queue_free_frame(&msg);
        }
        vQueueDelete(queue);
        ESP_LOGI(TAG, "Display queue destroyed");
    }
}

/**
 * @brief 将帧消息加入队列
 * @param queue 队列句柄
 * @param frame_msg 帧消息指针
 * @return 成功加入队列返回true，否则返回false
 */
bool display_queue_enqueue(QueueHandle_t queue, frame_msg_t *frame_msg) {
    if (queue == NULL || frame_msg == NULL) {
        return false;
    }
    
    // 设置帧消息魔术数
    frame_msg->magic = FRAME_MSG_MAGIC;
    
    // 如果队列已满，移除最旧的帧以腾出空间
    if (uxQueueMessagesWaiting(queue) >= 4) {
        frame_msg_t old_msg;
        if (xQueueReceive(queue, &old_msg, 0) == pdTRUE) {
            display_queue_free_frame(&old_msg);
            ESP_LOGW(TAG, "Display queue full, dropped oldest frame");
        }
    }
    
    return xQueueSend(queue, frame_msg, portMAX_DELAY) == pdTRUE;
}

/**
 * @brief 从队列中取出帧消息
 * @param queue 队列句柄
 * @param frame_msg 用于接收帧消息的指针
 * @param timeout 超时时间
 * @return 成功取出帧消息返回true，否则返回false
 */
bool display_queue_dequeue(QueueHandle_t queue, frame_msg_t *frame_msg, TickType_t timeout) {
    if (queue == NULL || frame_msg == NULL) {
        return false;
    }
    
    BaseType_t result = xQueueReceive(queue, frame_msg, timeout);
    if (result == pdTRUE && frame_msg->magic == FRAME_MSG_MAGIC) {
        return true;
    }
    
    return false;
}

/**
 * @brief 释放帧消息中的缓冲区资源
 * @param frame_msg 帧消息指针
 */
void display_queue_free_frame(frame_msg_t *frame_msg) {
    if (frame_msg != NULL && frame_msg->frame_buffer != NULL) {
        heap_caps_free(frame_msg->frame_buffer);
        frame_msg->frame_buffer = NULL;
        frame_msg->magic = 0; // 清除魔术数
    }
}
