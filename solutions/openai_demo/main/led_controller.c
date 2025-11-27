#include "common.h"
#include "settings.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"
#include <math.h>

static const char *TAG = "LED_CTRL";

static led_strip_handle_t led_strip = NULL;
static led_state_t current_state = LED_STATE_OFF;
static TaskHandle_t led_task_handle = NULL;
static bool is_initialized = false;

#define LED_ANIMATION_PERIOD_MS 20 

static void set_all_leds(uint8_t r, uint8_t g, uint8_t b)
{
    if (!led_strip) return;
    
    for (int i = 0; i < LED_STRIP_LED_COUNT; i++) {
        led_strip_set_pixel(led_strip, i, r, g, b);
    }
    led_strip_refresh(led_strip);
}

static void clear_all_leds(void)
{
    if (!led_strip) return;
    led_strip_clear(led_strip);
}

static void led_breathing_effect(uint8_t r, uint8_t g, uint8_t b, uint32_t *counter)
{
    float brightness = (sin(*counter * 0.05f) + 1.0f) / 2.0f; 
    brightness = brightness * (LED_STRIP_BRIGHTNESS / 100.0f);
    
    uint8_t r_scaled = (uint8_t)(r * brightness);
    uint8_t g_scaled = (uint8_t)(g * brightness);
    uint8_t b_scaled = (uint8_t)(b * brightness);
    
    set_all_leds(r_scaled, g_scaled, b_scaled);
    (*counter)++;
}

static void led_pulsing_effect(uint8_t r, uint8_t g, uint8_t b, uint32_t *counter)
{
    uint32_t phase = (*counter) % 50;
    float brightness;
    
    if (phase < 25) {
        brightness = phase / 25.0f;
    } else {
        brightness = (50 - phase) / 25.0f;
    }
    
    brightness = brightness * (LED_STRIP_BRIGHTNESS / 100.0f);
    
    uint8_t r_scaled = (uint8_t)(r * brightness);
    uint8_t g_scaled = (uint8_t)(g * brightness);
    uint8_t b_scaled = (uint8_t)(b * brightness);
    
    set_all_leds(r_scaled, g_scaled, b_scaled);
    (*counter)++;
}

static void led_wave_effect(uint8_t r, uint8_t g, uint8_t b, uint32_t *counter)
{
    if (!led_strip) return;
    
    for (int i = 0; i < LED_STRIP_LED_COUNT; i++) {
        float brightness = (sin((*counter * 0.1f) + (i * 1.5f)) + 1.0f) / 2.0f;
        brightness = brightness * (LED_STRIP_BRIGHTNESS / 100.0f);
        
        uint8_t r_scaled = (uint8_t)(r * brightness);
        uint8_t g_scaled = (uint8_t)(g * brightness);
        uint8_t b_scaled = (uint8_t)(b * brightness);
        
        led_strip_set_pixel(led_strip, i, r_scaled, g_scaled, b_scaled);
    }
    led_strip_refresh(led_strip);
    (*counter)++;
}

static void led_blinking_effect(uint8_t r, uint8_t g, uint8_t b, uint32_t *counter)
{
    uint32_t phase = (*counter) % 40;
    
    if (phase < 20) {
        uint8_t r_scaled = (uint8_t)(r * (LED_STRIP_BRIGHTNESS / 100.0f));
        uint8_t g_scaled = (uint8_t)(g * (LED_STRIP_BRIGHTNESS / 100.0f));
        uint8_t b_scaled = (uint8_t)(b * (LED_STRIP_BRIGHTNESS / 100.0f));
        set_all_leds(r_scaled, g_scaled, b_scaled);
    } else {
        clear_all_leds();
    }
    (*counter)++;
}

static void led_animation_task(void *pvParameters)
{
    uint32_t counter = 0;
    
    while (1) {
        switch (current_state) {
            case LED_STATE_IDLE:
                led_breathing_effect(0, 0, 255, &counter);
                break;
                
            case LED_STATE_CONNECTING:
                led_pulsing_effect(255, 255, 0, &counter);
                break;
                
            case LED_STATE_CONNECTED:
                set_all_leds(
                    0,
                    (uint8_t)(255 * (LED_STRIP_BRIGHTNESS / 100.0f)),
                    0
                );
                vTaskDelay(pdMS_TO_TICKS(100)); 
                break;
                
            case LED_STATE_SPEAKING:
                led_wave_effect(255, 0, 0, &counter);
                break;
                
            case LED_STATE_LISTENING:
                led_wave_effect(0, 255, 0, &counter);
                break;
                
            case LED_STATE_ERROR:
                led_blinking_effect(255, 0, 0, &counter);
                break;
                
            case LED_STATE_OFF:
            default:
                clear_all_leds();
                vTaskDelay(pdMS_TO_TICKS(100));
                break;
        }
        
        vTaskDelay(pdMS_TO_TICKS(LED_ANIMATION_PERIOD_MS));
    }
}

esp_err_t led_controller_init(void)
{
    if (is_initialized) {
        ESP_LOGW(TAG, "LED controller already initialized");
        return ESP_OK;
    }

#ifndef LED_STRIP_ENABLED
    ESP_LOGI(TAG, "LED strip disabled in configuration");
    return ESP_OK;
#endif

    ESP_LOGI(TAG, "Initializing LED controller (GPIO: %d, Count: %d)", 
             LED_STRIP_GPIO, LED_STRIP_LED_COUNT);

    led_strip_config_t strip_config = {
        .strip_gpio_num = LED_STRIP_GPIO,
        .max_leds = LED_STRIP_LED_COUNT,
        .led_model = LED_MODEL_WS2812,          
        .flags.invert_out = false,
    };

    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000, 
        .flags.with_dma = false,            
    };

    esp_err_t ret = led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create LED strip: %s", esp_err_to_name(ret));
        return ret;
    }

    clear_all_leds();

    BaseType_t task_ret = xTaskCreate(
        led_animation_task,
        "led_anim",
        3 * 1024,
        NULL,
        5,  // Low priority
        &led_task_handle
    );

    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create LED animation task");
        led_strip_del(led_strip);
        led_strip = NULL;
        return ESP_FAIL;
    }

    current_state = LED_STATE_IDLE;
    is_initialized = true;
    ESP_LOGI(TAG, "LED controller initialized successfully");

    return ESP_OK;
}

esp_err_t led_controller_set_state(led_state_t state)
{
    if (!is_initialized) {
        ESP_LOGW(TAG, "LED controller not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (state != current_state) {
        ESP_LOGI(TAG, "LED state changed: %d -> %d", current_state, state);
        current_state = state;
    }

    return ESP_OK;
}

led_state_t led_controller_get_state(void)
{
    return current_state;
}

esp_err_t led_controller_deinit(void)
{
    if (!is_initialized) {
        return ESP_OK;
    }

    if (led_task_handle) {
        vTaskDelete(led_task_handle);
        led_task_handle = NULL;
    }

    if (led_strip) {
        clear_all_leds();
        led_strip_del(led_strip);
        led_strip = NULL;
    }

    is_initialized = false;
    current_state = LED_STATE_OFF;
    ESP_LOGI(TAG, "LED controller deinitialized");

    return ESP_OK;
}
