#pragma once

#include "driver/ledc.h"

/**
 * @file app_config.h
 * @brief Compile-time hardware and firmware limits for the LED controller.
 *
 * Runtime values entered by the user belong in NVS. This file contains only
 * board wiring, protocol limits and safe defaults that require a new firmware
 * build when changed.
 */

/* Physical board wiring. */
#define APP_LED_CHANNEL_1_GPIO               14
#define APP_LED_CHANNEL_2_GPIO               12
#define APP_BUTTON_GPIO                      13
#define APP_BUTTON_ACTIVE_LEVEL              1

/* PWM configuration for the two HV9910B control inputs. */
#define APP_LEDC_FREQUENCY_HZ                3000
#define APP_LEDC_DUTY_RESOLUTION             LEDC_TIMER_10_BIT
#define APP_LEDC_DUTY_RESOLUTION_BITS        10U
#define APP_LEDC_DUTY_MAX \
    ((1U << APP_LEDC_DUTY_RESOLUTION_BITS) - 1U)
#define APP_POWER_TRANSITION_MS              1000U
#define APP_LEVEL_TRANSITION_MS              300U

/* Physical button timing. */
#define APP_BUTTON_DEBOUNCE_MS               50U
#define APP_BUTTON_LONG_PRESS_MS             5000U

/* Defaults and accepted ranges for the central application state. */
#define APP_DEFAULT_BRIGHTNESS_PERCENT       100U
#define APP_DEFAULT_LOWER_BRIGHTNESS_PERCENT 15U
#define APP_DEFAULT_FADE_MS                  3000U
#define APP_DEFAULT_PAUSE_MS                 1000U

#define APP_MIN_FADE_MS                      100U
#define APP_MAX_FADE_MS                      30000U
#define APP_MAX_PAUSE_MS                     30000U

/* Fixed resource and protocol limits. */
#define APP_STATE_MAX_SUBSCRIBERS            6U
#define APP_HTTP_MAX_BODY_SIZE               1024U

#define APP_PROVISIONING_AP_PASSWORD         "ledcontroller"
#define APP_DEFAULT_MQTT_PORT                1883U
#define APP_DEFAULT_DEVICE_ID_PREFIX         "led-controller"

#define APP_STATE_PERSIST_DEBOUNCE_MS        2000U

/*
 * These invariants describe mistakes in the firmware build, not bad runtime
 * input. Failing the build here makes a wiring/configuration error visible
 * before it can reach a real controller.
 */
_Static_assert(APP_LED_CHANNEL_1_GPIO != APP_LED_CHANNEL_2_GPIO,
               "LED channels must use different GPIOs");
_Static_assert(APP_BUTTON_GPIO != APP_LED_CHANNEL_1_GPIO &&
                   APP_BUTTON_GPIO != APP_LED_CHANNEL_2_GPIO,
               "Button GPIO must not overlap an LED channel");
_Static_assert(APP_BUTTON_ACTIVE_LEVEL == 0 || APP_BUTTON_ACTIVE_LEVEL == 1,
               "Button active level must be 0 or 1");
_Static_assert(APP_LEDC_DUTY_RESOLUTION_BITS >= 1U &&
                   APP_LEDC_DUTY_RESOLUTION_BITS <= 20U,
               "LEDC duty resolution must be between 1 and 20 bits");
_Static_assert(APP_DEFAULT_BRIGHTNESS_PERCENT <= 100U &&
                   APP_DEFAULT_LOWER_BRIGHTNESS_PERCENT <=
                       APP_DEFAULT_BRIGHTNESS_PERCENT,
               "Default brightness percentages are inconsistent");
_Static_assert(APP_MIN_FADE_MS <= APP_DEFAULT_FADE_MS &&
                   APP_DEFAULT_FADE_MS <= APP_MAX_FADE_MS,
               "Default fade duration is outside configured limits");
_Static_assert(APP_DEFAULT_PAUSE_MS <= APP_MAX_PAUSE_MS,
               "Default pause duration is outside configured limits");

