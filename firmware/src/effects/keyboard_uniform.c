#include "keyboard_uniform.h"

/* Floor sqrt for the 26-bit squared RGB weights. Fixed work, no libm. */
static uint32_t sqrt_weights(uint32_t value)
{
    uint32_t root = 0U;
    for (uint32_t bit = 1U << 24; bit != 0U; bit >>= 2) {
        if (value >= root + bit) {
            value -= root + bit;
            root = (root >> 1) + bit;
        } else {
            root >>= 1;
        }
    }
    return root;
}

void keyboard_uniform_pixel(const keyboard_effect_config_t *config,
                            uint8_t mode, uint32_t time_ms, uint16_t rgb[3])
{
    uint32_t level = config->peak;
    uint8_t mask;
    switch (mode) {
    case KEYBOARD_MODE_WHITE: mask = 7U; break;
    case KEYBOARD_MODE_RED: mask = 1U; break;
    case KEYBOARD_MODE_GREEN: mask = 2U; break;
    case KEYBOARD_MODE_BLUE: mask = 4U; break;
    case KEYBOARD_MODE_COLOR_BREATHE: {
        uint32_t duration = config->interval_ms;
        uint32_t count = config->color_count;
        if (duration == 0U || count == 0U) { mask = 0U; break; }
        uint32_t phase = time_ms % duration;
        uint32_t color = (time_ms / duration) % count;
        uint32_t x = phase * 4096U / duration;
        /* Keep the full polynomial until the final shift: an intermediate
         * rounded x*x can introduce tiny backward steps near either endpoint. */
        uint32_t mix = (uint32_t)(((uint64_t)(x * x) * (12288U - 2U * x)) >> 24);
        uint8_t current = config->colors[color];
        uint8_t next = config->colors[(color + 1U) % count];
        uint32_t weights[3], squared = 0U;
        for (uint8_t c = 0U; c < 3U; ++c) {
            weights[c] = ((current & (1U << c)) ? 4096U - mix : 0U)
                       + ((next & (1U << c)) ? mix : 0U);
            squared += weights[c] * weights[c];
        }
        /* A Euclidean norm changes smoothly when two channels cross. The old
         * max-channel normalization had a derivative corner at every crossing.
         * Round the norm up so no channel exceeds the configured peak. */
        uint32_t scale = sqrt_weights(squared);
        if (scale * scale < squared) ++scale;
        for (uint8_t c = 0U; c < 3U; ++c)
            rgb[c] = scale ? (config->peak * weights[c] / scale) & ~63U : 0U;
        return;
    }
    default: mask = 0U; break;
    }
    level &= ~63U;
    rgb[0] = (mask & 1U) ? level : 0U;
    rgb[1] = (mask & 2U) ? level : 0U;
    rgb[2] = (mask & 4U) ? level : 0U;
}
