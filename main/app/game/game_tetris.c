#include "esp_log.h"
#include "esp_random.h"
#include "game.h"
#include "key.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "theme_manager.h"
#include "ui.h"
#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"

// --- NVS 常量 ---
#define NVS_NAMESPACE "tetris_hs"
#define HIGH_SCORE_KEY "high_scores"
#define NUM_HIGH_SCORES 5

// --- 游戏常量 ---
#define BOARD_WIDTH 10
#define BOARD_HEIGHT 20
#define BLOCK_SIZE 11  // 方块像素大小
#define BORDER_WIDTH 1 // 方块边框宽度

// --- LVGL 对象 ---
static lv_obj_t* canvas;
static lv_obj_t* next_canvas; // 下一个方块显示画布
static lv_obj_t* score_label;
static lv_obj_t* level_label;
static lv_timer_t* game_tick_timer = NULL;
static lv_timer_t* input_timer = NULL;

// --- 动态分配的画布缓冲区 ---
static lv_color_t* main_canvas_buf = NULL;
static lv_color_t* next_canvas_buf = NULL;

// --- 游戏状态 ---
static uint8_t board[BOARD_HEIGHT][BOARD_WIDTH];
static bool game_over = false;
static int score = 0;
static int total_lines_cleared = 0;
static int high_scores[NUM_HIGH_SCORES] = {0};

// 定义俄罗斯方块的形状和颜色
typedef struct {
    uint8_t shape[4][4];
    lv_color_t color; // This is now unused but kept for struct compatibility
} Tetromino;

// --- 新增：颜色对结构体，用于实现3D效果 ---
typedef struct {
    lv_color_t outer;
    lv_color_t inner;
} TetrisColor;

// 当前下落的方块
static struct {
    int x, y;
    const Tetromino* p_tetromino;
    uint8_t shape[4][4];
    uint8_t color_index;
    TetrisColor color;      // 当前方块实际颜色(颜色对)
    int rotation;           // 旋转状态: 0, 1, 2, 3
} current_piece;

// 下一个方块
static struct {
    const Tetromino* p_tetromino;
    uint8_t shape[4][4];
    uint8_t color_index;
    TetrisColor color;      // 预览显示及实际使用的颜色(颜色对)
} next_piece;

// --- 更新：使用颜色对的调色板 ---
static const TetrisColor PIECE_COLORS[] = {
    {LV_COLOR_MAKE(0xD3, 0x2F, 0x2F), LV_COLOR_MAKE(0xFF, 0x52, 0x52)}, // 红
    {LV_COLOR_MAKE(0x30, 0x3F, 0x9F), LV_COLOR_MAKE(0x53, 0x6D, 0xF3)}, // 蓝
    {LV_COLOR_MAKE(0x38, 0x8E, 0x3C), LV_COLOR_MAKE(0x4C, 0xAF, 0x50)}, // 绿
    {LV_COLOR_MAKE(0xF5, 0x7C, 0x00), LV_COLOR_MAKE(0xFF, 0x98, 0x00)}, // 橙
    {LV_COLOR_MAKE(0x7B, 0x1F, 0xA2), LV_COLOR_MAKE(0x9C, 0x27, 0xB0)}, // 紫
    {LV_COLOR_MAKE(0x00, 0x79, 0x6B), LV_COLOR_MAKE(0x00, 0x96, 0x88)}, // 青
    {LV_COLOR_MAKE(0xF9, 0xA8, 0x25), LV_COLOR_MAKE(0xFF, 0xEB, 0x3B)}, // 黄
};
#define PIECE_COLORS_COUNT (sizeof(PIECE_COLORS) / sizeof(PIECE_COLORS[0]))

// 全局颜色索引，用于在调色板中滚动选择颜色
static int g_color_index = -1;

// 每种方块类型（I,O,T,L,J,S,Z）独立维护一个颜色索引，初始为0xFF表示下一次从0开始
static uint8_t type_to_color_index[7];

// --- NVS (非易失性存储) 函数 ---

// 清理游戏资源
static void cleanup_game_resources() {
    if (main_canvas_buf) {
        heap_caps_free(main_canvas_buf);
        main_canvas_buf = NULL;
    }
    if (next_canvas_buf) {
        heap_caps_free(next_canvas_buf);
        next_canvas_buf = NULL;
    }
}

