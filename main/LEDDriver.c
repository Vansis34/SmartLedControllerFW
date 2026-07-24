#include "LEDDriver.h"

#include <stdbool.h>
#include <stdint.h>

#include "AppState.h"
#include "app_config.h"
#include "driver/ledc.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

typedef enum {
    LED_WORKER_IDLE,
    LED_WORKER_MOVING,
    LED_WORKER_PAUSING,
} led_worker_phase_t;

typedef struct {
    uint32_t start_duty[2];
    uint32_t target_duty[2];
    TickType_t started_at;
    TickType_t duration_ticks;
} led_motion_t;

static const char *TAG = "LED";
static TaskHandle_t s_worker_task;

static const ledc_timer_config_t s_ledc_timer = {
    .speed_mode = LEDC_HIGH_SPEED_MODE,
    .duty_resolution = APP_LEDC_DUTY_RESOLUTION,
    .timer_num = LEDC_TIMER_0,
    .freq_hz = APP_LEDC_FREQUENCY_HZ,
    .clk_cfg = LEDC_AUTO_CLK,
};

static const ledc_channel_config_t s_channels[2] = {
    {
        .gpio_num = APP_LED_CHANNEL_1_GPIO,
        .speed_mode = LEDC_HIGH_SPEED_MODE,
        .channel = LEDC_CHANNEL_1,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0,
    },
    {
        .gpio_num = APP_LED_CHANNEL_2_GPIO,
        .speed_mode = LEDC_HIGH_SPEED_MODE,
        .channel = LEDC_CHANNEL_2,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0,
    },
};

/**
 * @brief Convert a validated percentage into a rounded 10-bit LEDC duty.
 * @param percent Brightness in the range 0..100.
 * @return Duty in the range 0..APP_LEDC_DUTY_MAX.
 */
static uint32_t percent_to_duty(uint8_t percent)
{
    return (APP_LEDC_DUTY_MAX * percent + 50U) / 100U;
}

/**
 * @brief Apply exact duty values through the thread-safe LEDC API.
 * @param duty_1 Channel 1 duty.
 * @param duty_2 Channel 2 duty.
 */
static void set_duties(uint32_t duty_1, uint32_t duty_2)
{
    ESP_ERROR_CHECK_WITHOUT_ABORT(ledc_set_duty_and_update(
        s_channels[0].speed_mode, s_channels[0].channel, duty_1, 0));
    ESP_ERROR_CHECK_WITHOUT_ABORT(ledc_set_duty_and_update(
        s_channels[1].speed_mode, s_channels[1].channel, duty_2, 0));
}

/**
 * @brief Prepare an interruptible software fade from current hardware duties.
 * @param motion Motion state updated by this function.
 * @param target_1 Target duty of channel 1.
 * @param target_2 Target duty of channel 2.
 * @param duration_ms Requested transition time in milliseconds.
 */
static void begin_motion(led_motion_t *motion, uint32_t target_1,
                         uint32_t target_2, uint32_t duration_ms)
{
    motion->start_duty[0] = ledc_get_duty(s_channels[0].speed_mode,
                                          s_channels[0].channel);
    motion->start_duty[1] = ledc_get_duty(s_channels[1].speed_mode,
                                          s_channels[1].channel);
    motion->target_duty[0] = target_1;
    motion->target_duty[1] = target_2;
    motion->started_at = xTaskGetTickCount();
    motion->duration_ticks = pdMS_TO_TICKS(duration_ms);
    if (motion->duration_ticks == 0) {
        motion->duration_ticks = 1;
    }
}

/**
 * @brief Advance an interruptible linear fade by one worker tick.
 * @param motion Active motion parameters.
 * @param now Current FreeRTOS tick.
 * @return true after both channels reach their exact targets.
 */
static bool advance_motion(const led_motion_t *motion, TickType_t now)
{
    TickType_t elapsed = now - motion->started_at;
    if (elapsed >= motion->duration_ticks) {
        set_duties(motion->target_duty[0], motion->target_duty[1]);
        return true;
    }

    uint32_t duties[2];
    for (size_t i = 0; i < 2; ++i) {
        const int64_t distance = (int64_t)motion->target_duty[i] -
                                 (int64_t)motion->start_duty[i];
        duties[i] = (uint32_t)((int64_t)motion->start_duty[i] +
                    distance * elapsed / motion->duration_ticks);
    }
    set_duties(duties[0], duties[1]);
    return false;
}

/**
 * @brief Read fade and boundary pause of the selected animated mode.
 * @param state Validated snapshot containing the selected mode profiles.
 * @param[out] fade_ms Fade duration in milliseconds.
 * @param[out] pause_ms Boundary pause duration in milliseconds.
 */
