#include "Button.h"

#include <stdbool.h>

#include "AppState.h"
#include "app_config.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "BUTTON";
static button_factory_reset_callback_t s_factory_reset;

/**
 * @brief Toggle only the runtime power field through the central store.
 */
static void toggle_power(void)
{
    app_state_snapshot_t current;
    if (AppState_Get(&current) != ESP_OK) {
        return;
    }

    const app_state_patch_t patch = {
        .mask = APP_STATE_FIELD_POWER,
        .values.power = !current.power,
    };
    ESP_ERROR_CHECK_WITHOUT_ABORT(AppState_Apply(&patch, NULL, NULL));
}

/**
 * @brief Poll and debounce the push-pull touch button.
 * @param argument Unused FreeRTOS task parameter.
 *
 * A 10 ms poll is inexpensive for one human input and keeps all duration logic
 * in task context. A short action is emitted only on release, so a long press
 * never toggles power before starting factory reset.
 */
static void button_worker(void *argument)
{
    (void)argument;
    bool raw_previous = gpio_get_level(APP_BUTTON_GPIO) == APP_BUTTON_ACTIVE_LEVEL;
    bool stable_pressed = raw_previous;
    bool long_press_handled = false;
    TickType_t raw_changed_at = xTaskGetTickCount();
    TickType_t pressed_at = raw_changed_at;

    for (;;) {
        const TickType_t now = xTaskGetTickCount();
        const bool raw_pressed =
            gpio_get_level(APP_BUTTON_GPIO) == APP_BUTTON_ACTIVE_LEVEL;

        if (raw_pressed != raw_previous) {
            raw_previous = raw_pressed;
            raw_changed_at = now;
        }

        if (raw_pressed != stable_pressed &&
            now - raw_changed_at >= pdMS_TO_TICKS(APP_BUTTON_DEBOUNCE_MS)) {
            stable_pressed = raw_pressed;
            if (stable_pressed) {
                pressed_at = now;
                long_press_handled = false;
            } else if (!long_press_handled) {
                toggle_power();
            }
        }

        if (stable_pressed && !long_press_handled &&
            now - pressed_at >= pdMS_TO_TICKS(APP_BUTTON_LONG_PRESS_MS)) {
            long_press_handled = true;
            ESP_LOGW(TAG, "factory reset requested by long press");
            if (s_factory_reset != NULL) {
                s_factory_reset();
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

esp_err_t Button_Init(button_factory_reset_callback_t factory_reset)
{
    s_factory_reset = factory_reset;
    const gpio_config_t config = {
        .pin_bit_mask = 1ULL << APP_BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t error = gpio_config(&config);
    if (error != ESP_OK) {
        return error;
    }
    if (xTaskCreate(button_worker, "button", 3072, NULL, 5, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

