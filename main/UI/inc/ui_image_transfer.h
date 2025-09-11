/*
 * @Author: tidycraze 2595256284@qq.com
 * @Date: 2025-09-10 13:37:58
 * @LastEditors: tidycraze 2595256284@qq.com
 * @LastEditTime: 2025-09-10 13:45:12
 * @FilePath: \demo-hello-world\main\UI\inc\ui_image_transfer.h
 * @Description: UI图像传输模块头文件，提供图像传输界面的创建、销毁和更新功能
 * 
 */

#ifndef UI_IMAGE_TRANSFER_H
#define UI_IMAGE_TRANSFER_H

#include "lvgl.h"
#include "image_transfer_app.h"

// Function Prototypes
void ui_image_transfer_create(lv_obj_t* parent);
void ui_image_transfer_destroy(void);

#endif // UI_IMAGE_TRANSFER_H