static void active_timing(const app_state_snapshot_t *state,
                          uint32_t *fade_ms, uint32_t *pause_ms)
{
    switch (state->mode) {
    case LED_MODE_FADE:
        *fade_ms = state->fade.fade_ms;
        *pause_ms = state->fade.pause_ms;
        break;
    case LED_MODE_ALTERNATE:
        *fade_ms = state->alternate.fade_ms;
        *pause_ms = state->alternate.pause_ms;
        break;
    case LED_MODE_HALF_ALTERNATE:
        *fade_ms = state->half_alternate.fade_ms;
        *pause_ms = state->half_alternate.pause_ms;
        break;
    default:
        /*
         * AppState validation prevents this branch. Safe zero values keep this
         * helper deterministic if its contract is accidentally violated later.
         */
        *fade_ms = 0U;
        *pause_ms = 0U;
        break;
    }
}

/**
 * @brief Calculate channel targets for one boundary of a lighting mode.
 * @param state Validated application state.
 * @param high_phase Selects the first or second animation boundary.
 * @param[out] target_1 Calculated duty for channel 1.
 * @param[out] target_2 Calculated duty for channel 2.
 * @return true for a known mode, or false after selecting safe zero targets.
 *
 * In alternate modes high_phase names the boundary where channel 1 is high.
 * In synchronous fade it names the boundary where both channels are high.
 */
static bool calculate_mode_targets(const app_state_snapshot_t *state,
                                   bool high_phase,
                                   uint32_t *target_1,
                                   uint32_t *target_2)
{
    const uint32_t high = percent_to_duty(state->brightness);

    switch (state->mode) {
    case LED_MODE_ALLTIME:
        *target_1 = high;
        *target_2 = high;
        return true;
    case LED_MODE_FADE:
        *target_1 = high_phase ? high : 0U;
        *target_2 = *target_1;
        return true;
    case LED_MODE_ALTERNATE:
        *target_1 = high_phase ? high : 0U;
        *target_2 = high_phase ? 0U : high;
        return true;
    case LED_MODE_HALF_ALTERNATE: {
        const uint32_t low =
            percent_to_duty(state->half_alternate.lower_brightness);
        *target_1 = high_phase ? high : low;
        *target_2 = high_phase ? low : high;
        return true;
    }
    default:
        /*
         * A corrupted mode must fail dark instead of unexpectedly energizing
         * a channel. Valid snapshots cannot normally reach this branch.
         */
        *target_1 = 0U;
        *target_2 = 0U;
        return false;
    }
}

/**
 * @brief Schedule one animated movement toward the selected boundary.
 * @param state Validated snapshot containing mode, brightness and profiles.
 * @param high_phase Selects which animation boundary is approached.
 * @param[out] motion Motion descriptor initialized from current PWM duties.
 */
static void begin_animation_motion(const app_state_snapshot_t *state,
                                   bool high_phase, led_motion_t *motion)
{
    uint32_t target_1;
    uint32_t target_2;
    uint32_t fade_ms;
    uint32_t pause_ms;

    (void)calculate_mode_targets(state, high_phase, &target_1, &target_2);
    active_timing(state, &fade_ms, &pause_ms);
    (void)pause_ms;
    begin_motion(motion, target_1, target_2, fade_ms);
}

/**
 * @brief Apply a complete snapshot and schedule its first transition.
 * @param state Newest committed application state.
 * @param[out] high_phase Animation phase reset to its deterministic first edge.
 * @param[out] motion New motion starting at the current hardware duties.
 * @return Worker phase selected for the newly scheduled transition.
 *
 * Reading current LEDC duties makes replacement interruptible: a new state
 * continues smoothly from the visible level instead of waiting for or jumping
 * to the end of the superseded transition.
 */
static led_worker_phase_t apply_snapshot(const app_state_snapshot_t *state,
                                         bool *high_phase,
                                         led_motion_t *motion)
{
    *high_phase = true;
    if (!state->power) {
        begin_motion(motion, 0U, 0U, APP_POWER_TRANSITION_MS);
    } else if (state->mode == LED_MODE_ALLTIME) {
        uint32_t target_1;
        uint32_t target_2;
        (void)calculate_mode_targets(state, *high_phase,
                                     &target_1, &target_2);
        begin_motion(motion, target_1, target_2, APP_LEVEL_TRANSITION_MS);
    } else {
        begin_animation_motion(state, *high_phase, motion);
    }
    return LED_WORKER_MOVING;
}

/**
 * @brief Compare a FreeRTOS deadline safely across tick counter wraparound.
 * @param now Current tick counter value.
 * @param deadline Previously calculated deadline.
 * @return true when now is equal to or later than deadline.
 *
 * The signed-difference idiom is valid because every configured delay is far
 * shorter than half of TickType_t's range.
 */
