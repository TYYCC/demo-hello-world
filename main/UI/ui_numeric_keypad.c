/**
 * @file ui_numeric_keypad.c
 * @brief Numeric keypad UI component for password input
 * @author Your Name
 * @date 2024
 */

#include "ui_numeric_keypad.h"
#include "theme_manager.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>

static const char* TAG = "UI_NUMERIC_KEYPAD";

// Numeric keypad data structure
typedef struct {
    lv_obj_t* container;
    lv_obj_t* password_display;
    lv_obj_t* title_label;
    char password[64];
    numeric_keypad_cb_t callback;
    void* user_data;
} keypad_data_t;


// Key labels
static const char* keypad_labels[] = {
    "1", "2", "3",
    "4", "5", "6", 
    "7", "8", "9",
    "DEL", "0", "OK"
};

static void keypad_button_cb(lv_event_t* e);
static void close_button_cb(lv_event_t* e);
static void update_password_display(keypad_data_t* data);

lv_obj_t* ui_numeric_keypad_create(lv_obj_t* parent, const char* title, 
                                   const char* current_password,
                                   numeric_keypad_cb_t callback, 
                                   void* user_data) {
    // Create modal background
    lv_obj_t* bg = lv_obj_create(parent);
    lv_obj_set_size(bg, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(bg, 0, 0);
    lv_obj_set_style_bg_color(bg, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(bg, LV_OPA_70, 0);
    lv_obj_set_style_border_width(bg, 0, 0);
    lv_obj_set_style_pad_all(bg, 0, 0);
    lv_obj_add_flag(bg, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_move_to_index(bg, -1); // Ensure displayed on top layer

    // Create main container - optimized for 240x320 screen
    lv_obj_t* container = lv_obj_create(bg);
    lv_obj_set_size(container, 220, 280);
    lv_obj_center(container);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(container, 8, 0);
    lv_obj_set_style_pad_gap(container, 6, 0);
    lv_obj_set_style_radius(container, 8, 0);
    lv_obj_set_style_shadow_width(container, 15, 0);
    lv_obj_set_style_shadow_opa(container, LV_OPA_50, 0);
    // Hide scroll bar
    lv_obj_set_style_width(container, 0, LV_PART_SCROLLBAR);
    lv_obj_set_style_opa(container, LV_OPA_0, LV_PART_SCROLLBAR);
    lv_obj_clear_flag(container, LV_OBJ_FLAG_SCROLLABLE);
    theme_apply_to_container(container);

    // Allocate data structure
    keypad_data_t* data = lv_mem_alloc(sizeof(keypad_data_t));
    memset(data, 0, sizeof(keypad_data_t));
    data->container = bg;
    data->callback = callback;
    data->user_data = user_data;
    
    if (current_password) {
        strncpy(data->password, current_password, sizeof(data->password) - 1);
    }

    lv_obj_set_user_data(bg, data);

    // Create title bar
    lv_obj_t* title_bar = lv_obj_create(container);
    lv_obj_set_width(title_bar, LV_PCT(100));
    lv_obj_set_height(title_bar, 35);
    lv_obj_set_flex_flow(title_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(title_bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(title_bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(title_bar, 0, 0);
    lv_obj_set_style_pad_all(title_bar, 5, 0);
    // Hide scroll bar
    lv_obj_set_style_width(title_bar, 0, LV_PART_SCROLLBAR);
    lv_obj_set_style_opa(title_bar, LV_OPA_0, LV_PART_SCROLLBAR);
    lv_obj_clear_flag(title_bar, LV_OBJ_FLAG_SCROLLABLE);

    // Title text
    data->title_label = lv_label_create(title_bar);
    lv_label_set_text(data->title_label, (title && title[0] != '\0') ? title : "AP Password");
    theme_apply_to_label(data->title_label, false);

    // Close button - fixed in top right
    lv_obj_t* close_btn = lv_btn_create(title_bar);
    lv_obj_set_size(close_btn, 30, 30);
    lv_obj_set_style_bg_color(close_btn, lv_palette_main(LV_PALETTE_RED), 0);
    lv_obj_set_style_radius(close_btn, 15, 0);
    theme_apply_to_button(close_btn, false);
    lv_obj_add_event_cb(close_btn, close_button_cb, LV_EVENT_CLICKED, data);

    lv_obj_t* close_label = lv_label_create(close_btn);
    lv_label_set_text(close_label, LV_SYMBOL_CLOSE);
    lv_obj_center(close_label);
    theme_apply_to_label(close_label, false);

    // Password display area
    lv_obj_t* password_container = lv_obj_create(container);
    lv_obj_set_width(password_container, LV_PCT(100));
    lv_obj_set_height(password_container, 50);
    lv_obj_set_style_pad_all(password_container, 4, 0);
    lv_obj_set_style_bg_opa(password_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(password_container, 1, 0);
    lv_obj_set_style_border_color(password_container, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_obj_set_style_radius(password_container, 5, 0);
    // Hide scroll bar
    lv_obj_set_style_width(password_container, 0, LV_PART_SCROLLBAR);
    lv_obj_set_style_opa(password_container, LV_OPA_0, LV_PART_SCROLLBAR);
    lv_obj_clear_flag(password_container, LV_OBJ_FLAG_SCROLLABLE);

    // Current password label
    if (current_password && strlen(current_password) > 0) {
        lv_obj_t* current_label = lv_label_create(password_container);
        lv_label_set_text_fmt(current_label, "Current: %s", current_password);
        lv_obj_align(current_label, LV_ALIGN_TOP_MID, 0, 5);
        theme_apply_to_label(current_label, false);
        lv_obj_set_style_text_color(current_label, lv_palette_main(LV_PALETTE_GREY), 0);
    }

    data->password_display = lv_label_create(password_container);
    lv_obj_align(data->password_display, LV_ALIGN_BOTTOM_MID, 0, -5);
    theme_apply_to_label(data->password_display, false);
    lv_obj_set_style_text_color(data->password_display, lv_palette_main(LV_PALETTE_BLUE), 0);
    update_password_display(data);

    // Create key grid
    lv_obj_t* keypad_grid = lv_obj_create(container);
    lv_obj_set_width(keypad_grid, LV_PCT(100));
    lv_obj_set_flex_grow(keypad_grid, 1);
    lv_obj_set_layout(keypad_grid, LV_LAYOUT_GRID);
    lv_obj_set_style_bg_opa(keypad_grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(keypad_grid, 0, 0);
    lv_obj_set_style_pad_all(keypad_grid, 5, 0);
    lv_obj_set_style_pad_gap(keypad_grid, 4, 0);
    // Hide scroll bar
    lv_obj_set_style_width(keypad_grid, 0, LV_PART_SCROLLBAR);
    lv_obj_set_style_opa(keypad_grid, LV_OPA_0, LV_PART_SCROLLBAR);
    lv_obj_clear_flag(keypad_grid, LV_OBJ_FLAG_SCROLLABLE);

    // Set grid template: 3 columns 4 rows, increase key spacing
    static lv_coord_t col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static lv_coord_t row_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    lv_obj_set_grid_dsc_array(keypad_grid, col_dsc, row_dsc);

    // Create keys
    for (int i = 0; i < 12; i++) {
        lv_obj_t* btn = lv_btn_create(keypad_grid);
        lv_obj_set_grid_cell(btn, LV_GRID_ALIGN_STRETCH, i % 3, 1, 
                             LV_GRID_ALIGN_STRETCH, i / 3, 1);
        lv_obj_set_style_pad_all(btn, 1, 0);
        lv_obj_set_style_min_height(btn, 40, 0);
        theme_apply_to_button(btn, false);

        lv_obj_t* label = lv_label_create(btn);
        lv_label_set_text(label, keypad_labels[i]);
        lv_obj_center(label);
        theme_apply_to_label(label, false);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);

        // Set different styles for special keys
        if (strcmp(keypad_labels[i], "OK") == 0) {
            lv_obj_set_style_bg_color(btn, lv_palette_main(LV_PALETTE_GREEN), 0);
            lv_obj_set_style_text_color(label, lv_color_white(), 0);
        } else if (strcmp(keypad_labels[i], "DEL") == 0) {
            lv_obj_set_style_bg_color(btn, lv_palette_main(LV_PALETTE_RED), 0);
            lv_obj_set_style_text_color(label, lv_color_white(), 0);
        }

        lv_obj_add_event_cb(btn, keypad_button_cb, LV_EVENT_CLICKED, data);
    }

    ESP_LOGI(TAG, "Numeric keypad created");
    return bg;
}

void ui_numeric_keypad_destroy(lv_obj_t* keypad) {
    if (!keypad) return;

    keypad_data_t* data = (keypad_data_t*)lv_obj_get_user_data(keypad);
    if (data) {
        lv_mem_free(data);
    }
    lv_obj_del(keypad);
}

static void keypad_button_cb(lv_event_t* e) {
    lv_obj_t* btn = lv_event_get_target(e);
    keypad_data_t* data = (keypad_data_t*)lv_event_get_user_data(e);
    
    lv_obj_t* label = lv_obj_get_child(btn, 0);
    const char* text = lv_label_get_text(label);

    if (strcmp(text, "DEL") == 0) {
        // Delete last character
        size_t len = strlen(data->password);
        if (len > 0) {
            data->password[len - 1] = '\0';
        }
    } else if (strcmp(text, "OK") == 0) {
        // Confirm password
        if (strlen(data->password) >= 8) {
            if (data->callback) {
                data->callback(data->password, data->user_data);
            }
            ui_numeric_keypad_destroy(data->container);
            return;
        } else {
            // 显示错误提示
            lv_obj_t* msgbox = lv_msgbox_create(lv_scr_act(), "Error", 
                                               "Password must be at least 8 digits", 
                                               NULL, true);
            lv_obj_center(msgbox);
        }
    } else if (strlen(text) == 1 && text[0] >= '0' && text[0] <= '9') {
        // 添加数字
        size_t len = strlen(data->password);
        if (len < sizeof(data->password) - 1) {
            data->password[len] = text[0];
            data->password[len + 1] = '\0';
        }
    }

    update_password_display(data);
}

static void close_button_cb(lv_event_t* e) {
    keypad_data_t* data = (keypad_data_t*)lv_event_get_user_data(e);
    ui_numeric_keypad_destroy(data->container);
}

static void update_password_display(keypad_data_t* data) {
    if (!data || !data->password_display) return;

    size_t len = strlen(data->password);
    if (len == 0) {
        lv_label_set_text(data->password_display, "New: (Enter 8+ digits)");
        lv_obj_set_style_text_color(data->password_display, lv_palette_main(LV_PALETTE_GREY), 0);
    } else {
        // 显示实际密码而不是星号，方便用户确认输入
        lv_label_set_text_fmt(data->password_display, "New: %s", data->password);
        
        // 根据密码长度改变颜色提示
        if (len >= 8) {
            lv_obj_set_style_text_color(data->password_display, lv_palette_main(LV_PALETTE_GREEN), 0);
        } else {
            lv_obj_set_style_text_color(data->password_display, lv_palette_main(LV_PALETTE_ORANGE), 0);
        }
    }
}
