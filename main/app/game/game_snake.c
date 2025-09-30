#include "game.h"
#include "theme_manager.h"
#include "ui.h"
#include "key.h"
#include "lvgl.h"
#include <stdlib.h>
#include <string.h>

// 游戏区域配置
#define GAME_AREA_X     10
#define GAME_AREA_Y     10
#define GAME_WIDTH      220
#define GAME_HEIGHT     240
#define CELL_SIZE       10
#define GRID_COLS       (GAME_WIDTH / CELL_SIZE)   // 22列
#define GRID_ROWS       (GAME_HEIGHT / CELL_SIZE)  // 24行
#define MAX_SNAKE_LEN   (GRID_COLS * GRID_ROWS)

// 方向定义
typedef enum {
    DIR_UP = 0,
    DIR_DOWN,
    DIR_LEFT,
    DIR_RIGHT
} direction_t;

// 蛇身节点
typedef struct {
    int x;
    int y;
} snake_node_t;

// 游戏状态
static snake_node_t snake[MAX_SNAKE_LEN];
static int snake_length = 3;
static direction_t current_dir = DIR_RIGHT;
static direction_t next_dir = DIR_RIGHT;

static snake_node_t food;
static int score = 0;
static bool game_over = false;
static bool game_started = false;

// LVGL对象
static lv_obj_t* game_container = NULL;
static lv_obj_t* canvas = NULL;
static lv_obj_t* score_label = NULL;
static lv_timer_t* game_timer = NULL;
static lv_color_t* canvas_buf = NULL;

// 游戏速度（毫秒）
static uint32_t game_speed = 150;

// 函数声明
static void game_init(void);
static void game_loop(lv_timer_t* timer);
static void draw_game(void);
static void move_snake(void);
static void generate_food(void);
static bool check_collision(void);
static bool check_food_collision(void);
static void input_handler(void);
static void start_game_cb(lv_event_t* e);
static void cleanup_game(void);
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

// 初始化游戏
static void game_init(void) {
    // 初始化蛇（在中间位置）
    snake_length = 3;
    snake[0].x = GRID_COLS / 2;
    snake[0].y = GRID_ROWS / 2;
    snake[1].x = snake[0].x - 1;
    snake[1].y = snake[0].y;
    snake[2].x = snake[0].x - 2;
    snake[2].y = snake[0].y;
    
    current_dir = DIR_RIGHT;
    next_dir = DIR_RIGHT;
    score = 0;
    game_over = false;
    game_started = true;
    game_speed = 150;
    
    // 生成第一个食物
    generate_food();
}

// 生成食物
static void generate_food(void) {
    bool valid_position = false;
    
    while (!valid_position) {
        food.x = rand() % GRID_COLS;
        food.y = rand() % GRID_ROWS;
        
        // 检查食物是否与蛇重叠
        valid_position = true;
        for (int i = 0; i < snake_length; i++) {
            if (snake[i].x == food.x && snake[i].y == food.y) {
                valid_position = false;
                break;
            }
        }
    }
}

// 检查碰撞（墙壁和自身）
static bool check_collision(void) {
    // 检查墙壁碰撞
    if (snake[0].x < 0 || snake[0].x >= GRID_COLS ||
        snake[0].y < 0 || snake[0].y >= GRID_ROWS) {
        return true;
    }
    
    // 检查自身碰撞
    for (int i = 1; i < snake_length; i++) {
        if (snake[0].x == snake[i].x && snake[0].y == snake[i].y) {
            return true;
        }
    }
    
    return false;
}

// 检查食物碰撞
static bool check_food_collision(void) {
    return (snake[0].x == food.x && snake[0].y == food.y);
}

