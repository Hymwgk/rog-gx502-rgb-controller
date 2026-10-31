#ifndef KEYBOARD_UNIFORM_H
#define KEYBOARD_UNIFORM_H
#include "keyboard_effect.h"
#include "keyboard_controls.h"

/* Unscaled RGB for constant-color and whole-keyboard color-breathing modes.
 * Each smooth color transition uses config->interval_ms; RGB vector energy
 * stays near the configured peak squared, scaled by the four brightness levels.
 * Channels retain multiple-of-64 S-PWM quantization. */
void keyboard_uniform_pixel(const keyboard_effect_config_t *config,
                            uint8_t mode, uint32_t time_ms, uint16_t rgb[3]);
#endif