// 从 NVS 读取最高分
static void read_high_scores() {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE("NVS", "Error (%s) opening NVS handle!", esp_err_to_name(err));
        return;
    }

    size_t required_size = sizeof(high_scores);
    err = nvs_get_blob(nvs_handle, HIGH_SCORE_KEY, high_scores, &required_size);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE("NVS", "Error (%s) reading high scores!", esp_err_to_name(err));
    }
    nvs_close(nvs_handle);
}

// 将最高分写入 NVS
static void write_high_scores() {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE("NVS", "Error (%s) opening NVS handle!", esp_err_to_name(err));
        return;
    }

    err = nvs_set_blob(nvs_handle, HIGH_SCORE_KEY, high_scores, sizeof(high_scores));
    if (err != ESP_OK) {
        ESP_LOGE("NVS", "Error (%s) writing high scores!", esp_err_to_name(err));
    }

    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE("NVS", "Error (%s) committing updates to NVS!", esp_err_to_name(err));
    }
    nvs_close(nvs_handle);
}

// 检查并更新最高分
static void update_high_scores(int current_score) {
    int i, j;
    for (i = 0; i < NUM_HIGH_SCORES; i++) {
        if (current_score > high_scores[i]) {
            // 发现新高分，将其他分数后移
            for (j = NUM_HIGH_SCORES - 1; j > i; j--) {
                high_scores[j] = high_scores[j - 1];
            }
            high_scores[i] = current_score;
            write_high_scores(); // 保存新高分榜
            break;
        }
    }
}

// 7种俄罗斯方块 (I, O, T, L, J, S, Z)
static const Tetromino tetrominos[] = {
    {.shape = {{0, 0, 0, 0}, {1, 1, 1, 1}, {0, 0, 0, 0}, {0, 0, 0, 0}},
     .color = LV_COLOR_MAKE(0x00, 0xFF, 0xFF)}, // I (青色)
    {.shape = {{0, 1, 1, 0}, {0, 1, 1, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}},
     .color = LV_COLOR_MAKE(0xFF, 0xFF, 0x00)}, // O (黄色)
    {.shape = {{0, 1, 0, 0}, {1, 1, 1, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}},
     .color = LV_COLOR_MAKE(0x80, 0x00, 0x80)}, // T (紫色)
    {.shape = {{1, 0, 0, 0}, {1, 1, 1, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}},
     .color = LV_COLOR_MAKE(0xFF, 0xA5, 0x00)}, // L (橙色)
    {.shape = {{0, 0, 1, 0}, {1, 1, 1, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}},
     .color = LV_COLOR_MAKE(0x00, 0x00, 0xFF)}, // J (蓝色)
    {.shape = {{1, 1, 0, 0}, {0, 1, 1, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}},
     .color = LV_COLOR_MAKE(0x00, 0xFF, 0x00)}, // S (绿色)
    {.shape = {{0, 1, 1, 0}, {1, 1, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}},
     .color = LV_COLOR_MAKE(0xFF, 0x00, 0x00)}, // Z (红色)
};

// --- 新增: SRS踢墙数据表 ---
// J, L, S, T, Z 方块的踢墙数据
static const int8_t JLSTZ_WALL_KICK_DATA[4][5][2] = {
    {{0, 0}, {-1, 0}, {-1, 1}, {0, -2}, {-1, -2}}, // 0 -> 1
    {{0, 0}, {1, 0}, {1, -1}, {0, 2}, {1, 2}},   // 1 -> 2
    {{0, 0}, {1, 0}, {1, 1}, {0, -2}, {1, -2}},   // 2 -> 3
    {{0, 0}, {-1, 0}, {-1, -1}, {0, 2}, {-1, 2}}, // 3 -> 0
};
// I 方块的踢墙数据
static const int8_t I_WALL_KICK_DATA[4][5][2] = {
    {{0, 0}, {-2, 0}, {1, 0}, {-2, -1}, {1, 2}},   // 0 -> 1
    {{0, 0}, {-1, 0}, {2, 0}, {-1, 2}, {2, -1}},  // 1 -> 2
    {{0, 0}, {2, 0}, {-1, 0}, {2, 1}, {-1, -2}},  // 2 -> 3
    {{0, 0}, {1, 0}, {-2, 0}, {1, -2}, {-2, 1}},  // 3 -> 0
};

// --- 函数声明 ---
static void game_init(void);
static void draw_board(void);
static void draw_next_piece(void);
static void game_tick(lv_timer_t* timer);
static void input_handler_cb(lv_timer_t* timer);

// --- 核心游戏逻辑 ---

