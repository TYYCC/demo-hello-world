/**
 * @file lv_conf.h
 * Configuration file for v8.4.0
 *
 * ESP32-S3 N16R8 Optimized Configuration
 */

/*
 * Copy this file as `lv_conf.h`
 * 1. simply next to the `lvgl` folder
 * 2. or any other places and
 *    - define `LV_CONF_INCLUDE_SIMPLE`
 *    - add the path as include path
 */
/* clang-format off */
#if 1 /*Set it to "1" to enable content*/

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/*====================
   LVGL VERSION
 *====================*/
#define LVGL_VERSION_MAJOR 8
#define LVGL_VERSION_MINOR 4
#define LVGL_VERSION_PATCH 0

/*====================
   COLOR SETTINGS
 *====================*/

/*Color depth: 1 (1 byte per pixel), 8 (RGB332), 16 (RGB565), 32 (ARGB8888)*/
#define LV_COLOR_DEPTH 16

/*Swap the 2 bytes of RGB565 color. Useful if the display has an 8-bit interface (e.g. SPI)*/
#define LV_COLOR_16_SWAP 1

#define LV_COLOR_SCREEN_TRANSP 0

/*=========================
   MEMORY SETTINGS
 *=========================*/

#define LV_MEM_CUSTOM 1
#if LV_MEM_CUSTOM == 0
    /*Size of the memory available for `lv_mem_alloc()` in bytes (>= 2kB)*/
    #define LV_MEM_SIZE (128U * 1024U)  /* 128K for internal RAM */
#else
    /*Header to include for the custom memory function*/
    #define LV_MEM_CUSTOM_INCLUDE "esp_heap_caps.h"
    /*Set the custom memory alloc functions. 'my_malloc' and 'my_free' must be declared.*/
    #define LV_MEM_CUSTOM_ALLOC(size)      heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
    #define LV_MEM_CUSTOM_FREE(ptr)        heap_caps_free(ptr)
    #define LV_MEM_CUSTOM_REALLOC(ptr, size) heap_caps_realloc(ptr, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#endif

/*====================
   HAL SETTINGS
 *====================*/

#define LV_DISP_DEF_REFR_PERIOD 16  // 60FPS (1000/60≈16ms)
#define LV_INDEV_DEF_READ_PERIOD 30
#define LV_TICK_CUSTOM 0

#define LV_DPI_DEF 130     /*[px/inch]*/

/*=======================
 * FEATURE CONFIGURATION
 *=======================*/

#define LV_DRAW_COMPLEX 1
#if LV_DRAW_COMPLEX
#define LV_SHADOW_CACHE_SIZE 0
#define LV_CIRCLE_CACHE_SIZE 4
#endif

#define LV_USE_GPU_ESP32_S3 1
#if LV_USE_GPU_ESP32_S3
    #define LV_GPU_ESP32_S3_USE_DRAW_BLEND 1
    #define LV_GPU_ESP32_S3_USE_DRAW_BG 1
    #define LV_GPU_ESP32_S3_USE_DRAW_RECT 1
    #define LV_GPU_ESP32_S3_USE_DRAW_ARC 1
#endif


/*-------------
 * Logging
 *-----------*/
#define LV_USE_LOG 1
#if LV_USE_LOG
    #define LV_LOG_LEVEL LV_LOG_LEVEL_WARN  // 改为只显示警告和错误
    #define LV_LOG_PRINTF 1
#endif

/*-------------
 * Asserts
 *-----------*/
#define LV_USE_ASSERT_STYLE 1

/*==================
 *   FONT USAGE
 *===================*/
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_18 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_DEFAULT &lv_font_montserrat_16

/* Enable dynamic font loading */
#define LV_FONT_LOAD 1
#define LV_FS 1

/*==================
 *  TEXT SETTINGS
 *=================*/
#define LV_TXT_ENC_UTF8 1  // 启用UTF-8编码支持（显示中文必需）
/* LV_TXT_ENC_ASCII is defined by LVGL internally */
#define LV_TXT_BREAK_CHARS " ,.;:-_"

/*==================
 *  WIDGET USAGE
 *================*/
#define LV_USE_ARC        1
#define LV_USE_BAR        1
#define LV_USE_BTN        1
#define LV_USE_LABEL      1
#define LV_USE_SWITCH     1
#define LV_USE_MSGBOX     1
#define LV_USE_IMG        1
#define LV_USE_CANVAS     1
#define LV_USE_SPINNER    1


/*==================
 * EXTRA COMPONENTS
 *==================*/
#define LV_USE_THEME_DEFAULT 1
#if LV_USE_THEME_DEFAULT
    #define LV_THEME_DEFAULT_DARK 0
    #define LV_THEME_DEFAULT_GROW 1
    #define LV_THEME_DEFAULT_TRANSITION_TIME 80
#endif

#define LV_USE_FLEX 1
#define LV_USE_GRID 1

/*==================
 * PERFORMANCE MONITOR
 *==================*/
#define LV_USE_PERF_MONITOR 0  // 启用性能监控
#if LV_USE_PERF_MONITOR
    #define LV_USE_MEM_MONITOR 1   // 内存监控
    #define LV_USE_OBSERVER 1      // 观察器
#endif


/*--END OF LV_CONF_H--*/

#endif /*LV_CONF_H*/

#endif /*End of "Content enable"*/