static bool tick_deadline_reached(TickType_t now, TickType_t deadline)
{
    return (int32_t)(now - deadline) >= 0;
}

/**
 * @brief Consume state snapshots and advance interruptible LED animations.
 * @param argument Unused FreeRTOS task parameter.
 *
 * ESP32's LEDC hardware cannot abort an active hardware fade. Short software
 * interpolation steps therefore control the hardware PWM duty directly. This
 * retains stable 3 kHz PWM while making mode, power and profile changes
 * responsive even during a long transition.
 */
static void led_worker(void *argument)
{
    (void)argument;
    app_state_snapshot_t state;
    led_motion_t motion = {0};
    led_worker_phase_t phase = LED_WORKER_IDLE;
    bool state_ready = false;
    bool high_phase = true;
    TickType_t pause_deadline = 0;

    for (;;) {
        const TickType_t wait_ticks =
            state_ready ? pdMS_TO_TICKS(APP_LED_WORKER_STEP_MS)
                        : portMAX_DELAY;

        if (ulTaskNotifyTake(pdTRUE, wait_ticks) > 0U) {
            app_state_snapshot_t latest;

            /*
             * Notifications carry no state. Re-reading the central store means
             * several rapid commits naturally coalesce into one application of
             * the newest revision without an ordering race between callbacks.
             */
            if (AppState_Get(&latest) != ESP_OK) {
                ESP_LOGE(TAG, "failed to refresh application state");
                continue;
            }
            if (!state_ready || latest.revision != state.revision) {
                state = latest;
                phase = apply_snapshot(&state, &high_phase, &motion);
                state_ready = true;
            }
            continue;
        }

        const TickType_t now = xTaskGetTickCount();
        if (phase == LED_WORKER_MOVING && advance_motion(&motion, now)) {
            if (!state.power || state.mode == LED_MODE_ALLTIME) {
                phase = LED_WORKER_IDLE;
            } else {
                uint32_t fade_ms;
                uint32_t pause_ms;
                active_timing(&state, &fade_ms, &pause_ms);
                (void)fade_ms;
                pause_deadline = now + pdMS_TO_TICKS(pause_ms);
                phase = LED_WORKER_PAUSING;
            }
        }

        if (phase == LED_WORKER_PAUSING &&
            tick_deadline_reached(now, pause_deadline)) {
            high_phase = !high_phase;
            begin_animation_motion(&state, high_phase, &motion);
            phase = LED_WORKER_MOVING;
        }
    }
}

/**
 * @brief Wake the LED worker after a committed application state change.
 *
 * A direct-to-task notification carries no payload. Multiple changes may
 * coalesce into one wake-up because the worker always reads the newest complete
 * snapshot from AppState. The callback therefore remains short and non-blocking.
 *
 * @param snapshot Committed snapshot; intentionally unused by this listener.
 * @param changed_mask Fields changed by the commit (not needed by LED).
 * @param context Unused listener context.
 */
static void state_listener(const app_state_snapshot_t *snapshot,
                           uint32_t changed_mask, void *context)
{
    (void)snapshot;
    (void)changed_mask;
    (void)context;

    if (s_worker_task == NULL ||
        xTaskNotifyGive(s_worker_task) != pdPASS) {
        ESP_LOGE(TAG, "failed to notify LED worker");
    }
}

esp_err_t LED_Init(void)
{
    if (s_worker_task != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_RETURN_ON_ERROR(ledc_timer_config(&s_ledc_timer), TAG,
                        "failed to configure LEDC timer");
    for (size_t i = 0; i < 2; ++i) {
        ESP_RETURN_ON_ERROR(ledc_channel_config(&s_channels[i]), TAG,
                            "failed to configure LEDC channel");
    }

    if (xTaskCreate(led_worker,
                    "led_worker",
                    4096,
                    NULL,
                    6,
                    &s_worker_task) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    const esp_err_t subscribe_result =
        AppState_Subscribe(state_listener, NULL);
    if (subscribe_result != ESP_OK) {
        /*
         * The worker is still waiting for its first notification, so it can be
         * deleted safely if subscription cannot be established.
         */
        vTaskDelete(s_worker_task);
        s_worker_task = NULL;
        ESP_LOGE(TAG, "failed to subscribe to state: %s",
                 esp_err_to_name(subscribe_result));
        return subscribe_result;
    }

    /*
     * The first notification closes the task-creation/subscription window and
     * makes the worker fetch the newest state before starting any animation.
     */
    xTaskNotifyGive(s_worker_task);
    return ESP_OK;
}
