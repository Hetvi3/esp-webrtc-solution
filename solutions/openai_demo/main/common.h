/* Common header

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "settings.h"
#include "media_sys.h"
#include "network.h"
#include "sys_state.h"
#include "esp_webrtc.h"

/**
 * @brief  Initialize board
 */
void init_board(void);

void bsp_power_init(void);

/**
 * @brief  OpenAI signaling configuration
 *
 * @note   Details see: https://platform.openai.com/docs/api-reference/realtime-sessions/create#realtime-sessions-create-voice
 */
typedef struct {
   char *token; /*!< OpenAI token */
   char *voice; /*!< Voice to select */
    char *instructions; /*!< Optional session-level instructions / persona sent to the Realtime API */
} openai_signaling_cfg_t;

const esp_peer_signaling_impl_t *esp_signaling_get_openai_signaling(void);

esp_err_t wifi_manager_clear_credentials(void);

bool wifi_manager_is_connected(void);

void auth_check_after_wifi(void);

const char *wifi_manager_get_auth_token(void);

void wifi_manager_start(void (*on_creds_found)(const char *ssid, const char *pass));

int start_webrtc(void);

int openai_send_text(char *text);

void query_webrtc(void);


typedef enum {
    LED_STATE_IDLE,        /* Idle state - soft breathing blue */
    LED_STATE_CONNECTING,  /* Connecting to WiFi/WebRTC - yellow pulsing */
    LED_STATE_CONNECTED,   /* Connected - solid green */
    LED_STATE_SPEAKING,    /* Speaking/transmitting audio - red wave */
    LED_STATE_LISTENING,   /* Listening/receiving audio - green wave */
    LED_STATE_ERROR,       /* Error state - red blinking */
    LED_STATE_OFF,         /* LEDs turned off */
} led_state_t;


esp_err_t led_controller_init(void);

esp_err_t led_controller_set_state(led_state_t state);

led_state_t led_controller_get_state(void);

esp_err_t led_controller_deinit(void);

int stop_webrtc(void);

#ifdef __cplusplus
}
#endif
