/**
 * @file i2s_tdm.c
 * @brief I2S TDM模式实现 - 单MAX98357 + 单麦克风输入
 * @author Your Name
 * @date 2024
 */

#include "i2s_tdm.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "I2S_TDM";

// 全局句柄
static i2s_tdm_handle_t g_i2s_tdm_handle = {0};

/**
 * @brief 初始化 I2S TDM - 单MAX98357 + 单麦克风
 */
esp_err_t i2s_tdm_init(void) {
    esp_err_t ret;

    if (g_i2s_tdm_handle.is_initialized) {
        ESP_LOGW(TAG, "I2S TDM already initialized");
        return ESP_OK;
    }

    // 配置发送通道 (到MAX98357)
    i2s_chan_config_t tx_chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    tx_chan_cfg.auto_clear = true;
    tx_chan_cfg.dma_desc_num = 8;
    tx_chan_cfg.dma_frame_num = 64;

    ret = i2s_new_channel(&tx_chan_cfg, &g_i2s_tdm_handle.tx_handle, &g_i2s_tdm_handle.rx_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create I2S channels: %s", esp_err_to_name(ret));
        return ret;
    }

    // 配置 TDM 时钟 - 使用更稳定的配置
    i2s_tdm_clk_config_t clk_cfg = I2S_TDM_CLK_DEFAULT_CONFIG(I2S_TDM_SAMPLE_RATE);
    clk_cfg.clk_src = I2S_CLK_SRC_XTAL;  // 使用XTAL时钟，更稳定
    clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;

    // 配置 TDM 时隙 - 手动初始化，优化配置
    i2s_tdm_slot_config_t slot_cfg = {
        .data_bit_width = I2S_TDM_BITS_PER_SAMPLE,
        .slot_bit_width = I2S_TDM_SLOT_BIT_WIDTH,
        .slot_mode = I2S_SLOT_MODE_MONO,  // 单声道模式
        .slot_mask = (1 << I2S_TDM_SLOT_SPEAKER) | (1 << I2S_TDM_SLOT_MIC),  // 启用扬声器时隙
        .ws_width = 32,
        .ws_pol = false,                           // WS低电平=左声道
        .bit_shift = true,                         // WS边沿后延迟1bit
        .left_align = true,                       // 右对齐
        .big_endian = false,                       // 小端序
        .bit_order_lsb = false,                    // MSB优先
    };

    i2s_tdm_config_t tdm_cfg = {
        .clk_cfg = clk_cfg,
        .slot_cfg = slot_cfg,
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = I2S_TDM_BCLK_PIN,
            .ws   = I2S_TDM_LRCK_PIN,
            .dout = I2S_TDM_DATA_OUT_PIN,
            .din  = I2S_TDM_DATA_IN_PIN,
            .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false }
        }
    };

    // 初始化发送通道 (TDM模式)
    ret = i2s_channel_init_tdm_mode(g_i2s_tdm_handle.tx_handle, &tdm_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init TX TDM mode: %d", ret);
        i2s_del_channel(g_i2s_tdm_handle.tx_handle);
        i2s_del_channel(g_i2s_tdm_handle.rx_handle);
        return ret;
    }

    // 配置接收时隙 (单麦克风)
    slot_cfg.slot_mask = (1 << I2S_TDM_SLOT_MIC);  // 只启用麦克风时隙
    tdm_cfg.slot_cfg = slot_cfg; // 确保 tdm_cfg 使用更新后的 slot_cfg

    // 初始化接收通道 (TDM模式)
    ret = i2s_channel_init_tdm_mode(g_i2s_tdm_handle.rx_handle, &tdm_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init RX TDM mode: %d", ret);
        i2s_del_channel(g_i2s_tdm_handle.tx_handle);
        i2s_del_channel(g_i2s_tdm_handle.rx_handle);
        return ret;
    }

    // 配置SD_MODE引脚，用于控制MAX98357
    // 注意：确保SD_MODE引脚连接到GPIO17
    gpio_config_t sd_mode_cfg = {
        .pin_bit_mask = (1ULL << I2S_TDM_SD_MODE_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    ret = gpio_config(&sd_mode_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure SD_MODE pin: %s", esp_err_to_name(ret));
        i2s_del_channel(g_i2s_tdm_handle.tx_handle);
        i2s_del_channel(g_i2s_tdm_handle.rx_handle);
        return ret;
    }
    // 设置为高电平，启用扬声器
    gpio_set_level(I2S_TDM_SD_MODE_PIN, 0); 

    g_i2s_tdm_handle.is_initialized = true;
    g_i2s_tdm_handle.sample_rate = I2S_TDM_SAMPLE_RATE;
    g_i2s_tdm_handle.buffer_size = 1024; // 默认缓冲区大小

    ESP_LOGI(TAG, "I2S TDM initialized - Single MAX98357 + Single Microphone");
    ESP_LOGI(TAG, "BCLK: GPIO%d, LRCK: GPIO%d, DOUT: GPIO%d, DIN: GPIO%d", 
             I2S_TDM_BCLK_PIN, I2S_TDM_LRCK_PIN, I2S_TDM_DATA_OUT_PIN, I2S_TDM_DATA_IN_PIN);
    ESP_LOGI(TAG, "Sample Rate: %dHz, Data Bits: %d, Slot Bits: %d, Slots: %d", 
             I2S_TDM_SAMPLE_RATE, I2S_TDM_BITS_PER_SAMPLE, I2S_TDM_SLOT_BIT_WIDTH, I2S_TDM_SLOT_NUM);
    ESP_LOGI(TAG, "TX Slot: %d (Speaker), RX Slot: %d (Mic)", 
             I2S_TDM_SLOT_SPEAKER, I2S_TDM_SLOT_MIC);

    return ESP_OK;
}

/**
 * @brief 反初始化 I2S TDM
 */
esp_err_t i2s_tdm_deinit(void) {
    if (!g_i2s_tdm_handle.is_initialized) {
        return ESP_OK;
    }

    // 停止通道（仅在已启动时禁用）
    if (g_i2s_tdm_handle.is_started) {
        if (g_i2s_tdm_handle.tx_handle) {
            i2s_channel_disable(g_i2s_tdm_handle.tx_handle);
        }
        if (g_i2s_tdm_handle.rx_handle) {
            i2s_channel_disable(g_i2s_tdm_handle.rx_handle);
        }
        g_i2s_tdm_handle.is_started = false;
    }

    // 删除通道
    if (g_i2s_tdm_handle.tx_handle) {
        i2s_del_channel(g_i2s_tdm_handle.tx_handle);
        g_i2s_tdm_handle.tx_handle = NULL;
    }
    if (g_i2s_tdm_handle.rx_handle) {
        i2s_del_channel(g_i2s_tdm_handle.rx_handle);
        g_i2s_tdm_handle.rx_handle = NULL;
    }

    g_i2s_tdm_handle.is_initialized = false;
    ESP_LOGI(TAG, "I2S TDM deinitialized");

    return ESP_OK;
}

/**
 * @brief 启动 I2S TDM (TX + RX)
 */
esp_err_t i2s_tdm_start(void) {
    if (!g_i2s_tdm_handle.is_initialized) {
        ESP_LOGE(TAG, "I2S TDM not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret;

    // 启动发送通道 (MAX98357)
    ret = i2s_channel_enable(g_i2s_tdm_handle.tx_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable TX channel: %s", esp_err_to_name(ret));
        return ret;
    }

    // 启动接收通道 (麦克风)
    ret = i2s_channel_enable(g_i2s_tdm_handle.rx_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable RX channel: %s", esp_err_to_name(ret));
        // 如果启用 RX 失败，禁用已启用的 TX
        i2s_channel_disable(g_i2s_tdm_handle.tx_handle);
        return ret;
    }

    g_i2s_tdm_handle.is_started = true;

    ESP_LOGI(TAG, "I2S TDM started (TX + RX)");
    return ESP_OK;
}

/**
 * @brief 停止 I2S TDM
 */
esp_err_t i2s_tdm_stop(void) {
    if (!g_i2s_tdm_handle.is_initialized) {
        return ESP_OK;
    }

    if (g_i2s_tdm_handle.tx_handle) {
        i2s_channel_disable(g_i2s_tdm_handle.tx_handle);
    }
    if (g_i2s_tdm_handle.rx_handle) {
        i2s_channel_disable(g_i2s_tdm_handle.rx_handle);
    }

    g_i2s_tdm_handle.is_started = false;

    ESP_LOGI(TAG, "I2S TDM stopped");
    return ESP_OK;
}

/**
 * @brief 写入音频数据到MAX98357
 * 数据格式: [音频16bit][音频16bit][音频16bit]...
 */
esp_err_t i2s_tdm_write(const void* data, size_t size, size_t* bytes_written) {
    if (!g_i2s_tdm_handle.is_initialized) {
        ESP_LOGE(TAG, "I2S TDM not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = i2s_channel_write(g_i2s_tdm_handle.tx_handle, data, size, bytes_written, portMAX_DELAY);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write audio data: %s", esp_err_to_name(ret));
    }

    return ret;
}

/**
 * @brief 读取麦克风音频数据
 * 数据格式: [麦克风16bit][麦克风16bit][麦克风16bit]...
 */
esp_err_t i2s_tdm_read(void* data, size_t size, size_t* bytes_read) {
    if (!g_i2s_tdm_handle.is_initialized) {
        ESP_LOGE(TAG, "I2S TDM not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = i2s_channel_read(g_i2s_tdm_handle.rx_handle, data, size, bytes_read, portMAX_DELAY);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read audio data: %s", esp_err_to_name(ret));
    }

    return ret;
}

/**
 * @brief 设置采样率
 */
esp_err_t i2s_tdm_set_sample_rate(uint32_t sample_rate) {
    if (!g_i2s_tdm_handle.is_initialized) {
        ESP_LOGE(TAG, "I2S TDM not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    // 仅在之前已启动时才停止通道并在完成后恢复
    bool was_started = g_i2s_tdm_handle.is_started;
    if (was_started) {
        if (g_i2s_tdm_handle.tx_handle) i2s_channel_disable(g_i2s_tdm_handle.tx_handle);
        if (g_i2s_tdm_handle.rx_handle) i2s_channel_disable(g_i2s_tdm_handle.rx_handle);
        g_i2s_tdm_handle.is_started = false;
    }

    // 重新配置 TDM 时钟
    i2s_tdm_clk_config_t clk_cfg = I2S_TDM_CLK_DEFAULT_CONFIG(sample_rate);
    clk_cfg.clk_src = I2S_CLK_SRC_XTAL;  // 使用XTAL时钟，更稳定
    clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;

    esp_err_t ret = i2s_channel_reconfig_tdm_clock(g_i2s_tdm_handle.tx_handle, &clk_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to reconfigure TX TDM clock: %d", ret);
        return ret;
    }

    ret = i2s_channel_reconfig_tdm_clock(g_i2s_tdm_handle.rx_handle, &clk_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to reconfigure RX TDM clock: %d", ret);
        return ret;
    }

    // 如果函数进入时设备正在运行，则重新启用通道
    if (was_started) {
        esp_err_t eret = i2s_channel_enable(g_i2s_tdm_handle.tx_handle);
        if (eret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to re-enable TX after clk change: %d", eret);
            return eret;
        }
        eret = i2s_channel_enable(g_i2s_tdm_handle.rx_handle);
        if (eret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to re-enable RX after clk change: %d", eret);
            i2s_channel_disable(g_i2s_tdm_handle.tx_handle);
            return eret;
        }
        g_i2s_tdm_handle.is_started = true;
    }

    g_i2s_tdm_handle.sample_rate = sample_rate;
    ESP_LOGI(TAG, "Sample rate changed to %lu Hz", sample_rate);

    return ESP_OK;
}

/**
 * @brief 获取当前采样率
 */
uint32_t i2s_tdm_get_sample_rate(void) { return g_i2s_tdm_handle.sample_rate; }

/**
 * @brief 检查I2S TDM是否已初始化
 */
bool i2s_tdm_is_initialized(void) { return g_i2s_tdm_handle.is_initialized; }

/**
 * @brief 获取缓冲区大小
 */
uint16_t i2s_tdm_get_buffer_size(void) { return g_i2s_tdm_handle.buffer_size; }