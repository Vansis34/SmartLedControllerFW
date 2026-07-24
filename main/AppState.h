#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

/** @brief Public lighting modes accepted by WEB and MQTT. */
typedef enum {
    LED_MODE_ALLTIME = 1,
    LED_MODE_FADE = 2,
    LED_MODE_ALTERNATE = 3,
    LED_MODE_HALF_ALTERNATE = 4,
} led_mode_t;

/** @brief Timing profile shared by the rise and fall phases of a mode. */
typedef struct {
    uint32_t fade_ms;
    uint32_t pause_ms;
} animation_profile_t;

/** @brief Timing and lower brightness profile for half-alternate mode. */
typedef struct {
    uint32_t fade_ms;
    uint32_t pause_ms;
    uint8_t lower_brightness;
} half_animation_profile_t;

/**
 * @brief Stable value-copy of the complete runtime lighting state.
 *
 * A snapshot does not point into the central store. The receiver owns its copy
 * and may keep or modify it without racing with later state revisions.
 */
typedef struct {
    bool power;
    uint8_t brightness;
    led_mode_t mode;
    animation_profile_t fade;
    animation_profile_t alternate;
    half_animation_profile_t half_alternate;
    uint32_t revision;
} app_state_snapshot_t;

typedef enum {
    APP_STATE_FIELD_POWER = 1U << 0,
    APP_STATE_FIELD_BRIGHTNESS = 1U << 1,
    APP_STATE_FIELD_MODE = 1U << 2,
    APP_STATE_FIELD_FADE_PROFILE = 1U << 3,
    APP_STATE_FIELD_ALTERNATE_PROFILE = 1U << 4,
    APP_STATE_FIELD_HALF_PROFILE = 1U << 5,
    APP_STATE_FIELD_ALL = (1U << 6) - 1U,
} app_state_field_t;

/** @brief Partial update; only fields selected by mask are applied. */
typedef struct {
    uint32_t mask;
    app_state_snapshot_t values;
} app_state_patch_t;

/**
 * @brief Receive a committed state change after the central mutex is released.
 *
 * The callback runs synchronously in the task that called AppState_Apply().
 * It must remain non-blocking and must not call AppState_Apply() recursively;
 * the normal pattern is an overwrite queue or a FreeRTOS task notification to
 * a dedicated consumer task.
 *
 * @param snapshot Stable copy of the newly committed state.
 * @param changed_mask Bit mask of fields that actually changed.
 * @param context Opaque pointer supplied during subscription.
 */
typedef void (*app_state_listener_t)(const app_state_snapshot_t *snapshot,
                                     uint32_t changed_mask,
                                     void *context);

/**
 * @brief Initialize the central state store.
 * @param restored Optional persisted settings; power is always forced off.
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if already initialized.
 */
esp_err_t AppState_Init(const app_state_snapshot_t *restored);

/**
 * @brief Copy the current state while holding the internal mutex.
 * @param[out] snapshot Destination for a consistent state copy.
 * @return ESP_OK or ESP_ERR_INVALID_ARG/ESP_ERR_INVALID_STATE.
 */
esp_err_t AppState_Get(app_state_snapshot_t *snapshot);

/**
 * @brief Validate and atomically apply a partial state update.
 * @param patch Fields and values requested by a command source.
 * @param[out] changed_mask Optional mask of values that actually changed.
 * @param[out] result Optional resulting state snapshot.
 * @return ESP_OK, ESP_ERR_INVALID_ARG for invalid values, or state errors.
 */
esp_err_t AppState_Apply(const app_state_patch_t *patch,
                         uint32_t *changed_mask,
                         app_state_snapshot_t *result);

/**
 * @brief Register a non-blocking listener invoked after successful changes.
 * @param listener Callback that receives a stable snapshot.
 * @param context Opaque pointer returned to the callback.
 * @return ESP_OK, ESP_ERR_INVALID_ARG, ESP_ERR_INVALID_STATE, or
 *         ESP_ERR_NO_MEM when all listener slots are occupied.
 */
esp_err_t AppState_Subscribe(app_state_listener_t listener, void *context);

/**
 * @brief Fill a snapshot with firmware defaults.
 * @param[out] snapshot Destination structure.
 */
void AppState_SetDefaults(app_state_snapshot_t *snapshot);

