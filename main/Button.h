#pragma once

#include "esp_err.h"

typedef void (*button_factory_reset_callback_t)(void);

/**
 * @brief Initialize the active-high touch button task.
 * @param factory_reset Called after a stable long press reaches its threshold.
 * @return ESP_OK or an initialization/resource error.
 */
esp_err_t Button_Init(button_factory_reset_callback_t factory_reset);

