#ifndef KEYBOARD_RGB_H
#define KEYBOARD_RGB_H

#include <stdint.h>
#include "mbia045.h"

void keyboard_rgb_init(void);
void keyboard_rgb_emergency_off(void);
void keyboard_rgb_ctrl_all_off(void);
int keyboard_rgb_ctrl_set(uint8_t index, uint8_t level);
int keyboard_rgb_ctrl_only(uint8_t index, uint8_t level);
int keyboard_rgb_ctrl_set_mask(uint8_t mask, uint8_t level);
int keyboard_rgb_set_channel(uint8_t index, uint16_t brightness);
void keyboard_rgb_set_all_channels(uint16_t brightness);
void keyboard_rgb_set_raw48(const uint16_t channels[MBIA045_RAW_CHANNELS]);
void keyboard_rgb_apply(void);
uint32_t keyboard_rgb_apply_live(uint32_t gclk_hz);
/* Uniform RGB shared by all CTRL banks, using the measured slot map. */
void keyboard_rgb_set_uniform(uint16_t red, uint16_t green, uint16_t blue);
uint32_t keyboard_rgb_gclk_start(uint32_t hz);
void keyboard_rgb_gclk_stop(void);

/* One raw electrical address, no assumption about row or RGB mapping. */
int keyboard_rgb_probe(uint8_t ctrl, uint8_t level, uint8_t channel,
                       uint16_t brightness, uint32_t gclk_hz);

#endif
