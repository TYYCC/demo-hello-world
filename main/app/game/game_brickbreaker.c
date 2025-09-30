#include "game.h"
#include "theme_manager.h"
#include "ui.h"
#include "key.h"
#include "lvgl.h"
#include <stdlib.h>

#define CUBE_START_X  10
#define CUBE_END_X    230
#define CUBE_START_Y  10
#define CUBE_END_Y    70
#define NUM_ROWS      3
#define NUM_COLS      11
#define CUBE_WIDTH    ((CUBE_END_X - CUBE_START_X) / NUM_COLS)
#define CUBE_HEIGHT   ((CUBE_END_Y - CUBE_START_Y) / NUM_ROWS)
#define BALL_SIZE     16

// 外部声明球体图片
LV_IMG_DECLARE(qiu)

typedef struct {
    lv_obj_t* obj;
    lv_obj_t* label;
    unsigned int alive;
} cube_type;

// 游戏状态
static cube_type cubes[NUM_ROWS][NUM_COLS];
static float move_x = -2.0f, move_y = -2.0f;
static float current_x = 120.0f, current_y = 120.0f;
static int score = 0;

// LVGL对象
static lv_obj_t* ball_obj = NULL;
static lv_obj_t* board_obj = NULL;
static lv_obj_t* score_label = NULL;
static lv_obj_t* game_container = NULL;
static lv_timer_t* game_timer = NULL;

// 游戏控制变量
static int board_x = 70;  // 挡板x位置（适配240宽度屏幕）
static bool game_active = false;

// 函数声明
static void game_timer_cb(lv_timer_t* t);
static void init_game_objects(lv_obj_t* parent);
static void start_game(void);
static void cleanup_game(void);
static int check_collision(lv_obj_t* obj1, lv_obj_t* obj2);
static void create_cube_animation(lv_color_t color, short x, short y);
static void back_to_game_menu(lv_event_t* e);

// 返回游戏菜单回调
static void back_to_game_menu(lv_event_t* e) {
    cleanup_game();
    lv_obj_t* screen = lv_scr_act();
    if (screen) {
        lv_obj_clean(screen);
        ui_game_menu_create(screen);
    }
}

// 碰撞检测
static int check_collision(lv_obj_t* obj1, lv_obj_t* obj2) {
    if (!obj1 || !obj2) return 0;
    
    int x1 = lv_obj_get_x(obj1);
    int x2 = lv_obj_get_x(obj2);
    int y1 = lv_obj_get_y(obj1);
    int y2 = lv_obj_get_y(obj2);
    
    int w1 = lv_obj_get_width(obj1);
    int w2 = lv_obj_get_width(obj2);
    int h1 = lv_obj_get_height(obj1);
    int h2 = lv_obj_get_height(obj2);
    
    // 右边碰撞
    if ((x2 - x1) == w1 && (y2 - y1) <= (h1 - 3) && (y1 - y2) <= h2 - 3) return 1;
    // 上边碰撞
    if ((y1 - y2) == h2 && (x2 - x1) <= (w1 - 3) && (x1 - x2) <= w2 - 3) return 2;
    // 左边碰撞
    if ((x1 - x2) == w2 && (y2 - y1) <= (h1 - 3) && (y1 - y2) <= h2 - 3) return 3;
    // 下边碰撞
    if ((y2 - y1) == h1 && (x2 - x1) <= (w1 - 3) && (x1 - x2) <= w2 - 3) return 4;
    
    return 0;
}

// 方块爆炸动画回调函数
static void anim_exec_cb1(void* var, int32_t v) {
    lv_obj_t* obj = (lv_obj_t*)var;
    lv_obj_set_x(obj, lv_obj_get_x(obj) - 1);
    lv_obj_set_y(obj, lv_obj_get_y(obj) + v - 1);
}

static void anim_exec_cb2(void* var, int32_t v) {
    lv_obj_t* obj = (lv_obj_t*)var;
    lv_obj_set_x(obj, lv_obj_get_x(obj) + 1);
    lv_obj_set_y(obj, lv_obj_get_y(obj) + v - 1);
}