// 碰撞检测
static bool check_collision(int new_x, int new_y, uint8_t piece_shape[4][4]) {
    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
            if (piece_shape[y][x]) {
                int board_x = new_x + x;
                int board_y = new_y + y;

                // 撞墙或落地
                if (board_x < 0 || board_x >= BOARD_WIDTH || board_y >= BOARD_HEIGHT) {
                    return true;
                }

                // 撞到其他方块
                if (board_y >= 0 && board[board_y][board_x]) {
                    return true;
                }
            }
        }
    }
    return false;
}

// 生成下一个方块
static void generate_next_piece() {
    const Tetromino* new_tetromino;
    // 确保下一个方块与当前方块类型不同
    do {
        new_tetromino = &tetrominos[esp_random() % (sizeof(tetrominos) / sizeof(Tetromino))];
    } while (current_piece.p_tetromino != NULL && new_tetromino == current_piece.p_tetromino);
    next_piece.p_tetromino = new_tetromino;

    memcpy(next_piece.shape, next_piece.p_tetromino->shape, 16); // 4x4=16
    // 全局滚动颜色
    g_color_index = (g_color_index + 1) % PIECE_COLORS_COUNT;
    next_piece.color_index = g_color_index;
    next_piece.color = PIECE_COLORS[g_color_index];
}

// 生成新方块
static void spawn_new_piece() {
    // 如果还没有下一个方块，先生成一个
    if (next_piece.p_tetromino == NULL) {
        generate_next_piece();
    }

    // 将下一个方块设为当前方块
    current_piece.p_tetromino = next_piece.p_tetromino;
    memcpy(current_piece.shape, next_piece.shape, 16);
    current_piece.x = BOARD_WIDTH / 2 - 2;
    current_piece.y = 0;
    current_piece.rotation = 0; // 初始化旋转状态
    current_piece.color_index = next_piece.color_index;
    current_piece.color = next_piece.color;

    // 提交本类型的颜色索引，确保同类型的下一次颜色继续滚动
    int curr_type_index = (int)(current_piece.p_tetromino - tetrominos);
    type_to_color_index[curr_type_index] = current_piece.color_index;

    // 生成新的下一个方块
    generate_next_piece();

    // 如果新生成的方块直接就碰撞了，说明游戏结束
    if (check_collision(current_piece.x, current_piece.y, current_piece.shape)) {
        game_over = true;
    }
}

// 锁定方块到游戏区域
static void lock_piece() {
    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
            if (current_piece.shape[y][x]) {
                int board_x = current_piece.x + x;
                int board_y = current_piece.y + y;
                if (board_y >= 0) {
                // 在board中记录颜色索引+1（0表示空）
                board[board_y][board_x] = (uint8_t)(current_piece.color_index + 1);
                }
            }
        }
    }
}

// 消除满行
static void clear_lines() {
    int lines_cleared_this_turn = 0;
    for (int y = BOARD_HEIGHT - 1; y >= 0; y--) {
        bool line_full = true;
        for (int x = 0; x < BOARD_WIDTH; x++) {
            if (board[y][x] == 0) {
                line_full = false;
                break;
            }
        }

        if (line_full) {
            lines_cleared_this_turn++;
            for (int k = y; k > 0; k--) {
                memcpy(board[k], board[k - 1], BOARD_WIDTH * sizeof(uint8_t));
            }
            memset(board[0], 0, BOARD_WIDTH * sizeof(uint8_t));
            y++; // 重新检查当前行
        }
    }

    if (lines_cleared_this_turn > 0) {
        // 更新分数
        score += lines_cleared_this_turn * 100 * lines_cleared_this_turn;
        lv_label_set_text_fmt(score_label, "Score:\n%d", score);

        // 更新总消行数并检查是否升级
        total_lines_cleared += lines_cleared_this_turn;
        int current_level = (total_lines_cleared / 10) + 1;
        lv_label_set_text_fmt(level_label, "Level:\n%d", current_level);

        // 根据等级调整游戏速度
        uint32_t new_period = 500 - (current_level - 1) * 40;
        if (new_period < 100) { // 设置最快速度
            new_period = 100;
        }
        lv_timer_set_period(game_tick_timer, new_period);
    }
}

// --- 绘图函数 ---

