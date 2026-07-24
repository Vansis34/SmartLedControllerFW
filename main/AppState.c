#include "AppState.h"

#include "app_config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

typedef struct {
    app_state_listener_t callback;
    void *context;
} listener_slot_t;

static SemaphoreHandle_t s_state_mutex;
static app_state_snapshot_t s_state;
static listener_slot_t s_listeners[APP_STATE_MAX_SUBSCRIBERS];
static const char *TAG = "APP_STATE";

/**
 * @brief Check profile time limits common to all animated modes.
 * @param profile Profile being validated.
 * @return true when fade and pause values are supported.
 */
static bool profile_is_valid(const animation_profile_t *profile)
{
    return profile->fade_ms >= APP_MIN_FADE_MS &&
           profile->fade_ms <= APP_MAX_FADE_MS &&
           profile->pause_ms <= APP_MAX_PAUSE_MS;
}

/**
 * @brief Compare the logical fields of two animation profiles.
 *
 * Field-wise comparison is intentional. Comparing whole C structures with
 * memcmp() may include unspecified padding bytes that are not part of state.
 *
 * @param left First profile.
 * @param right Second profile.
 * @return true when both timing values are equal.
 */
static bool profiles_are_equal(const animation_profile_t *left,
                               const animation_profile_t *right)
{
    return left->fade_ms == right->fade_ms &&
           left->pause_ms == right->pause_ms;
}

/**
 * @brief Compare the logical fields of two half-alternate profiles.
 *
 * @param left First profile.
 * @param right Second profile.
 * @return true when timing and lower brightness are equal.
 */
static bool half_profiles_are_equal(const half_animation_profile_t *left,
                                    const half_animation_profile_t *right)
{
    return left->fade_ms == right->fade_ms &&
           left->pause_ms == right->pause_ms &&
           left->lower_brightness == right->lower_brightness;
}

/**
 * @brief Validate a complete candidate state before committing it.
 * @param candidate Fully merged state candidate.
 * @return true when every public invariant is satisfied.
 */
static bool state_is_valid(const app_state_snapshot_t *candidate)
{
    const animation_profile_t half_timing = {
        .fade_ms = candidate->half_alternate.fade_ms,
        .pause_ms = candidate->half_alternate.pause_ms,
    };

    return candidate->brightness <= 100U &&
           candidate->mode >= LED_MODE_ALLTIME &&
           candidate->mode <= LED_MODE_HALF_ALTERNATE &&
           profile_is_valid(&candidate->fade) &&
           profile_is_valid(&candidate->alternate) &&
           profile_is_valid(&half_timing) &&
           candidate->half_alternate.lower_brightness <= candidate->brightness;
}

void AppState_SetDefaults(app_state_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }

    *snapshot = (app_state_snapshot_t) {
        .power = false,
        .brightness = APP_DEFAULT_BRIGHTNESS_PERCENT,
        .mode = LED_MODE_ALLTIME,
        .fade = {APP_DEFAULT_FADE_MS, APP_DEFAULT_PAUSE_MS},
        .alternate = {APP_DEFAULT_FADE_MS, APP_DEFAULT_PAUSE_MS},
        .half_alternate = {
            APP_DEFAULT_FADE_MS,
            APP_DEFAULT_PAUSE_MS,
            APP_DEFAULT_LOWER_BRIGHTNESS_PERCENT,
        },
        .revision = 0,
    };
}

