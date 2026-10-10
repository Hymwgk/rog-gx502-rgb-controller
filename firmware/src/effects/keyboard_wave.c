#include "keyboard_wave.h"

static uint32_t smoothstep(uint32_t x)
{
    return x * x * (3072U - 2U * x) / (1024U * 1024U);
}

static void tail_pixel(const keyboard_effect_config_t *config, uint32_t wave,
                       uint32_t tail, uint16_t rgb[3])
{
    rgb[0] = rgb[1] = rgb[2] = 0U;
    if (tail >= config->tail_ms) return;
    uint32_t remaining = (config->tail_ms - tail) * 255U / config->tail_ms;
    uint32_t gain = remaining * remaining / 255U;
    uint32_t mix = tail * 255U / config->tail_ms;
    uint32_t value = config->peak * gain / 255U;
    uint8_t current = config->colors[wave % config->color_count];
    uint8_t next = config->colors[(wave + 1U) % config->color_count];
    for (uint8_t channel = 0U; channel < 3U; ++channel) {
        uint32_t a = (current & (1U << channel)) ? 255U : 0U;
        uint32_t b = (next & (1U << channel)) ? 255U : 0U;
        uint32_t color = (a * (255U - mix) + b * mix) / 255U;
        rgb[channel] = value * color / 255U;
    }
}

void keyboard_wave_pixel(const keyboard_effect_config_t *config,
                         uint16_t projection, uint32_t time_ms, uint16_t rgb[3])
{
    rgb[0] = rgb[1] = rgb[2] = 0U;
    if (projection == UINT16_MAX) return;
    uint32_t arrival = (projection * config->travel_ms) / 1024U;
    if (time_ms < arrival) return;
    uint32_t local = time_ms - arrival;
    uint32_t wave = local / config->interval_ms;
    uint32_t age = local % config->interval_ms;
    if (config->wave_smooth) {
        uint32_t phase = (age + config->interval_ms - config->crest_ms) % config->interval_ms;
        uint32_t index = (wave + config->color_count - (age < config->crest_ms)) % config->color_count;
        uint32_t mix = smoothstep(phase * 1024U / config->interval_ms);
        uint32_t distance = phase < config->interval_ms - phase ? phase : config->interval_ms - phase;
        uint32_t x = distance * 1024U / config->tail_ms;
        if (x > 1024U) x = 1024U;
        uint32_t shoulder = 1024U - smoothstep(x);
        uint32_t gain = shoulder * shoulder / 1024U;
        uint32_t value = config->floor + (config->peak - config->floor) * gain / 1024U;
        uint8_t a = config->colors[index];
        uint8_t b = config->colors[(index + 1U) % config->color_count];
        uint32_t weights[3], scale = 1024U;
        for (uint8_t c = 0U; c < 3U; ++c) {
            weights[c] = ((a & (1U << c)) ? 1024U - mix : 0U)
                       + ((b & (1U << c)) ? mix : 0U);
        }
        if (config->wave_smooth == 3U) {
            uint32_t u = phase * 1024U / config->interval_ms;
            uint32_t u2 = u * u / 1024U, v = 1024U - u;
            uint32_t u3 = u2 * u / 1024U;
            uint32_t wp = (v * v / 1024U * v / 1024U + u3) / 6U;
            uint32_t wa = (4096U - 6U * u2 + 3U * u3) / 6U;
            uint32_t blend = config->color_blend;
            wp = (wp * (1024U - blend) * 3U + 1024U * blend) / 3072U;
            wa = (wa * (1024U - blend) * 3U + 1024U * blend) / 3072U;
            uint32_t wb = 1024U - wp - wa;
            uint8_t previous = config->colors[(index + config->color_count - 1U) % config->color_count];
            for (uint8_t c = 0U; c < 3U; ++c)
                weights[c] = ((previous & (1U << c)) ? wp : 0U)
                           + ((a & (1U << c)) ? wa : 0U)
                           + ((b & (1U << c)) ? wb : 0U);
        }
        if (config->wave_smooth >= 2U) {
            scale = 0U;
            for (uint8_t c = 0U; c < 3U; ++c)
                if (weights[c] > scale) scale = weights[c];
        }
        for (uint8_t c = 0U; c < 3U; ++c)
            rgb[c] = (value * weights[c] / scale) & ~63U;
        return;
    }
    uint8_t current = config->colors[wave % config->color_count];
    if (age < config->crest_ms) {
        uint32_t rise = age * 255U / config->crest_ms;
        tail_pixel(config, (wave + config->color_count - 1U) % config->color_count,
                   age + config->interval_ms - config->crest_ms, rgb);
        for (uint8_t channel = 0U; channel < 3U; ++channel) {
            uint32_t head = (current & (1U << channel)) ? config->peak * rise / 255U : 0U;
            rgb[channel] = head + rgb[channel] * (255U - rise) / 255U;
        }
    } else {
        tail_pixel(config, wave, age - config->crest_ms, rgb);
    }
    for (uint8_t channel = 0U; channel < 3U; ++channel) rgb[channel] &= ~63U;
}