static void anim_exec_cb3(void* var, int32_t v) {
    lv_obj_t* obj = (lv_obj_t*)var;
    lv_obj_set_x(obj, lv_obj_get_x(obj) - 3);
    lv_obj_set_y(obj, lv_obj_get_y(obj) + v - 1);
}

static void anim_exec_cb4(void* var, int32_t v) {
    lv_obj_t* obj = (lv_obj_t*)var;
    lv_obj_set_x(obj, lv_obj_get_x(obj) + 3);
    lv_obj_set_y(obj, lv_obj_get_y(obj) + v - 1);
}

static void anim_exec_cb5(void* var, int32_t v) {
    lv_obj_t* obj = (lv_obj_t*)var;
    lv_obj_set_x(obj, lv_obj_get_x(obj) + 5);
    lv_obj_set_y(obj, lv_obj_get_y(obj) + v - 1);
}

static void anim_exec_cb6(void* var, int32_t v) {
    lv_obj_t* obj = (lv_obj_t*)var;
    lv_obj_set_x(obj, lv_obj_get_x(obj) - 5);
    lv_obj_set_y(obj, lv_obj_get_y(obj) + v - 1);
}

static void anim_ready_cb(lv_anim_t* var) {
    lv_obj_del((lv_obj_t*)var->var);
}