// 绘制单个方块 (更新为3D效果)
static void draw_block(int x, int y, TetrisColor color) {
    lv_draw_rect_dsc_t rect_dsc;
    lv_draw_rect_dsc_init(&rect_dsc);
    rect_dsc.border_width = 1;
    rect_dsc.border_color = lv_color_black();
    rect_dsc.radius = 2;

    // 1. 绘制外框色
    rect_dsc.bg_color = color.outer;
    lv_canvas_draw_rect(canvas, x * BLOCK_SIZE, y * BLOCK_SIZE, BLOCK_SIZE, BLOCK_SIZE, &rect_dsc);

    // 2. 在上方绘制内芯色，形成立体效果
    rect_dsc.bg_color = color.inner;
    rect_dsc.border_width = 0;
    lv_canvas_draw_rect(canvas, x * BLOCK_SIZE + 2, y * BLOCK_SIZE + 1, BLOCK_SIZE - 4, BLOCK_SIZE - 3, &rect_dsc);
}

// 绘制下一个方块 (更新为3D效果)
static void draw_next_piece() {
    if (!next_canvas || !next_piece.p_tetromino)
        return;

    // 用背景色清空画布
    lv_canvas_fill_bg(next_canvas, lv_color_hex(0xcccccc), LV_OPA_COVER);

    // 计算下一个方块在画布中的居中位置
    int offset_x = 0; // 4x4方块在4x4画布中的偏移
    int offset_y = 0;

    lv_draw_rect_dsc_t rect_dsc;
    lv_draw_rect_dsc_init(&rect_dsc);
    rect_dsc.radius = 2;

    // 绘制下一个方块
    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
            if (next_piece.shape[y][x]) {
                int draw_x = (offset_x + x) * BLOCK_SIZE;
                int draw_y = (offset_y + y) * BLOCK_SIZE;

                // 1. 绘制外框色
                rect_dsc.bg_color = next_piece.color.outer;
                rect_dsc.border_width = 1;
                rect_dsc.border_color = lv_color_black();
                lv_canvas_draw_rect(next_canvas, draw_x, draw_y, BLOCK_SIZE, BLOCK_SIZE, &rect_dsc);
                
                // 2. 绘制内芯色
                rect_dsc.bg_color = next_piece.color.inner;
                rect_dsc.border_width = 0;
                lv_canvas_draw_rect(next_canvas, draw_x + 2, draw_y + 1, BLOCK_SIZE - 4, BLOCK_SIZE - 3, &rect_dsc);
            }
        }
    }
}

// 绘制整个游戏区域
static void draw_board() {
    // 用背景色清空画布
    lv_canvas_fill_bg(canvas, lv_color_hex(0xcccccc), LV_OPA_COVER);

    // 绘制已经固定的方块
    for (int y = 0; y < BOARD_HEIGHT; y++) {
        for (int x = 0; x < BOARD_WIDTH; x++) {
            if (board[y][x]) {
                draw_block(x, y, PIECE_COLORS[board[y][x] - 1]);
            }
        }
    }

    // 绘制当前下落的方块
    if (!game_over) {
        for (int y = 0; y < 4; y++) {
            for (int x = 0; x < 4; x++) {
                if (current_piece.shape[y][x]) {
                    draw_block(current_piece.x + x, current_piece.y + y, current_piece.color);
                }
            }
        }
    }

    // 更新下一个方块显示
    draw_next_piece();
}

// --- 玩家动作 ---

// 更新为带踢墙逻辑的SRS旋转
static void tetris_rotate(void) {
    if (game_over)
        return;

    // 1. 创建临时旋转后的形状
    uint8_t temp_shape[4][4] = {0};
    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
            temp_shape[x][3 - y] = current_piece.shape[y][x];
        }
    }

    int original_rotation = current_piece.rotation;
    int new_rotation = (original_rotation + 1) % 4;
    const int8_t (*kick_table)[2];

    // 2. 根据方块类型选择踢墙数据表 (I型方块是tetrominos数组的第一个)
    if (current_piece.p_tetromino == &tetrominos[0]) {
        kick_table = I_WALL_KICK_DATA[original_rotation];
    } else {
        kick_table = JLSTZ_WALL_KICK_DATA[original_rotation];
    }

    // 3. 遍历5个踢墙测试点
    for (int i = 0; i < 5; i++) {
        // 偏移量(x, y) - y轴在LVGL和参考代码中都是向下为正，但SRS标准表通常假设y向上为正。
        // 参考代码中顺时针旋转是 y - offset, 我们也遵循此惯例。
        int test_x = current_piece.x + kick_table[i][0];
        int test_y = current_piece.y - kick_table[i][1];

        if (!check_collision(test_x, test_y, temp_shape)) {
            // 找到一个可行的位置
            current_piece.x = test_x;
            current_piece.y = test_y;
            memcpy(current_piece.shape, temp_shape, sizeof(temp_shape));
            current_piece.rotation = new_rotation;
            draw_board();
            return; // 旋转成功，退出函数
        }
    }
    // 所有5个测试点都失败，旋转无法进行
}

