#ifndef KEYBOARD_CONTROLS_H
#define KEYBOARD_CONTROLS_H
#include <stdint.h>

#define KEYBOARD_BRIGHTNESS_STEPS 3U
#define KEYBOARD_BUTTON_DEBOUNCE_MS 20U

typedef enum {
    KEYBOARD_MODE_FORWARD,
    KEYBOARD_MODE_REVERSE,
    KEYBOARD_MODE_UNIFORM,
    /* Append modes so existing EEPROM identifiers retain their meaning. */
    KEYBOARD_MODE_WHITE,
    KEYBOARD_MODE_RED,
    KEYBOARD_MODE_GREEN,
    KEYBOARD_MODE_BLUE,
    KEYBOARD_MODE_COLOR_BREATHE,
    KEYBOARD_MODE_METEOR,
    KEYBOARD_MODE_RIPPLE,
    KEYBOARD_MODE_COUNT
} keyboard_mode_t;

typedef struct {
    uint32_t changed_ms[4];
    uint8_t candidate;
    uint8_t stable;
    uint8_t mode;
    uint8_t brightness; /* 0..3, scales the configured peak without exceeding it. */
} keyboard_controls_t;

/* Call at least once per refresh frame with actual elapsed milliseconds.
 * PB12/PB13: previous/next; PB14/PB15: brighter/dimmer.
 * One action per press; both buttons of a pair cancel each other. */
void keyboard_controls_update(keyboard_controls_t *controls,
                              uint8_t pressed, uint32_t now_ms);
/* This runs per channel in the measured scan path; prevent size optimization
 * from outlining it when additional effect modes reuse the calculation. */
static inline __attribute__((always_inline)) uint16_t keyboard_controls_level(const keyboard_controls_t *controls, uint16_t level)
{
    /* Exact floor(level * brightness / 3) for the supported 0..16384
     * levels, without a software division in the bank preparation path. */
    static const uint32_t gain[4] = {0U, 21846U, 43691U, 65536U};
    return (uint16_t)((((uint32_t)level * gain[controls->brightness]) >> 16) & ~63U);
}
#endif
