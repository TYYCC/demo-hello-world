#include "game.h"
#include "theme_manager.h"
#include "ui.h" // For ui_main_menu_create

// --- 回调函数 ---

// 返回主菜单的回调
static void back_to_main_menu_cb(lv_event_t* e) {
    lv_obj_t* screen = lv_scr_act();
    if (screen) {
        lv_obj_clean(screen);
        ui_main_menu_create(screen);
    }
}

// 启动俄罗斯方块的回调
static void tetris_game_cb(lv_event_t* e) {
    lv_obj_t* screen = lv_scr_act();
    if (screen) {
        lv_obj_clean(screen);
        ui_tetris_create(screen);
    }
}

// 启动贪吃蛇的回调
static void snake_game_cb(lv_event_t* e) {
    lv_obj_t* screen = lv_scr_act();
    if (screen) {
        lv_obj_clean(screen);
        ui_snake_create(screen);
    }
}

// 启动打砖块的回调
static void brickbreaker_game_cb(lv_event_t* e) {
    lv_obj_t* screen = lv_scr_act();
    if (screen) {
        lv_obj_clean(screen);
        ui_brickbreaker_create(screen);
    }
}

// --- 游戏菜单创建 ---

void ui_game_menu_create(lv_obj_t* parent) {
    // 应用当前主题到屏幕
    theme_apply_to_screen(parent);

    // 1. 创建页面父级容器（统一管理整个页面）
    lv_obj_t* page_parent_container;
    ui_create_page_parent_container(parent, &page_parent_container);

    // 2. 创建顶部栏容器（包含返回按钮和标题）
    lv_obj_t* top_bar_container;
    lv_obj_t* title_container;
    ui_create_top_bar(page_parent_container, "Game Menu", false, &top_bar_container, &title_container, NULL);

    // 替换顶部栏的返回按钮回调为自定义回调
    lv_obj_t* back_btn = lv_obj_get_child(top_bar_container, 0); // 获取返回按钮
    if (back_btn) {
        lv_obj_remove_event_cb(back_btn, NULL); // 移除默认回调
        lv_obj_add_event_cb(back_btn, back_to_main_menu_cb, LV_EVENT_CLICKED, NULL);
    }

    // 3. 创建页面内容容器
    lv_obj_t* content_container;
    ui_create_page_content_area(page_parent_container, &content_container);

    // 4. 在content_container中添加页面内容
    // 创建游戏列表
    lv_obj_t* list = lv_list_create(content_container);
    lv_obj_set_size(list, 220, 180);
    lv_obj_align(list, LV_ALIGN_CENTER, 0, 0);

    // 添加俄罗斯方块按钮
    lv_obj_t* tetris_btn = lv_list_add_btn(list, LV_SYMBOL_PLAY, "Tetris");
    lv_obj_add_event_cb(tetris_btn, tetris_game_cb, LV_EVENT_CLICKED, NULL);

    // 添加贪吃蛇按钮
    lv_obj_t* snake_btn = lv_list_add_btn(list, LV_SYMBOL_PLAY, "Snake");
    lv_obj_add_event_cb(snake_btn, snake_game_cb, LV_EVENT_CLICKED, NULL);

    // 添加打砖块按钮
    lv_obj_t* brickbreaker_btn = lv_list_add_btn(list, LV_SYMBOL_PLAY, "Brick Breaker");
    lv_obj_add_event_cb(brickbreaker_btn, brickbreaker_game_cb, LV_EVENT_CLICKED, NULL);
}