// 移动蛇
static void move_snake(void) {
    // 更新方向
    current_dir = next_dir;
    
    // 保存尾部位置
    snake_node_t tail = snake[snake_length - 1];
    
    // 移动身体（从尾部往前移动）
    for (int i = snake_length - 1; i > 0; i--) {
        snake[i] = snake[i - 1];
    }
    
    // 移动头部
    switch (current_dir) {
        case DIR_UP:
            snake[0].y--;
            break;
        case DIR_DOWN:
            snake[0].y++;
            break;
        case DIR_LEFT:
            snake[0].x--;
            break;
        case DIR_RIGHT:
            snake[0].x++;
            break;
    }
    
    // 检查是否吃到食物
    if (check_food_collision()) {
        score += 10;
        snake_length++;
        snake[snake_length - 1] = tail;
        generate_food();
        
        // 加速游戏
        if (game_speed > 80) {
            game_speed -= 5;
            if (game_timer) {
                lv_timer_set_period(game_timer, game_speed);
            }
        }
        
        // 更新分数
        if (score_label) {
            lv_label_set_text_fmt(score_label, "Score: %d", score);
        }
    }
}

// 处理输入
static void input_handler(void) {
    key_dir_t keys = key_scan();
    
    // 防止反向移动
    if (keys & KEY_UP && current_dir != DIR_DOWN) {
        next_dir = DIR_UP;
    }
    else if (keys & KEY_DOWN && current_dir != DIR_UP) {
        next_dir = DIR_DOWN;
    }
    else if (keys & KEY_LEFT && current_dir != DIR_RIGHT) {
        next_dir = DIR_LEFT;
    }
    else if (keys & KEY_RIGHT && current_dir != DIR_LEFT) {
        next_dir = DIR_RIGHT;
    }
}

// 绘制游戏
static void draw_game(void) {
    if (!canvas || !canvas_buf) return;
    
    // 清空画布
    lv_canvas_fill_bg(canvas, lv_color_hex(0x000000), LV_OPA_COVER);
    
    // 绘制网格（可选）
    for (int i = 0; i <= GRID_COLS; i++) {
        lv_canvas_draw_line(canvas, 
                           (lv_point_t[]){
                               {i * CELL_SIZE, 0},
                               {i * CELL_SIZE, GAME_HEIGHT}
                           }, 2,
                           &(lv_draw_line_dsc_t){
                               .color = lv_color_hex(0x1a1a1a),
                               .width = 1
                           });
    }
    for (int i = 0; i <= GRID_ROWS; i++) {
        lv_canvas_draw_line(canvas,
                           (lv_point_t[]){
                               {0, i * CELL_SIZE},
                               {GAME_WIDTH, i * CELL_SIZE}
                           }, 2,
                           &(lv_draw_line_dsc_t){
                               .color = lv_color_hex(0x1a1a1a),
                               .width = 1
                           });
    }
    
    // 绘制食物
    lv_draw_rect_dsc_t food_dsc;
    lv_draw_rect_dsc_init(&food_dsc);
    food_dsc.bg_color = lv_color_hex(0xFF0000);
    food_dsc.radius = 2;
    lv_canvas_draw_rect(canvas,
                       food.x * CELL_SIZE + 1,
                       food.y * CELL_SIZE + 1,
                       CELL_SIZE - 2,
                       CELL_SIZE - 2,
                       &food_dsc);
    
    // 绘制蛇
    for (int i = 0; i < snake_length; i++) {
        lv_draw_rect_dsc_t snake_dsc;
        lv_draw_rect_dsc_init(&snake_dsc);
        
        if (i == 0) {
            // 蛇头（亮绿色）
            snake_dsc.bg_color = lv_color_hex(0x00FF00);
        } else {
            // 蛇身（深绿色）
            snake_dsc.bg_color = lv_color_hex(0x00AA00);
        }
        snake_dsc.radius = 1;
        
        lv_canvas_draw_rect(canvas,
                           snake[i].x * CELL_SIZE + 1,
                           snake[i].y * CELL_SIZE + 1,
                           CELL_SIZE - 2,
                           CELL_SIZE - 2,
                           &snake_dsc);
    }
}

