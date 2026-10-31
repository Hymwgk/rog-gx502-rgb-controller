#include "keyboard_spatial.h"
#if KB_TEST_MODE == 9
#include "keyboard_spatial_tables.h"

static uint32_t next_random(keyboard_spatial_state_t *state)
{
    uint32_t value = state->random;
    if (value == 0U) value = 0x6D2B79F5U;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    state->random = value;
    return value;
}

static void choose_origin(keyboard_spatial_state_t *state)
{
    uint32_t index = next_random(state) % SPATIAL_MAPPED_COUNT;
    if (spatial_mapped_ids[index] == state->origin)
        index = (index + 1U) % SPATIAL_MAPPED_COUNT;
    state->origin = spatial_mapped_ids[index];
}

void keyboard_spatial_prepare(keyboard_spatial_state_t *state,
                              const keyboard_effect_config_t *config,
                              uint8_t mode, uint32_t now_ms, uint32_t entropy)
{
    uint32_t interval = config->interval_ms;
    if (interval == 0U) interval = 3600U;
    if (state->mode != mode) {
        state->mode = mode;
        state->phase_ms = 0U;
        state->random ^= entropy ^ now_ms ^ 0xA341316CU;
        state->color = next_random(state) % 3U;
        choose_origin(state);
    } else {
        state->phase_ms += now_ms - state->last_ms;
    }
    state->last_ms = now_ms;
    if (mode == KEYBOARD_MODE_METEOR) {
        uint32_t period = interval * 3U;
        if (state->phase_ms >= period) state->phase_ms %= period;
        uint32_t duration = config->travel_ms;
        if (duration == 0U || duration > period) duration = period;
        for (uint8_t i = 0U; i < 3U; ++i) {
            uint32_t age = state->phase_ms + period - i * interval;
            if (age >= period) age -= period;
            state->meteor_head[i] = age >= duration ? -32768 :
                (int32_t)(age * (SPATIAL_WIDTH + 256U) / duration) - 24;
        }
    } else if (mode == KEYBOARD_MODE_RIPPLE) {
        if (state->phase_ms >= interval) {
            state->phase_ms %= interval;
            choose_origin(state);
            state->color = (state->color + 1U) % 3U;
        }
        /* Front and tail both leave the farthest LED before choosing a new
         * center. The source cannot teleport while its old ring is visible. */
        state->ripple_radius = (int32_t)(state->phase_ms *
                                  (SPATIAL_DISTANCE_MAX + 64U) / interval) - 8;
    }
}

static inline __attribute__((always_inline)) void spatial_pixel(const keyboard_spatial_state_t *state,
                            uint8_t bank, uint8_t slot, uint16_t rgb[3])
{
    rgb[0] = rgb[1] = rgb[2] = 0U;
    if (bank >= 8U || slot >= 16U) return;
    uint8_t id = bank * 16U + slot;
    uint8_t lane = spatial_lane[id];
    if (lane == 255U) return;
    if (state->mode == KEYBOARD_MODE_METEOR) {
        int32_t index = (int32_t)state->meteor_head[lane] - spatial_x[id] + 24;
        if (index < 0 || index >= 256) return;
        uint32_t gain = (uint32_t)spatial_gain[id] + 1U;
        const uint16_t *sample = spatial_meteor[lane][index];
        rgb[0] = sample[0] * gain >> 8;
        rgb[1] = sample[1] * gain >> 8;
        rgb[2] = sample[2] * gain >> 8;
    } else if (state->mode == KEYBOARD_MODE_RIPPLE) {
        uint32_t distance = spatial_distances[state->origin][id];
        int32_t index = state->ripple_radius - (int32_t)distance + 8;
        if (index < 0 || index >= 64) return;
        const uint16_t *sample = spatial_ripple[state->color][index];
        rgb[0] = sample[0];
        rgb[1] = sample[1];
        rgb[2] = sample[2];
    }
}
void keyboard_spatial_pixel(const keyboard_spatial_state_t *state,
                            uint8_t bank, uint8_t slot, uint16_t rgb[3])
{
    spatial_pixel(state, bank, slot, rgb);
}

void keyboard_spatial_render_bank(const keyboard_spatial_state_t *state,
                                  uint8_t bank, const keyboard_controls_t *controls,
                                  uint16_t frame[3][16])
{
    for (uint8_t slot = 0U; slot < 16U; ++slot) {
        uint16_t rgb[3];
        spatial_pixel(state, bank, slot, rgb);
        frame[0][slot] = keyboard_controls_level(controls, rgb[0]);
        frame[1][slot] = keyboard_controls_level(controls, rgb[1]);
        frame[2][slot] = keyboard_controls_level(controls, rgb[2]);
    }
}
#endif