static void tetris_move_left(void) {
    if (game_over)
        return;
    if (!check_collision(current_piece.x - 1, current_piece.y, current_piece.shape)) {
        current_piece.x--;
        draw_board();
    }
}

static void tetris_move_right(void) {
    if (game_over)
        return;
    if (!check_collision(current_piece.x + 1, current_piece.y, current_piece.shape)) {
        current_piece.x++;
        draw_board();
    }
}

static void tetris_soft_drop(void) {
    if (game_over)
        return;
    if (!check_collision(current_piece.x, current_piece.y + 1, current_piece.shape)) {
        current_piece.y++;
        draw_board();
    } else {
        lock_piece();
        clear_lines();
        spawn_new_piece();
        draw_board();
    }
}

static void tetris_hard_drop(void) {
    if (game_over)
        return;
    while (!check_collision(current_piece.x, current_piece.y + 1, current_piece.shape)) {
        current_piece.y++;
    }
    lock_piece();
    clear_lines();
    spawn_new_piece();
    draw_board();
}

// --- 定时器和回调 ---

// 游戏主循环，由定时器驱动
static void game_tick(lv_timer_t* timer) {
    if (game_over) {
        // 更新并保存最高分
        update_high_scores(score);

        lv_obj_t* label = lv_label_create(canvas);
        lv_label_set_text(label, "GAME OVER");
        lv_obj_set_style_text_color(label, lv_color_white(), 0);
        lv_obj_set_style_bg_color(label, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(label, LV_OPA_50, 0);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
        lv_obj_center(label);

        // 游戏结束，删除定时器
        if (game_tick_timer) {
            lv_timer_del(game_tick_timer);
            game_tick_timer = NULL;
        }
        return;
    }
    tetris_soft_drop(); // 方块自动下落一格
}

// 按键输入处理
static void input_handler_cb(lv_timer_t* timer) {
    static uint32_t last_key_time = 0;
    static bool is_down_pressed = false;
    static uint32_t down_press_start_time = 0;
    static uint32_t last_soft_drop_time = 0;
    static bool hard_drop_triggered = false;

    key_dir_t keys = key_scan();

    // --- 处理上、左、右键 ---
    // 使用消抖/重复延迟来处理这些按键
    if (lv_tick_elaps(last_key_time) > 150) {
        if (keys & KEY_UP) {
            tetris_rotate();
            last_key_time = lv_tick_get();
        } else if (keys & KEY_LEFT) {
            tetris_move_left();
            last_key_time = lv_tick_get();
        } else if (keys & KEY_RIGHT) {
            tetris_move_right();
            last_key_time = lv_tick_get();
        }
    }

    // --- 处理下键的长按和短按 ---
    if (keys & KEY_DOWN) {
        if (!is_down_pressed) {
            // 首次按下
            is_down_pressed = true;
            hard_drop_triggered = false;
            down_press_start_time = lv_tick_get();
            last_soft_drop_time = lv_tick_get();
            tetris_soft_drop(); // 立即软着陆
        } else {
            // 按键被按住 - 实现加速下落
            uint32_t current_time = lv_tick_get();
            uint32_t press_duration = lv_tick_elaps(down_press_start_time);

            if (!hard_drop_triggered && press_duration > 800) { // 800ms 触发硬着陆
                tetris_hard_drop();
                hard_drop_triggered = true;    // 确保每次长按只触发一次
            } else if (press_duration > 200) { // 200ms后开始加速下落
                // 加速下落：每100ms下落一格
                if (lv_tick_elaps(last_soft_drop_time) > 100) {
                    tetris_soft_drop();
                    last_soft_drop_time = current_time;
                }
            }
        }
    } else {
        // 按键被释放
        is_down_pressed = false;
    }
}

// 游戏初始化
static void game_init(void) {
    memset(board, 0, sizeof(board));
    game_over = false;
    score = 0;
    total_lines_cleared = 0;

    // 初始化下一个方块并重置全局颜色索引
    next_piece.p_tetromino = NULL;
    g_color_index = -1; // 重置颜色索引，使第一个方块颜色为PIECE_COLORS[0]

    if (score_label) {
        lv_label_set_text_fmt(score_label, "Score:\n%d", score);
    }
    if (level_label) {
        lv_label_set_text_fmt(level_label, "Level:\n%d", 1);
    }
    if (game_tick_timer) {
        lv_timer_set_period(game_tick_timer, 500); // 重置为初始速度
    }

    spawn_new_piece();
}

// Forward declaration
static void ui_tetris_menu_create(lv_obj_t* parent);

// --- 游戏UI创建及页面管理 ---

// 返回俄罗斯方块主菜单
static void back_to_tetris_menu_from_scoreboard(lv_event_t* e) {
    lv_obj_t* parent = lv_event_get_user_data(e);
    if (parent) {
        lv_obj_clean(parent);
        ui_tetris_menu_create(parent); // 返回到俄罗斯方块菜单
    }
}

// 创建排行榜界面
static void ui_scoreboard_create(lv_obj_t* parent) {
    lv_obj_clean(parent);

    // 设置父容器为不可滚动
    lv_obj_set_scroll_dir(parent, LV_DIR_NONE);

    lv_obj_t* cont = lv_obj_create(parent);
    lv_obj_center(cont);
    lv_obj_set_size(cont, 180, 240); // 设置容器大小，确保能容纳所有内容
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(cont, 20, 0);
    lv_obj_set_style_pad_row(cont, 10, 0);

    lv_obj_t* title = lv_label_create(cont);
    lv_label_set_text(title, "High Scores");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0); // 增大字体
    lv_obj_set_style_pad_bottom(title, 20, 0);

    // 显示分数
    for (int i = 0; i < NUM_HIGH_SCORES; i++) {
        lv_obj_t* score_entry = lv_label_create(cont);
        if (high_scores[i] > 0) {
            lv_label_set_text_fmt(score_entry, "%d. %d", i + 1, high_scores[i]);
        } else {
            lv_label_set_text_fmt(score_entry, "%d. ---", i + 1);
        }
        lv_obj_set_style_text_font(score_entry, &lv_font_montserrat_16, 0); // 增大字体
        lv_obj_set_style_pad_bottom(score_entry, 5, 0);
    }

    // 返回按钮
    lv_obj_t* btn = lv_btn_create(cont);
    lv_obj_add_event_cb(btn, back_to_tetris_menu_from_scoreboard, LV_EVENT_CLICKED, parent);
    lv_obj_set_size(btn, 150, 45);                              // 设置按钮大小
    lv_obj_set_style_text_font(btn, &lv_font_montserrat_14, 0); // 设置按钮字体
    lv_obj_set_style_pad_top(btn, 20, 0);
    lv_obj_t* label = lv_label_create(btn);
    lv_label_set_text(label, LV_SYMBOL_LEFT " Back");
    lv_obj_center(label);
}