#if KB_TEST_MODE == 9
#include "board.h"
#include "keyboard_rgb.h"
#if MBIA045_NUM_IC != 3
#error "The measured wave map requires three MBIA045 drivers"
#endif
#include "effect_config.h"
#include "keyboard_wave_tables.h"
#define SYST_CSR (*(volatile uint32_t *)0xE000E010U)
#define SYST_RVR (*(volatile uint32_t *)0xE000E014U)
#define SYST_CVR (*(volatile uint32_t *)0xE000E018U)

volatile uint32_t keyboard_wave_overruns;
volatile uint32_t keyboard_wave_max_work_ticks;
volatile uint32_t keyboard_wave_frames;
static mbia045_frame_t next_frame;

static void render_bank(uint8_t bank, uint32_t time_ms)
{
    for (uint8_t slot = 0U; slot < 16U; ++slot) {
        uint32_t arrival = keyboard_wave_arrivals[bank][slot];
        const uint16_t *rgb = 0;
        if (arrival != UINT16_MAX && time_ms >= arrival) {
            uint32_t local = time_ms - arrival;
            uint32_t interval = keyboard_effect_config.interval_ms;
            while (local >= interval * 3U) local -= interval * 3U;
            uint8_t wave = 0U;
            if (local >= interval * 2U) { local -= interval * 2U; wave = 2U; }
            else if (local >= interval) { local -= interval; wave = 1U; }
            rgb = keyboard_wave_samples[wave][local >> 3];
        }
        for (uint8_t color = 0U; color < 3U; ++color)
            next_frame[color][slot] = rgb ? rgb[color] : 0U;
    }
}

void keyboard_wave_run(void)
{
    if (board_use_wave_clock() != 0) {
        keyboard_rgb_emergency_off();
        return;
    }
    /* Free-running cycle counter; no scan interrupt races serial data/LE. */
    SYST_CSR = 0U;
    SYST_RVR = 0xFFFFFFU;
    SYST_CVR = 0U;
    SYST_CSR = 5U;
    uint32_t actual = keyboard_rgb_gclk_start(keyboard_effect_config.gclk_hz);
    if (actual == 0U) { keyboard_rgb_emergency_off(); return; }
    keyboard_rgb_gclk_stop();
    /* Begin in steady state: a whole color cycle preserves the origin phase,
     * and enough cycles ensure the wave has already reached every key. */
    uint32_t period = keyboard_effect_config.interval_ms * keyboard_effect_config.color_count;
    uint32_t time_ms = ((keyboard_effect_config.travel_ms + period - 1U) / period) * period;
    render_bank(0U, time_ms);
    mbia045_shift_frame_fast(next_frame);
    mbia045_global_latch();
    keyboard_rgb_ctrl_only(0U, 1U);
    keyboard_rgb_gclk_start(keyboard_effect_config.gclk_hz);
    uint32_t started = SYST_CVR;
    uint32_t previous = started;
    uint32_t remainder = 0U;
    uint32_t bank_ticks = (32000000U / actual) * 1024U;
    uint8_t bank = 0U;
    for (;;) {
        uint8_t next = (bank + 1U) & 7U;
        /* Prepare the next bank while the current bank remains visible. */
        render_bank(next, time_ms);
        mbia045_shift_frame_fast(next_frame);
        uint32_t work = (started - SYST_CVR) & 0xFFFFFFU;
        if (work > keyboard_wave_max_work_ticks) keyboard_wave_max_work_ticks = work;
        if (work > bank_ticks) ++keyboard_wave_overruns;
        while (((started - SYST_CVR) & 0xFFFFFFU) < bank_ticks) { }
        keyboard_rgb_gclk_stop();
        keyboard_rgb_ctrl_all_off();
        mbia045_global_latch();
        keyboard_rgb_ctrl_only(next, 1U);
        keyboard_rgb_gclk_start(keyboard_effect_config.gclk_hz);
        started = SYST_CVR;
        uint32_t elapsed = (previous - started) & 0xFFFFFFU;
        previous = started;
        remainder += elapsed;
        time_ms += remainder / 32000U;
        remainder %= 32000U;
        /* Avoid 32-bit time wrap while preserving the complete three-wave phase. */
        if (time_ms > keyboard_effect_config.travel_ms + period * 2U) time_ms -= period;
        bank = next;
        if (bank == 0U) ++keyboard_wave_frames;
    }
}
#endif