// 游戏主循环
static void game_loop(lv_timer_t* timer) {
    if (!game_started) return;
    
    if (game_over) {
        // 显示游戏结束
        lv_obj_t* game_over_label = lv_label_create(game_container);
        lv_label_set_text_fmt(game_over_label, "GAME OVER!\nScore: %d\n\nPress OK to restart", score);
        lv_obj_set_style_text_color(game_over_label, lv_color_hex(0xFF0000), LV_PART_MAIN);
        lv_obj_set_style_text_font(game_over_label, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_align(game_over_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_bg_color(game_over_label, lv_color_hex(0x000000), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(game_over_label, LV_OPA_80, LV_PART_MAIN);
        lv_obj_set_style_pad_all(game_over_label, 10, 0);
        lv_obj_center(game_over_label);
        
        if (game_timer) {
            lv_timer_del(game_timer);
            game_timer = NULL;
        }
        game_started = false;
        return;
    }
    
    // 处理输入
    input_handler();
    
    // 移动蛇
    move_snake();
    
    // 检查碰撞
    if (check_collision()) {
        game_over = true;
        return;
    }
    
    // 绘制游戏
    draw_game();
}

// 开始游戏回调
static void start_game_cb(lv_event_t* e) {
    lv_obj_t* btn = lv_event_get_target(e);
    
    // 如果是重新开始，先清理
    if (game_started || game_over) {
        if (canvas) {
            lv_obj_del(canvas);
            canvas = NULL;
        }
        if (canvas_buf) {
            lv_mem_free(canvas_buf);
            canvas_buf = NULL;
        }
    }
    
    // 删除按钮
    lv_obj_del(btn);
    
    // 创建画布
    canvas_buf = lv_mem_alloc(LV_CANVAS_BUF_SIZE_TRUE_COLOR(GAME_WIDTH, GAME_HEIGHT));
    if (canvas_buf) {
        canvas = lv_canvas_create(game_container);
        lv_canvas_set_buffer(canvas, canvas_buf, GAME_WIDTH, GAME_HEIGHT, LV_IMG_CF_TRUE_COLOR);
        lv_obj_set_pos(canvas, GAME_AREA_X, GAME_AREA_Y);
        lv_canvas_fill_bg(canvas, lv_color_hex(0x000000), LV_OPA_COVER);
    }
    
    // 创建分数标签
    if (!score_label) {
        score_label = lv_label_create(game_container);
        lv_obj_set_pos(score_label, GAME_AREA_X, GAME_AREA_Y + GAME_HEIGHT + 5);
        lv_obj_set_style_text_color(score_label, lv_color_hex(0xFFFF00), LV_PART_MAIN);
        lv_obj_set_style_text_font(score_label, &lv_font_montserrat_14, 0);
    }
    
    // 初始化游戏
    game_init();
    
    // 创建游戏定时器
    if (!game_timer) {
        game_timer = lv_timer_create(game_loop, game_speed, NULL);
    } else {
        lv_timer_resume(game_timer);
        lv_timer_set_period(game_timer, game_speed);
    }
}

// 清理游戏资源
static void cleanup_game(void) {
    if (game_timer) {
        lv_timer_del(game_timer);
        game_timer = NULL;
    }
    
    if (canvas_buf) {
        lv_mem_free(canvas_buf);
        canvas_buf = NULL;
    }
    
    game_started = false;
    game_over = false;
}

// 主创建函数
void ui_snake_create(lv_obj_t* parent) {
    // 应用当前主题到屏幕
    theme_apply_to_screen(parent);

    // 1. 创建页面父级容器
    lv_obj_t* page_parent_container;
    ui_create_page_parent_container(parent, &page_parent_container);

    // 2. 创建顶部栏
    lv_obj_t* top_bar_container;
    lv_obj_t* title_container;
    ui_create_top_bar(page_parent_container, "Snake Game", false, 
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
    
    // 添加游戏说明
    lv_obj_t* info_label = lv_label_create(content_container);
    lv_label_set_text(info_label, "Use arrow keys to control");
    lv_obj_set_style_text_color(info_label, lv_color_hex(0x888888), LV_PART_MAIN);
    lv_obj_set_style_text_font(info_label, &lv_font_montserrat_12, 0);
    lv_obj_align(info_label, LV_ALIGN_CENTER, 0, 40);
}