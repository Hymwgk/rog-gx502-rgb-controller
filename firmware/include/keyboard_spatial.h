#ifndef KEYBOARD_SPATIAL_H
#define KEYBOARD_SPATIAL_H
#include "keyboard_controls.h"
#include "keyboard_effect.h"

typedef struct {
    uint32_t last_ms;
    uint32_t phase_ms;
    uint32_t random;
    int16_t meteor_head[3];
    int16_t ripple_radius;
    uint8_t origin; /* Electrical slot of a mapped LED, 0..127. */
    uint8_t color;
    uint8_t mode;
} keyboard_spatial_state_t;

/* Prepare once per complete scan, using a clock that does not wrap with the
 * old three-color animation. Entropy is timing jitter, not hardware randomness. */
void keyboard_spatial_prepare(keyboard_spatial_state_t *state,
                              const keyboard_effect_config_t *config,
                              uint8_t mode, uint32_t now_ms, uint32_t entropy);
/* Unquantized build-profile RGB; caller applies the four-level scale once. */
void keyboard_spatial_pixel(const keyboard_spatial_state_t *state,
                            uint8_t bank, uint8_t slot, uint16_t rgb[3]);
/* Inline the pixel calculation into a complete bank to avoid 16 function calls
 * and temporary RGB stack traffic in the 512 us preparation window. */
void keyboard_spatial_render_bank(const keyboard_spatial_state_t *state,
                                  uint8_t bank, const keyboard_controls_t *controls,
                                  uint16_t frame[3][16]);
#endif
