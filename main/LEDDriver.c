#include "LEDDriver.h"

#include <stdbool.h>
#include <stdint.h>

#include "AppState.h"
#include "app_config.h"
#include "driver/ledc.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
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
static QueueHandle_t s_state_queue;
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
 */
static void active_timing(const app_state_snapshot_t *state,
                          uint32_t *fade_ms, uint32_t *pause_ms)
{
    if (state->mode == LED_MODE_FADE) {
        *fade_ms = state->fade.fade_ms;
        *pause_ms = state->fade.pause_ms;
    } else if (state->mode == LED_MODE_ALTERNATE) {
        *fade_ms = state->alternate.fade_ms;
        *pause_ms = state->alternate.pause_ms;
    } else {
        *fade_ms = state->half_alternate.fade_ms;
        *pause_ms = state->half_alternate.pause_ms;
    }
}

/**
 * @brief Schedule one animation movement toward the selected boundary.
 */
static void begin_animation_motion(const app_state_snapshot_t *state,
                                   bool high_phase, led_motion_t *motion)
{
    const uint32_t high = percent_to_duty(state->brightness);
    const uint32_t low = state->mode == LED_MODE_HALF_ALTERNATE
                             ? percent_to_duty(state->half_alternate.lower_brightness)
                             : 0U;
    uint32_t fade_ms;
    uint32_t pause_ms;
    active_timing(state, &fade_ms, &pause_ms);
    (void)pause_ms;

    if (state->mode == LED_MODE_FADE) {
        begin_motion(motion, high_phase ? high : 0U,
                     high_phase ? high : 0U, fade_ms);
    } else {
        begin_motion(motion, high_phase ? high : low,
                     high_phase ? low : high, fade_ms);
    }
}

/**
 * @brief Apply a complete snapshot and schedule its first transition.
 */
static led_worker_phase_t apply_snapshot(const app_state_snapshot_t *state,
                                         bool *high_phase,
                                         led_motion_t *motion)
{
    *high_phase = true;
    if (!state->power) {
        begin_motion(motion, 0U, 0U, APP_POWER_TRANSITION_MS);
    } else if (state->mode == LED_MODE_ALLTIME) {
        const uint32_t duty = percent_to_duty(state->brightness);
        begin_motion(motion, duty, duty, APP_LEVEL_TRANSITION_MS);
    } else {
        begin_animation_motion(state, *high_phase, motion);
    }
    return LED_WORKER_MOVING;
}

/**
 * @brief Consume state snapshots and advance interruptible LED animations.
 * @param argument Unused FreeRTOS task parameter.
 *
 * ESP32's LEDC hardware cannot abort an active hardware fade. A short 10 ms
 * software interpolation therefore controls the hardware PWM duty directly.
 * This retains stable 3 kHz PWM while making mode, power and profile changes
 * responsive even during a 30-second transition.
 */
static void led_worker(void *argument)
{
    (void)argument;
    app_state_snapshot_t state;
    led_motion_t motion = {0};
    led_worker_phase_t phase;
    bool high_phase = true;
    TickType_t pause_deadline = 0;

    ESP_ERROR_CHECK(AppState_Get(&state));
    phase = apply_snapshot(&state, &high_phase, &motion);

    for (;;) {
        app_state_snapshot_t incoming;
        if (xQueueReceive(s_state_queue,
                          &incoming,
                          pdMS_TO_TICKS(10)) == pdPASS) {
            state = incoming;
            phase = apply_snapshot(&state, &high_phase, &motion);
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
            (int32_t)(now - pause_deadline) >= 0) {
            high_phase = !high_phase;
            begin_animation_motion(&state, high_phase, &motion);
            phase = LED_WORKER_MOVING;
        }
    }
}

/**
 * @brief Forward the newest committed snapshot to the LED worker.
 *
 * This callback runs in the task that changed AppState. xQueueOverwrite() is
 * non-blocking for the one-element queue: an unprocessed older snapshot is
 * intentionally replaced because PWM only needs the newest desired state.
 *
 * @param snapshot Stable copy of the committed application state.
 * @param changed_mask Fields changed by the commit (not needed by LED).
 * @param context Unused listener context.
 */
static void state_listener(const app_state_snapshot_t *snapshot,
                           uint32_t changed_mask, void *context)
{
    (void)changed_mask;
    (void)context;

    if (s_state_queue == NULL ||
        xQueueOverwrite(s_state_queue, snapshot) != pdPASS) {
        /*
         * A valid one-element queue cannot normally reject overwrite. Logging
         * exposes an internal lifecycle violation without blocking the task
         * that applied AppState.
         */
        ESP_LOGE(TAG, "failed to forward state snapshot");
    }
}

esp_err_t LED_Init(void)
{
    if (s_state_queue != NULL || s_worker_task != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_RETURN_ON_ERROR(ledc_timer_config(&s_ledc_timer), TAG,
                        "failed to configure LEDC timer");
    for (size_t i = 0; i < 2; ++i) {
        ESP_RETURN_ON_ERROR(ledc_channel_config(&s_channels[i]), TAG,
                            "failed to configure LEDC channel");
    }

    s_state_queue = xQueueCreate(1, sizeof(app_state_snapshot_t));
    if (s_state_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }
    ESP_RETURN_ON_ERROR(AppState_Subscribe(state_listener, NULL), TAG,
                        "failed to subscribe to state");
    if (xTaskCreate(led_worker,
                    "led_worker",
                    4096,
                    NULL,
                    6,
                    &s_worker_task) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
