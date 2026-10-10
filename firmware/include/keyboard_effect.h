#ifndef KEYBOARD_EFFECT_H
#define KEYBOARD_EFFECT_H
#include <stdint.h>

typedef enum {
    KEYBOARD_CURVE_SQUARE,
    KEYBOARD_CURVE_LINEAR
} keyboard_effect_curve_t;

typedef struct {
    uint16_t peak;
    uint16_t steps_per_half;
    uint16_t step_ms;
    uint32_t gclk_hz;
    keyboard_effect_curve_t curve;
    uint8_t color_count;
    uint8_t colors[16]; /* Bit 0=red, bit 1=green, bit 2=blue. */
    uint16_t travel_ms, interval_ms, crest_ms, tail_ms;
    uint16_t floor;
    uint16_t color_blend; /* 0..1024: blend spline weights toward equal thirds. */
    uint8_t wave_smooth; /* 0=legacy, 1=smooth, 2=color-flow, 3=blended */
} keyboard_effect_config_t;

/* Pure brightness calculation; retains the S-PWM multiple-of-64 rule. */
uint16_t keyboard_effect_level(const keyboard_effect_config_t *config, uint32_t step);
/* Plays the selected build-time profile continuously. */
void keyboard_effect_run(void);
#endif
