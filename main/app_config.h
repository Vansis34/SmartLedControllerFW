#pragma once

/**
 * @file app_config.h
 * @brief Compile-time hardware and firmware limits for the LED controller.
 *
 * Runtime values entered by the user belong in NVS. This file contains only
 * board wiring, protocol limits and safe defaults that require a new firmware
 * build when changed.
 */

#define APP_LED_CHANNEL_1_GPIO              14
#define APP_LED_CHANNEL_2_GPIO              12
#define APP_BUTTON_GPIO                     13
#define APP_BUTTON_ACTIVE_LEVEL             1

#define APP_LEDC_FREQUENCY_HZ               3000
#define APP_LEDC_DUTY_MAX                    1023U
#define APP_POWER_TRANSITION_MS             1000U
#define APP_LEVEL_TRANSITION_MS             300U

#define APP_BUTTON_DEBOUNCE_MS              50U
#define APP_BUTTON_LONG_PRESS_MS            5000U

#define APP_DEFAULT_BRIGHTNESS_PERCENT      100U
#define APP_DEFAULT_LOWER_BRIGHTNESS_PERCENT 15U
#define APP_DEFAULT_FADE_MS                 3000U
#define APP_DEFAULT_PAUSE_MS                1000U

#define APP_MIN_FADE_MS                     100U
#define APP_MAX_FADE_MS                     30000U
#define APP_MAX_PAUSE_MS                    30000U

#define APP_STATE_MAX_SUBSCRIBERS           6U
#define APP_HTTP_MAX_BODY_SIZE              1024U

#define APP_PROVISIONING_AP_PASSWORD        "ledcontroller"
#define APP_DEFAULT_MQTT_PORT               1883U
#define APP_DEFAULT_DEVICE_ID_PREFIX        "led-controller"

#define APP_STATE_PERSIST_DEBOUNCE_MS       2000U

