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

/**
 * @brief  Get OpenAI signaling implementation
 *
 * @return
 *      - NULL    Not enough memory
 *      - Others  OpenAI signaling implementation
 */
const esp_peer_signaling_impl_t *esp_signaling_get_openai_signaling(void);

/* ========== WiFi Manager (from wifi_manager.c) ========== */

/**
 * @brief Clear WiFi credentials + Auth token
 */
esp_err_t wifi_manager_clear_credentials(void);

/**
 * @brief Check if STA is connected
 */
bool wifi_manager_is_connected(void);

/**
 * @brief Trigger auth token check after WiFi connects
 *
 * Called automatically by IP_EVENT_STA_GOT_IP in wifi_manager.c,
 * but available for manual invocation if needed.
 */
void auth_check_after_wifi(void);

/**
 * @brief Get loaded/generated auth token (optional but useful)
 */
const char *wifi_manager_get_auth_token(void);

/**
 * @brief Start WiFi Manager (connect or start SoftAP provisioning)
 */
void wifi_manager_start(void (*on_creds_found)(const char *ssid, const char *pass));

/**
 * @brief  Start WebRTC
 *
 * @return
 *      - 0       On success
 *      - Others  Fail to start
 */
int start_webrtc(void);

/**
 * @brief  Send text to OpenAI server
 *
 * @param[in]  text  Text to be sent
 *
 * @return
 *      - 0       On success
 *      - Others  Fail to start
 */
int openai_send_text(char *text);

/**
 * @brief  Query WebRTC status
 */
void query_webrtc(void);

/**
 * @brief  Start WebRTC
 *
 * @param[in]  url  Signaling URL
 *
 * @return
 *      - 0       On success
 *      - Others  Fail to start
 */
int stop_webrtc(void);

#ifdef __cplusplus
}
#endif
