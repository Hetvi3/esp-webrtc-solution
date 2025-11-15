#include <stdio.h>
#include "esp_log.h"
#include "codec_init.h"
#include "codec_board.h"
#include "esp_codec_dev.h"
#include "sdkconfig.h"
#include "settings.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "Board";

#define BSP_POWER_CODEC_EN 48

void init_board(void)
{
    ESP_LOGI(TAG, "Init board.");
    set_codec_board_type(TEST_BOARD_NAME);
    // Notes when use playback and record at same time, must set reuse_dev = false
    codec_init_cfg_t cfg = {
#if CONFIG_IDF_TARGET_ESP32S3
        .in_mode = CODEC_I2S_MODE_TDM,
        .in_use_tdm = true,
#endif
        .reuse_dev = false
    };
    init_codec(&cfg);
}

void bsp_power_init(void)
{
    ESP_LOGI(TAG, "Codec Power Enabled");
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << BSP_POWER_CODEC_EN,
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io_conf);

    gpio_set_level(BSP_POWER_CODEC_EN, 1);  
    vTaskDelay(pdMS_TO_TICKS(50));         
}