esp_err_t AppState_Init(const app_state_snapshot_t *restored)
{
    if (s_state_mutex != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    s_state_mutex = xSemaphoreCreateMutex();
    if (s_state_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }

    AppState_SetDefaults(&s_state);
    if (restored != NULL && state_is_valid(restored)) {
        s_state = *restored;
    }

    // Power is intentionally not persistent: booting must never energize the
    // HV9910B channels before all controller tasks are ready.
    s_state.power = false;
    s_state.revision = 1;

    // Static-storage objects start zeroed. Clearing explicitly also documents
    // that initialization begins with no registered event consumers.
    for (size_t i = 0; i < APP_STATE_MAX_SUBSCRIBERS; ++i) {
        s_listeners[i] = (listener_slot_t){0};
    }
    return ESP_OK;
}

esp_err_t AppState_Get(app_state_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_state_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    *snapshot = s_state;
    xSemaphoreGive(s_state_mutex);
    return ESP_OK;
}

esp_err_t AppState_Apply(const app_state_patch_t *patch,
                         uint32_t *changed_mask,
                         app_state_snapshot_t *result)
{
    if (patch == NULL || (patch->mask & ~APP_STATE_FIELD_ALL) != 0U) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_state_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    app_state_snapshot_t candidate;
    uint32_t changes = 0;
    listener_slot_t listeners[APP_STATE_MAX_SUBSCRIBERS] = {0};

    xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    candidate = s_state;

    if ((patch->mask & APP_STATE_FIELD_POWER) != 0U &&
        candidate.power != patch->values.power) {
        candidate.power = patch->values.power;
        changes |= APP_STATE_FIELD_POWER;
    }
    if ((patch->mask & APP_STATE_FIELD_BRIGHTNESS) != 0U &&
        candidate.brightness != patch->values.brightness) {
        candidate.brightness = patch->values.brightness;
        changes |= APP_STATE_FIELD_BRIGHTNESS;
    }
    if ((patch->mask & APP_STATE_FIELD_MODE) != 0U &&
        candidate.mode != patch->values.mode) {
        candidate.mode = patch->values.mode;
        changes |= APP_STATE_FIELD_MODE;
    }
    if ((patch->mask & APP_STATE_FIELD_FADE_PROFILE) != 0U &&
        !profiles_are_equal(&candidate.fade, &patch->values.fade)) {
        candidate.fade = patch->values.fade;
        changes |= APP_STATE_FIELD_FADE_PROFILE;
    }
    if ((patch->mask & APP_STATE_FIELD_ALTERNATE_PROFILE) != 0U &&
        !profiles_are_equal(&candidate.alternate, &patch->values.alternate)) {
        candidate.alternate = patch->values.alternate;
        changes |= APP_STATE_FIELD_ALTERNATE_PROFILE;
    }
    if ((patch->mask & APP_STATE_FIELD_HALF_PROFILE) != 0U &&
        !half_profiles_are_equal(&candidate.half_alternate,
                                 &patch->values.half_alternate)) {
        candidate.half_alternate = patch->values.half_alternate;
        changes |= APP_STATE_FIELD_HALF_PROFILE;
    }

    if (!state_is_valid(&candidate)) {
        xSemaphoreGive(s_state_mutex);
        return ESP_ERR_INVALID_ARG;
    }

    if (changes != 0U) {
        candidate.revision = s_state.revision + 1U;
        s_state = candidate;
    }
    if (result != NULL) {
        *result = s_state;
    }

    if (changes != 0U) {
        /*
         * Take a stable copy while the registry is protected. Callbacks are
         * invoked only after unlock, so they may safely call AppState_Get()
         * without deadlocking on this mutex. A listener should not recursively
         * apply state because nested notifications would obscure event order.
         */
        for (size_t i = 0; i < APP_STATE_MAX_SUBSCRIBERS; ++i) {
            listeners[i] = s_listeners[i];
        }
    }
    xSemaphoreGive(s_state_mutex);

    ESP_LOGD(TAG,
             "candidate validated: requested=0x%02lx changed=0x%02lx "
             "revision=%lu",
             (unsigned long)patch->mask,
             (unsigned long)changes,
             (unsigned long)candidate.revision);

    if (changed_mask != NULL) {
        *changed_mask = changes;
    }

    // Callbacks run after releasing the mutex. They may query AppState again,
    // but must remain non-blocking and normally only notify their worker task.
    if (changes != 0U) {
        for (size_t i = 0; i < APP_STATE_MAX_SUBSCRIBERS; ++i) {
            if (listeners[i].callback != NULL) {
                listeners[i].callback(&candidate, changes,
                                      listeners[i].context);
            }
        }
    }
    return ESP_OK;
}

esp_err_t AppState_Subscribe(app_state_listener_t listener, void *context)
{
    if (listener == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_state_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    for (size_t i = 0; i < APP_STATE_MAX_SUBSCRIBERS; ++i) {
        if (s_listeners[i].callback == NULL) {
            s_listeners[i].callback = listener;
            s_listeners[i].context = context;
            xSemaphoreGive(s_state_mutex);
            return ESP_OK;
        }
    }
    xSemaphoreGive(s_state_mutex);
    return ESP_ERR_NO_MEM;
}

