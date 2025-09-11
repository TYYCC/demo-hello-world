/*
 * @Author: tidycraze 2595256284@qq.com
 * @Date: 2025-09-10 13:37:58
 * @LastEditors: tidycraze 2595256284@qq.com
 * @LastEditTime: 2025-09-10 13:45:12
 * @FilePath: \demo-hello-world\main\app\image_transfer\inc\image_transfer_protocol.h
 * @Description: 该文件用于特殊的图传协议，为协议定义相关宏定义和结构体
 * 
 */
#ifndef IMAGE_TRANSFER_PROTOCOL_H
#define IMAGE_TRANSFER_PROTOCOL_H

#include <stdint.h>

// 协议同步字（魔数）
#define PROTOCOL_SYNC_WORD 0xAEBC1402

// 数据帧类型定义（根据提示词要求，移除RAW支持）
typedef enum {
    FRAME_TYPE_JPEG = 0x01,  // JPEG压缩帧
    FRAME_TYPE_LZ4  = 0x02,  // LZ4压缩帧
    // RAW类型已移除，不再支持
} frame_type_t;

/**
 * @brief 图像传输协议头部结构
 * 
 * 使用__attribute__((packed))确保编译器不会在成员之间添加填充，
 * 这对于直接从字节流读取头部进行正确协议解析至关重要。
 */
typedef struct {
    uint32_t sync_word;   // 同步字（魔数），标识帧的开始
    uint8_t  frame_type; // 帧数据类型（JPEG或LZ4）
    uint32_t data_len;   // 有效载荷数据的长度
} __attribute__((packed)) image_transfer_header_t;

#endif // IMAGE_TRANSFER_PROTOCOL_H