#pragma once
// #include "driver/gpio.h"

#define LED_1CH_PIN     (14)
#define LED_2CH_PIN     (12)
#define FADE_TIME_ON    2000
#define FADE_TIME_OFF   2000
#define FADE_TIMEOUT    3000

#define RELEY_PIN       13

typedef enum ledMode_e
{
    MODE_OFF = 0,
    MODE_ALLTIME,
    MODE_FADE,
    MODE_FADE_ALTERNATELY,
    MODE_HALF_FADE_ALTERNATELY,
}ledMode_t;


typedef struct {
    bool led_state; // 0 = выключено, 1 = включено
    ledMode_t mode;
    uint8_t ledBrightness;
    uint8_t halfBrightness;
    uint16_t brightness_1ch;
    uint16_t brightness_2ch;
    uint16_t halfBrightness_1ch;
    uint16_t halfBrightness_2ch;
    uint32_t timeFade;

} device_status_t;