// 创建方块爆炸动画
static void create_cube_animation(lv_color_t color, short x, short y) {
    lv_anim_t animations[6];
    lv_obj_t* cube_fragments[6];
    lv_anim_exec_xcb_t exec_callbacks[6] = {
        anim_exec_cb1, anim_exec_cb2, anim_exec_cb3,
        anim_exec_cb4, anim_exec_cb5, anim_exec_cb6
    };
    
    for (int i = 0; i < 6; i++) {
        cube_fragments[i] = lv_btn_create(game_container);
        lv_obj_set_size(cube_fragments[i], (CUBE_WIDTH - 2) / 2, (CUBE_HEIGHT - 2) / 2);
        lv_obj_set_pos(cube_fragments[i], CUBE_WIDTH / 4 + x, CUBE_HEIGHT / 4 + y);
        lv_obj_set_style_bg_color(cube_fragments[i], color, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(cube_fragments[i], 0, LV_PART_MAIN);
        
        lv_anim_init(&animations[i]);
        lv_anim_set_var(&animations[i], cube_fragments[i]);
        lv_anim_set_exec_cb(&animations[i], exec_callbacks[i]);
        lv_anim_set_time(&animations[i], 1500);
        lv_anim_set_delay(&animations[i], rand() >> 24);
        lv_anim_set_values(&animations[i], 1, 20 + (rand() >> 27));
        lv_anim_set_ready_cb(&animations[i], anim_ready_cb);
        lv_anim_start(&animations[i]);
    }
}

// 游戏主循环
static void game_timer_cb(lv_timer_t* t) {
    if (!game_active || !ball_obj || !board_obj) return;
    
    // 处理按键输入
    key_dir_t keys = key_scan();
    
    // 左右移动挡板
    if (keys & KEY_LEFT) {
        board_x -= 4;
        if (board_x < 0) board_x = 0;
        lv_obj_set_x(board_obj, board_x);
    }
    if (keys & KEY_RIGHT) {
        board_x += 4;
        if (board_x > 160) board_x = 160;  // 240 - 80(板宽)
        lv_obj_set_x(board_obj, board_x);
    }
    
    // 更新分数显示
    if (score_label) {
        lv_label_set_text_fmt(score_label, "Score: %d", score);
    }
    
    // 更新球的位置
    current_x += move_x;
    current_y += move_y;
    lv_obj_set_pos(ball_obj, (int)current_x, (int)current_y);
    lv_img_set_angle(ball_obj, lv_img_get_angle(ball_obj) + (int)(40 * move_x));
    
    // 检测游戏失败（球掉出屏幕）
    if (current_y > 260) {  // 适配更小的屏幕
        game_active = false;
        lv_obj_t* game_over_label = lv_label_create(game_container);
        lv_label_set_text(game_over_label, "GAME OVER");
        lv_obj_set_style_text_color(game_over_label, lv_color_hex(0xFF0000), LV_PART_MAIN);
        lv_obj_set_style_text_font(game_over_label, &lv_font_montserrat_20, 0);
        lv_obj_center(game_over_label);
        return;
    }
    
    // 检测与挡板的碰撞
    if (check_collision(ball_obj, board_obj) == 4) {
        int ball_board_distance = lv_obj_get_x(ball_obj) - lv_obj_get_x(board_obj) - 30;
        move_x = (float)ball_board_distance / 20.0f;
        if (move_x < -2.0f) move_x = -2.0f;
        if (move_x > 2.0f) move_x = 2.0f;
        move_y = move_y * (-1);
        current_y += move_y;
    }
    
    // 边界碰撞检测（修复右边界反弹问题）
    if (current_x <= 0) {
        current_x = 0;
        move_x = move_x * (-1);
    }
    if (current_x >= 240 - BALL_SIZE) {
        current_x = 240 - BALL_SIZE;
        move_x = move_x * (-1);
    }
    if (current_y <= 0) {
        current_y = 0;
        move_y = move_y * (-1);
    }
    
    // 检测与方块的碰撞
    for (int i = 0; i < NUM_ROWS; i++) {
        for (int j = 0; j < NUM_COLS; j++) {
            if (!cubes[i][j].alive) continue;
            
            int collision = check_collision(ball_obj, cubes[i][j].obj);
            if (collision > 0) {
                score++;
                create_cube_animation(
                    lv_obj_get_style_bg_color(cubes[i][j].obj, LV_PART_MAIN),
                    lv_obj_get_x(cubes[i][j].obj),
                    lv_obj_get_y(cubes[i][j].obj)
                );
                
                cubes[i][j].alive--;
                lv_label_set_text_fmt(cubes[i][j].label, "%d", cubes[i][j].alive);
                
                current_x -= move_x;
                current_y -= move_y;
                
                if (collision == 1 || collision == 3) {
                    move_x = move_x * (-1);
                } else {
                    move_y = move_y * (-1);
                }
                
                if (cubes[i][j].alive == 0) {
                    lv_obj_del(cubes[i][j].obj);
                }
                return;
            }
        }
    }
    
    // 检查是否所有方块都被消除
    bool all_cleared = true;
    for (int i = 0; i < NUM_ROWS; i++) {
        for (int j = 0; j < NUM_COLS; j++) {
            if (cubes[i][j].alive) {
                all_cleared = false;
                break;
            }
        }
        if (!all_cleared) break;
    }
    
    if (all_cleared) {
        game_active = false;
        lv_obj_t* win_label = lv_label_create(game_container);
        lv_label_set_text(win_label, "YOU WIN!");
        lv_obj_set_style_text_color(win_label, lv_color_hex(0x00FF00), LV_PART_MAIN);
        lv_obj_set_style_text_font(win_label, &lv_font_montserrat_20, 0);
        lv_obj_center(win_label);
    }
}

// 开始游戏按钮回调
static void start_game_cb(lv_event_t* e) {
    lv_obj_t* btn = lv_event_get_target(e);
    lv_obj_del(btn);
    start_game();
}

// 初始化游戏对象
static void init_game_objects(lv_obj_t* parent) {
    game_container = parent;
    
    // 创建方块
    for (int i = 0; i < NUM_ROWS; i++) {
        for (int j = 0; j < NUM_COLS; j++) {
            cubes[i][j].obj = lv_btn_create(parent);
            lv_obj_set_size(cubes[i][j].obj, CUBE_WIDTH - 2, CUBE_HEIGHT - 2);
            lv_obj_set_pos(cubes[i][j].obj,
                          j * (CUBE_END_X - CUBE_START_X) / NUM_COLS + CUBE_START_X,
                          i * (CUBE_END_Y - CUBE_START_Y) / NUM_ROWS + CUBE_START_Y);
            lv_obj_set_style_bg_color(cubes[i][j].obj,
                                     lv_color_hex(rand() * 512 + rand() / 128),
                                     LV_PART_MAIN);
            lv_obj_set_style_radius(cubes[i][j].obj, 0, LV_PART_MAIN);
            lv_obj_set_style_shadow_width(cubes[i][j].obj, 0, LV_PART_MAIN);
            
            cubes[i][j].alive = (rand() >> 28) + 1;
            cubes[i][j].label = lv_label_create(cubes[i][j].obj);
            lv_obj_center(cubes[i][j].label);
            lv_label_set_text_fmt(cubes[i][j].label, "%d", cubes[i][j].alive);
        }
    }
    
    // 创建挡板
    board_obj = lv_btn_create(parent);
    lv_obj_set_align(board_obj, LV_ALIGN_BOTTOM_LEFT);
    lv_obj_set_y(board_obj, -20);
    lv_obj_set_size(board_obj, 80, 12);
    lv_obj_set_style_bg_color(board_obj, lv_color_hex(0xFF0000), LV_PART_MAIN);
    
    lv_obj_t* arrow = lv_label_create(board_obj);
    lv_label_set_text(arrow, "<<===>>>");
    lv_obj_set_style_text_color(arrow, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_text_font(arrow, &lv_font_montserrat_12, 0);
    lv_obj_center(arrow);
    
    // 创建球
    ball_obj = lv_img_create(parent);
    lv_img_set_src(ball_obj, &qiu);
    lv_obj_set_pos(ball_obj, (int)current_x, (int)current_y);
    
    // 创建分数标签（放在方块区域下方）
    score_label = lv_label_create(parent);
    lv_label_set_text(score_label, "Score: 0");
    lv_obj_set_style_text_color(score_label, lv_color_hex(0xFFFF00), LV_PART_MAIN);
    lv_obj_set_style_text_font(score_label, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(score_label, 10, 75);  // 固定位置，方块下方
}

// 开始游戏
static void start_game(void) {
    current_x = 120.0f;
    current_y = 120.0f;
    move_x = -2.0f;
    move_y = -2.0f;
    score = 0;
    board_x = 70;
    game_active = true;
    
    init_game_objects(game_container);
    
    // 创建游戏定时器
    game_timer = lv_timer_create(game_timer_cb, 6, NULL);
}

// 清理游戏资源
static void cleanup_game(void) {
    if (game_timer) {
        lv_timer_del(game_timer);
        game_timer = NULL;
    }
    lv_anim_del_all();
    game_active = false;
}

// 主创建函数
void ui_brickbreaker_create(lv_obj_t* parent) {
    // 应用当前主题到屏幕
    theme_apply_to_screen(parent);
    
    // 1. 创建页面父级容器
    lv_obj_t* page_parent_container;
    ui_create_page_parent_container(parent, &page_parent_container);
    
    // 2. 创建顶部栏
    lv_obj_t* top_bar_container;
    lv_obj_t* title_container;
    ui_create_top_bar(page_parent_container, "Brick Breaker", false,
                     &top_bar_container, &title_container, NULL);
    
    // 替换返回按钮回调
    lv_obj_t* back_btn = lv_obj_get_child(top_bar_container, 0);
    if (back_btn) {
        lv_obj_remove_event_cb(back_btn, NULL);
        lv_obj_add_event_cb(back_btn, back_to_game_menu, LV_EVENT_CLICKED, NULL);
    }
    
    // 3. 创建页面内容容器
    lv_obj_t* content_container;
    ui_create_page_content_area(page_parent_container, &content_container);
    
    // 设置内容容器为不可滚动
    lv_obj_clear_flag(content_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(content_container, lv_color_hex(0x000000), LV_PART_MAIN);
    
    game_container = content_container;
    
    // 4. 创建开始按钮
    lv_obj_t* start_btn = lv_btn_create(content_container);
    lv_obj_align(start_btn, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_size(start_btn, 130, 45);
    
    lv_obj_t* label = lv_label_create(start_btn);
    lv_label_set_text(label, "START GAME");
    lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_event_cb(start_btn, start_game_cb, LV_EVENT_CLICKED, NULL);
}
