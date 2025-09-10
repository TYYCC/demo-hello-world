/*
 * @Author: tidycraze 2595256284@qq.com
 * @Date: 2025-09-10 13:37:58
 * @LastEditors: tidycraze 2595256284@qq.com
 * @LastEditTime: 2025-09-10 13:45:12
 * @FilePath: \demo-hello-world\main\app\image_transfer\inc\image_transfer_protocol.h
 * @Description: 
 * 
 */
#ifndef IMAGE_TRANSFER_PROTOCOL_H
#define IMAGE_TRANSFER_PROTOCOL_H

#include <stdint.h>

// Define the synchronization word for the protocol header
#define PROTOCOL_SYNC_WORD 0xAEBC1402

// Define data type constants
typedef enum {
    DATA_TYPE_JPEG = 0x01,
    DATA_TYPE_LZ4  = 0x02,
    DATA_TYPE_RAW  = 0x03,
} data_type_t;

/**
 * @brief Defines the header structure for the image transfer protocol.
 *
 * This structure is marked with __attribute__((packed)) to ensure that the
 * compiler does not add any padding between the members. This is crucial for
 * correct protocol parsing, as the header is read directly from a byte stream.
 */
typedef struct {
    uint32_t sync_word; // Synchronization word (magic number) to identify the start of a frame.
    uint8_t  data_type; // The type of data in the payload (e.g., JPEG, LZ4, RAW).
    uint32_t data_len;  // The length of the payload data that follows this header.
} __attribute__((packed)) image_transfer_header_t;

#endif // IMAGE_TRANSFER_PROTOCOL_H