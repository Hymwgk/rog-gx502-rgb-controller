#ifndef KEYBOARD_WAVE_H
#define KEYBOARD_WAVE_H
#include <stdint.h>
#include "keyboard_effect.h"
/* projection 0..1024: bottom-left to top-right; result is quantized RGB. */
void keyboard_wave_pixel(const keyboard_effect_config_t *config,
                         uint16_t projection, uint32_t time_ms, uint16_t rgb[3]);
void keyboard_wave_run(void);
#endif
