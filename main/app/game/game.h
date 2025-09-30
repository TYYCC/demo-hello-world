#ifndef UI_GAME_H
#define UI_GAME_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

/**
 * @brief 创建游戏主菜单界面
 * @param parent 父对象
 */
void ui_game_menu_create(lv_obj_t* parent);

/**
 * @brief 创建贪吃蛇游戏界面
 * @param parent 父对象
 */
void ui_snake_create(lv_obj_t* parent);

/**
 * @brief 创建俄罗斯方块游戏界面
 * @param parent 父对象
 */
void ui_tetris_create(lv_obj_t* parent);

/**
 * @brief 创建打砖块游戏界面
 * @param parent 父对象
 */
void ui_brickbreaker_create(lv_obj_t* parent);

#ifdef __cplusplus
}
#endif

#endif // UI_GAME_H
