#pragma once

#include "esp_err.h"

/**
 * @brief Initialize LEDC hardware and start the LED controller task.
 *
 * The driver subscribes to AppState and consumes immutable snapshots. Calling
 * this function requires AppState_Init() to have completed successfully.
 *
 * @return ESP_OK on success or an ESP-IDF/resource allocation error.
 */
esp_err_t LED_Init(void);

