#pragma once

#include "esp_err.h"

/**
 * @brief Initialize LEDC hardware and start the LED controller task.
 *
 * The driver subscribes to AppState and consumes stable value snapshots. Calling
 * this function requires AppState_Init() to have completed successfully.
 *
 * @return ESP_OK, ESP_ERR_INVALID_STATE when already initialized, or an
 *         ESP-IDF/resource allocation error.
 */
esp_err_t LED_Init(void);