// 显示排行榜的回调
static void show_scoreboard_cb(lv_event_t* e) {
    lv_obj_t* parent = lv_event_get_user_data(e);
    ui_scoreboard_create(parent);
}

// 返回俄罗斯方块主菜单
static void back_to_tetris_menu(lv_event_t* e) {
    // 删除游戏定时器，释放资源
    if (game_tick_timer) {
        lv_timer_del(game_tick_timer);
        game_tick_timer = NULL;
    }
    if (input_timer) {
        lv_timer_del(input_timer);
        input_timer = NULL;
    }

    // 清理游戏状态
    next_piece.p_tetromino = NULL;

    lv_obj_t* parent = lv_event_get_user_data(e);
    if (parent) {
        lv_obj_clean(parent);
        ui_tetris_menu_create(parent); // 返回到俄罗斯方块菜单
    }
}

// 启动实际的游戏界面
static void start_game_cb(lv_event_t* e) {
    lv_obj_t* parent = lv_event_get_user_data(e);
    lv_obj_clean(parent);

    // 动态分配画布缓冲区到PSRAM
    if (main_canvas_buf == NULL) {
        size_t main_buf_size = LV_CANVAS_BUF_SIZE_TRUE_COLOR(BOARD_WIDTH * BLOCK_SIZE, BOARD_HEIGHT * BLOCK_SIZE);
        main_canvas_buf = (lv_color_t*)heap_caps_malloc(main_buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!main_canvas_buf) {
            // 如果PSRAM分配失败，尝试内部RAM
            main_canvas_buf = (lv_color_t*)malloc(main_buf_size);
        }
    }
    
    if (next_canvas_buf == NULL) {
        size_t next_buf_size = LV_CANVAS_BUF_SIZE_TRUE_COLOR(4 * BLOCK_SIZE, 4 * BLOCK_SIZE);
        next_canvas_buf = (lv_color_t*)heap_caps_malloc(next_buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!next_canvas_buf) {
            next_canvas_buf = (lv_color_t*)malloc(next_buf_size);
        }
    }

    // 设置父容器为不可滚动
    lv_obj_set_scroll_dir(parent, LV_DIR_NONE);

    // 创建画布用于游戏区域
    canvas = lv_canvas_create(parent);
    lv_canvas_set_buffer(canvas, main_canvas_buf, BOARD_WIDTH * BLOCK_SIZE, BOARD_HEIGHT * BLOCK_SIZE, LV_IMG_CF_TRUE_COLOR);
    lv_obj_align(canvas, LV_ALIGN_LEFT_MID, 10, 0);

    // --- 右侧控制/信息面板 ---
    lv_obj_t* panel = lv_obj_create(parent);
    lv_obj_set_size(panel, 90, BOARD_HEIGHT * BLOCK_SIZE);
    lv_obj_align_to(panel, canvas, LV_ALIGN_OUT_RIGHT_MID, 5, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(panel, 5, 0);

    // 标题
    lv_obj_t* title = lv_label_create(panel);
    lv_label_set_text(title, "Tetris");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0); // 减小字体
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);

    // 分数标签
    score_label = lv_label_create(panel);
    lv_label_set_text_fmt(score_label, "Score:\n%d", score);
    lv_obj_set_style_text_align(score_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(score_label, &lv_font_montserrat_12, 0); // 减小字体

    // 等级标签
    level_label = lv_label_create(panel);
    lv_label_set_text_fmt(level_label, "Level:\n%d", 1);
    lv_obj_set_style_text_align(level_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(level_label, &lv_font_montserrat_12, 0); // 减小字体

    // 下一个方块标签
    lv_obj_t* next_label = lv_label_create(panel);
    lv_label_set_text(next_label, "Next:");
    lv_obj_set_style_text_align(next_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(next_label, &lv_font_montserrat_12, 0); // 减小字体

    // 下一个方块显示画布 - 使用动态分配的缓冲区
    next_canvas = lv_canvas_create(panel);
    lv_canvas_set_buffer(next_canvas, next_canvas_buf, 4 * BLOCK_SIZE, 4 * BLOCK_SIZE, LV_IMG_CF_TRUE_COLOR);
    lv_obj_set_size(next_canvas, 4 * BLOCK_SIZE, 4 * BLOCK_SIZE);

    // 返回按钮 (返回到俄罗斯方块菜单)
    lv_obj_t* btn = lv_btn_create(panel);
    lv_obj_add_event_cb(btn, back_to_tetris_menu, LV_EVENT_CLICKED, parent);
    lv_obj_set_size(btn, 70, 30); // 设置按钮大小
    lv_obj_t* btn_label = lv_label_create(btn);
    lv_label_set_text(btn_label, LV_SYMBOL_LEFT " Back");
    lv_obj_set_style_text_font(btn_label, &lv_font_montserrat_14, 0); // 减小字体
    lv_obj_center(btn_label);

    // --- 启动游戏 ---
    game_init();
    draw_board();

    // 创建游戏定时器
    game_tick_timer = lv_timer_create(game_tick, 500, NULL);   // 方块下落定时器
    input_timer = lv_timer_create(input_handler_cb, 50, NULL); // 按键扫描定时器
}

// 返回到APP主菜单 (游戏选择界面)
static void back_to_app_menu(lv_event_t* e) {
    lv_obj_t* parent = lv_event_get_user_data(e);
    if (parent) {
        cleanup_game_resources(); // 释放游戏资源
        lv_obj_clean(parent);
        ui_game_menu_create(parent); // 返回到最外层的游戏菜单
    }
}

// 创建俄罗斯方块主菜单
static void ui_tetris_menu_create(lv_obj_t* parent) {
    // 首次进入时，从NVS加载一次最高分
    read_high_scores();

    // 应用当前主题到屏幕
    theme_apply_to_screen(parent);

    // 设置父容器为不可滚动
    lv_obj_set_scroll_dir(parent, LV_DIR_NONE);

    // 1. 创建页面父级容器（统一管理整个页面）
    lv_obj_t* page_parent_container;
    ui_create_page_parent_container(parent, &page_parent_container);

    // 2. 创建顶部栏容器（包含返回按钮和标题）
    lv_obj_t* top_bar_container;
    lv_obj_t* title_container;
    ui_create_top_bar(page_parent_container, "Tetris", false, &top_bar_container, &title_container, NULL);

    // 替换顶部栏的返回按钮回调为自定义回调
    lv_obj_t* back_btn = lv_obj_get_child(top_bar_container, 0); // 获取返回按钮
    if (back_btn) {
        lv_obj_remove_event_cb(back_btn, NULL); // 移除默认回调
        lv_obj_add_event_cb(back_btn, back_to_app_menu, LV_EVENT_CLICKED, parent);
    }

    // 3. 创建页面内容容器
    lv_obj_t* content_container;
    ui_create_page_content_area(page_parent_container, &content_container);

    // 4. 在content_container中添加页面内容
    // 容器和布局
    lv_obj_t* cont = content_container;
    lv_obj_align(cont, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_size(cont, 180, 240); // 设置容器大小，确保能容纳所有按钮
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(cont, 20, 0);
    lv_obj_set_style_pad_row(cont, 15, 0);

    // 标题
    lv_obj_t* title = lv_label_create(cont);
    lv_label_set_text(title, "Tetris");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0); // 增大字体
    theme_apply_to_label(title, true);
    lv_obj_set_style_pad_bottom(title, 30, 0);

    // 菜单按钮
    lv_obj_t* btn;
    lv_obj_t* label;

    // 开始游戏
    btn = lv_btn_create(cont);
    lv_obj_add_event_cb(btn, start_game_cb, LV_EVENT_CLICKED, parent);
    theme_apply_to_button(btn, true);
    lv_obj_set_size(btn, 140, 30);                              // 设置按钮大小
    lv_obj_set_style_text_font(btn, &lv_font_montserrat_16, 0); // 设置按钮字体
    label = lv_label_create(btn);
    lv_label_set_text(label, "Start Game");
    lv_obj_center(label);

    // 积分榜
    btn = lv_btn_create(cont);
    lv_obj_add_event_cb(btn, show_scoreboard_cb, LV_EVENT_CLICKED, parent);
    theme_apply_to_button(btn, true);
    lv_obj_set_size(btn, 140, 30);                              // 设置按钮大小
    lv_obj_set_style_text_font(btn, &lv_font_montserrat_16, 0); // 设置按钮字体
    label = lv_label_create(btn);
    lv_label_set_text(label, "Scoreboard");
    lv_obj_center(label);

    // 帮助 (占位)
    btn = lv_btn_create(cont);
    theme_apply_to_button(btn, true);
    lv_obj_set_size(btn, 140, 30);                              // 设置按钮大小
    lv_obj_set_style_text_font(btn, &lv_font_montserrat_16, 0); // 设置按钮字体
    lv_obj_set_style_bg_opa(btn, LV_OPA_50, 0);                 // 半透明表示禁用
    label = lv_label_create(btn);
    lv_label_set_text(label, "Help");
    lv_obj_center(label);
    lv_obj_add_state(btn, LV_STATE_DISABLED); // 暂时禁用

    // 设置 (占位)
    btn = lv_btn_create(cont);
    theme_apply_to_button(btn, true);
    lv_obj_set_size(btn, 140, 30);                              // 设置按钮大小
    lv_obj_set_style_text_font(btn, &lv_font_montserrat_16, 0); // 设置按钮字体
    lv_obj_set_style_bg_opa(btn, LV_OPA_50, 0);                 // 半透明表示禁用
    label = lv_label_create(btn);
    lv_label_set_text(label, "Settings");
    lv_obj_center(label);
    lv_obj_add_state(btn, LV_STATE_DISABLED); // 暂时禁用

    // 退出 (返回到 APP 菜单)
    btn = lv_btn_create(cont);
    lv_obj_add_event_cb(btn, back_to_app_menu, LV_EVENT_CLICKED, parent);
    theme_apply_to_button(btn, true);
    lv_obj_set_size(btn, 140, 30);                              // 设置按钮大小
    lv_obj_set_style_text_font(btn, &lv_font_montserrat_16, 0); // 设置按钮字体
    label = lv_label_create(btn);
    lv_label_set_text(label, "Exit");
    lv_obj_center(label);
}

// 总入口: 当从APP菜单选择俄罗斯方块时调用此函数
void ui_tetris_create(lv_obj_t* parent) { ui_tetris_menu_create(parent); }
