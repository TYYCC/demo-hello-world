#include "image_transfer_app.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "TEST_IMAGE_TRANSFER";

void test_image_transfer_app(void) {
    ESP_LOGI(TAG, "Testing Image Transfer Application...");
    
    // 测试初始化
    esp_err_t ret = image_transfer_app_init(IMAGE_TRANSFER_MODE_RAW);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize image transfer app");
        return;
    }
    
    ESP_LOGI(TAG, "Image transfer app initialized successfully");
    
    // 测试TCP服务器启动
    ret = image_transfer_app_start_tcp_server(6556);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start TCP server");
        image_transfer_app_deinit();
        return;
    }
    
    ESP_LOGI(TAG, "TCP server started successfully on port 6556");
    
    // 测试模式切换
    ret = image_transfer_app_set_mode(IMAGE_TRANSFER_MODE_LZ4);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to switch to LZ4 mode");
    } else {
        ESP_LOGI(TAG, "Switched to LZ4 mode successfully");
    }
    
    ret = image_transfer_app_set_mode(IMAGE_TRANSFER_MODE_JPEG);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to switch to JPEG mode");
    } else {
        ESP_LOGI(TAG, "Switched to JPEG mode successfully");
    }
    
    // 测试状态检查
    bool is_running = image_transfer_app_is_running();
    bool is_tcp_running = image_transfer_app_is_tcp_server_running();
    
    ESP_LOGI(TAG, "App running: %s", is_running ? "YES" : "NO");
    ESP_LOGI(TAG, "TCP server running: %s", is_tcp_running ? "YES" : "NO");
    
    // 等待一段时间让服务器运行
    vTaskDelay(pdMS_TO_TICKS(5000));
    
    // 停止服务
    image_transfer_app_stop_tcp_server();
    image_transfer_app_deinit();
    
    ESP_LOGI(TAG, "Image transfer app test completed");
}