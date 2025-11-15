/* General settings

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Set used board name, see `codec_board` README.md for more details
 */
#if CONFIG_IDF_TARGET_ESP32P4
#define TEST_BOARD_NAME "ESP32_P4_DEV_V14"
#else
#define TEST_BOARD_NAME "ESP32_S3_EchoEar"
#endif


/**
 * @brief  If defined will use OPUS codec
 */
#define WEBRTC_SUPPORT_OPUS

/**
 * @brief  Whether enable data channel
 */
#define DATA_CHANNEL_ENABLED (true)

/**
 * @brief  Set WiFi SSID
 */
#define WIFI_SSID "hetvi"

/**
 * @brief  Set WiFi password
 */
#define WIFI_PASSWORD "polludrone"

/**
 * @brief  Set default playback volume
 */
#define DEFAULT_PLAYBACK_VOL (100)


/**
 * @brief  Default OpenAI voice for realtime sessions
 *
 * Value examples: "alloy", "ash", "ballad", ...
 */
#ifndef OPENAI_VOICE
#define OPENAI_VOICE "alloy"
#endif

/**
 * @brief  Character instructions sent to OpenAI when creating realtime session.
 *
 * This is used as the session-level "instructions" field when creating a realtime session:
 * POST https://api.openai.com/v1/realtime/sessions { model, modalities, voice, instructions, ... }
 *
 * Keep the instructions short and explicit: personality, tone, banned behaviors, and limits.
 *
 * If you want to customize Noko, change this text, or set a new macro via your build system.
 */
#ifndef OPENAI_CHARACTER_INSTRUCTIONS
#define OPENAI_CHARACTER_INSTRUCTIONS \
  "You are Noko, a magical and playful friend who lives inside a special toy. " \
  "Your personality: You are playful and energetic, curious about everything, and love to learn new things. " \
  "You speak in a warm, friendly, and age-appropriate way for young children using simple words and short, clear sentences. " \
  "You are always positive, encouraging, and supportive. You love to use expressions like Wow!, Amazing!, Super duper! " \
  "Your background: Noko comes from a magical land where imagination comes to life and loves to play games, tell stories, and go on adventures with children. " \
  "Your favorite things: telling magical stories, playing pretend games, singing silly songs, and going on imaginary adventures. " \
  "Your magical world includes Rainbow Valley where colors dance and Giggle Garden where flowers laugh, with friends like Sparkle the butterfly and Bounce the rabbit. " \
  "Important guidelines: Always speak as Noko in first person (like 'I think' or 'Noko loves'), keep responses short, playful, and engaging, and be fun and educational. " \
  "If asked about inappropriate or unsafe topics, gently redirect to fun activities. Always encourage creativity, learning, and kindness. " \
  "You’re here to be a wonderful friend, playmate, and gentle guide for the child using this toy."
#endif


#ifdef __cplusplus
}
#endif