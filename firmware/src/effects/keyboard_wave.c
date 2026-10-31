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
#include "keyboard_controls.h"
#include "keyboard_settings.h"
#include "keyboard_uniform.h"
#include "keyboard_spatial.h"
#include "keyboard_rgb.h"
#if MBIA045_NUM_IC != 3
#error "The measured wave map requires three MBIA045 drivers"
#endif
#include "effect_config.h"
#include "keyboard_wave_tables.h"
#ifndef KB_TEST_FREEZE_ANIMATION
#define KB_TEST_FREEZE_ANIMATION 0
#endif
#ifndef KB_TEST_BANK_SUBCYCLES
#define KB_TEST_BANK_SUBCYCLES 0
#endif
#ifndef KB_TEST_WAVE_GCLK_HZ
#define KB_TEST_WAVE_GCLK_HZ keyboard_effect_config.gclk_hz
#endif
#if KB_TEST_BANK_SUBCYCLES < 0 || KB_TEST_BANK_SUBCYCLES > 8
#error "Bank duration: 0 for automatic, or 1..8 complete S-PWM subcycles"
#endif
#define SYST_CSR (*(volatile uint32_t *)0xE000E010U)
#define SYST_RVR (*(volatile uint32_t *)0xE000E014U)
#define SYST_CVR (*(volatile uint32_t *)0xE000E018U)

volatile uint32_t keyboard_wave_overruns;
volatile uint32_t keyboard_wave_max_work_ticks;
volatile uint32_t keyboard_wave_frames;
static mbia045_frame_t next_frame;
static uint16_t uniform_frame_rgb[3];
static uint32_t spatial_clock_ms;
keyboard_spatial_state_t keyboard_spatial_state = {.mode = 255U};
volatile uint32_t keyboard_settings_save_errors;
/* Debugger-readable state; stored settings override first-boot defaults. */
keyboard_controls_t keyboard_wave_controls = { .brightness = KEYBOARD_BRIGHTNESS_STEPS };

/* Keep the uniform-mode stack and temporary RGB values out of the measured
 * wave preparation path; inlining them increases register spills on Cortex-M0+. */
static __attribute__((noinline)) void render_uniform_bank(uint8_t bank, uint32_t time_ms)
{
    /* Latch one color per complete eight-bank scan. Sampling separately for
     * every bank can put adjacent banks on different quantization steps. */
    if (bank == 0U) {
        uint16_t rgb[3];
        keyboard_uniform_pixel(&keyboard_effect_config, keyboard_wave_controls.mode, time_ms, rgb);
        uniform_frame_rgb[0] = keyboard_controls_level(&keyboard_wave_controls, rgb[0]);
        uniform_frame_rgb[1] = keyboard_controls_level(&keyboard_wave_controls, rgb[1]);
        uniform_frame_rgb[2] = keyboard_controls_level(&keyboard_wave_controls, rgb[2]);
    }
    uint16_t red = uniform_frame_rgb[0];
    uint16_t green = uniform_frame_rgb[1];
    uint16_t blue = uniform_frame_rgb[2];
    for (uint8_t slot = 0U; slot < 16U; ++slot) {
        /* Preserve the measured map: unused electrical slots stay dark. */
        uint8_t mapped = keyboard_wave_arrivals[bank][slot] != UINT16_MAX;
        next_frame[0][slot] = mapped ? red : 0U;
        next_frame[1][slot] = mapped ? green : 0U;
        next_frame[2][slot] = mapped ? blue : 0U;
    }
}

static __attribute__((noinline)) void render_wave_bank(uint8_t bank, uint32_t time_ms)
{
    for (uint8_t slot = 0U; slot < 16U; ++slot) {
        uint32_t arrival = keyboard_wave_arrivals[bank][slot];
        const uint16_t *rgb = 0;
        if (arrival != UINT16_MAX) {
            if (keyboard_wave_controls.mode == KEYBOARD_MODE_REVERSE)
                arrival = keyboard_effect_config.travel_ms - arrival;
            else if (keyboard_wave_controls.mode == KEYBOARD_MODE_UNIFORM)
                arrival = 0U;
        }
        if (arrival != UINT16_MAX && time_ms >= arrival) {
            uint32_t local = time_ms - arrival;
            uint32_t interval = keyboard_effect_config.interval_ms;
            while (local >= interval * 3U) local -= interval * 3U;
            uint8_t wave = 0U;
            if (local >= interval * 2U) { local -= interval * 2U; wave = 2U; }
            else if (local >= interval) { local -= interval; wave = 1U; }
            rgb = keyboard_wave_samples[wave][local >> 3];
        }
        if (rgb) {
            next_frame[0][slot] = keyboard_controls_level(&keyboard_wave_controls, rgb[0]);
            next_frame[1][slot] = keyboard_controls_level(&keyboard_wave_controls, rgb[1]);
            next_frame[2][slot] = keyboard_controls_level(&keyboard_wave_controls, rgb[2]);
        } else {
            next_frame[0][slot] = next_frame[1][slot] = next_frame[2][slot] = 0U;
        }
    }
}

static __attribute__((noinline)) void render_spatial_bank(uint8_t bank)
{
    if (bank == 0U)
        keyboard_spatial_prepare(&keyboard_spatial_state, &keyboard_effect_config,
                                 keyboard_wave_controls.mode, spatial_clock_ms, SYST_CVR);
    keyboard_spatial_render_bank(&keyboard_spatial_state, bank,
                                  &keyboard_wave_controls, next_frame);
}

static void render_bank(uint8_t bank, uint32_t time_ms)
{
    if (keyboard_wave_controls.mode >= KEYBOARD_MODE_METEOR) {
        render_spatial_bank(bank);
    } else {
        keyboard_spatial_state.mode = 255U;
        if (keyboard_wave_controls.mode >= KEYBOARD_MODE_WHITE)
            render_uniform_bank(bank, time_ms);
        else
            render_wave_bank(bank, time_ms);
    }
}

void keyboard_wave_run(void)
{
    keyboard_settings_load(&keyboard_wave_controls);
    if (board_use_wave_clock() != 0) {
        keyboard_rgb_emergency_off();
        return;
    }
    /* Free-running cycle counter; no scan interrupt races serial data/LE. */
    SYST_CSR = 0U;
    SYST_RVR = 0xFFFFFFU;
    SYST_CVR = 0U;
    SYST_CSR = 5U;
    uint32_t requested = KB_TEST_WAVE_GCLK_HZ;
    uint32_t actual = keyboard_rgb_gclk_start(requested);
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
    keyboard_rgb_gclk_start(requested);
    uint32_t started = SYST_CVR;
    uint32_t previous = started;
#if KB_TEST_HOLD_BANK
    /* Retain bank zero's mapped mixed-color load, with no scan or relatch.
     * Accumulate short counter deltas to handle the 24-bit SysTick wrap. */
    uint32_t hold_elapsed = 0U;
#if KB_TEST_HOLD_BANK >= 2
    uint8_t hold_stage = 0U;
    uint32_t hold_low_hz = KB_TEST_HOLD_BANK == 4 ? 4000000U :
                           KB_TEST_HOLD_BANK == 3 ? 2000000U : 1000000U;
    keyboard_rgb_gclk_stop();
    keyboard_rgb_gclk_start(hold_low_hz);
    previous = SYST_CVR;
    for (;;) {
        uint32_t now = SYST_CVR;
        hold_elapsed += (previous - now) & 0xFFFFFFU;
        previous = now;
        if (hold_elapsed >= (hold_stage ? 64000000U : 160000000U)) {
            hold_stage ^= 1U;
            keyboard_rgb_gclk_stop();
            keyboard_rgb_gclk_start(hold_stage ? hold_low_hz * 2U : hold_low_hz);
            hold_elapsed = 0U;
            previous = SYST_CVR;
        }
    }
#else
    while (hold_elapsed < 1920000000U) {
        uint32_t now = SYST_CVR;
        hold_elapsed += (previous - now) & 0xFFFFFFU;
        previous = now;
    }
    keyboard_rgb_emergency_off();
    return;
#endif
#endif
    uint32_t remainder = 0U;
    uint32_t buttons_ms = 0U;
    /* Higher GCLK repeats complete PWM subcycles within the original 512 us
     * preparation budget; never shorten the bank to 256 us at 4 MHz. */
    uint32_t subcycles = KB_TEST_BANK_SUBCYCLES;
    if (subcycles == 0U) subcycles = actual >= 2000000U ? actual / 2000000U : 1U;
    uint32_t bank_ticks = (32000000U / actual) * 1024U * subcycles;
#if KB_TEST_NOISE_AB
    /* Mode 1: 1/2 MHz at equal bank duration; mode 2: bank duration
     * at 2 MHz; modes 3/4: 2/8 or 2/4 MHz at original bank duration.
     * A lasts 5 s and B lasts 2 s. */
    uint32_t noise_stage_ticks = 0U;
    uint8_t noise_stage = 0U;
    uint32_t noise_hz = KB_TEST_NOISE_AB >= 2 ? 2000000U : 1000000U;
    bank_ticks = KB_TEST_NOISE_AB >= 2 ? 16384U : 32768U;
    keyboard_rgb_gclk_stop();
    keyboard_rgb_gclk_start(noise_hz);
    started = SYST_CVR;
    previous = started;
#endif
    uint8_t bank = 0U;
    uint8_t save_pending = 0U;
    for (;;) {
        uint8_t next = (bank + 1U) & 7U;
        /* Poll outside interrupts, once per complete scan, using real time even
         * when the animation is frozen for acoustic diagnostics. */
        if (next == 0U) {
            uint8_t old_mode = keyboard_wave_controls.mode;
            uint8_t old_brightness = keyboard_wave_controls.brightness;
            keyboard_controls_update(&keyboard_wave_controls, board_buttons_pressed(), buttons_ms);
            save_pending = old_mode != keyboard_wave_controls.mode ||
                           old_brightness != keyboard_wave_controls.brightness;
        }
        /* Prepare the next bank while the current bank remains visible. */
        render_bank(next, time_ms);
        mbia045_shift_frame_fast(next_frame);
        uint32_t work = (started - SYST_CVR) & 0xFFFFFFU;
        if (work > keyboard_wave_max_work_ticks) keyboard_wave_max_work_ticks = work;
        if (work > bank_ticks) ++keyboard_wave_overruns;
        while (((started - SYST_CVR) & 0xFFFFFFU) < bank_ticks) { }
        keyboard_rgb_gclk_stop();
        keyboard_rgb_ctrl_all_off();
        if (save_pending) {
            /* EEPROM programming stalls Flash execution. Keep all banks off
             * during the save so no one bank is left illuminated too long. */
            if (keyboard_settings_save(&keyboard_wave_controls) != 0)
                ++keyboard_settings_save_errors;
            save_pending = 0U;
        }
        mbia045_global_latch();
        keyboard_rgb_ctrl_only(next, 1U);
#if KB_TEST_NOISE_AB
        uint32_t noise_duration = noise_stage ? 64000000U : 160000000U;
        if (next == 0U && noise_stage_ticks >= noise_duration) {
            noise_stage ^= 1U;
            noise_stage_ticks = 0U;
            if (KB_TEST_NOISE_AB == 2) {
                bank_ticks = noise_stage ? 32768U : 16384U;
            } else if (KB_TEST_NOISE_AB == 3) {
                noise_hz = noise_stage ? 8000000U : 2000000U;
            } else if (KB_TEST_NOISE_AB == 4) {
                noise_hz = noise_stage ? 4000000U : 2000000U;
            } else {
                noise_hz = noise_stage ? 2000000U : 1000000U;
            }
        }
        keyboard_rgb_gclk_start(noise_hz);
#else
        keyboard_rgb_gclk_start(requested);
#endif
        started = SYST_CVR;
        uint32_t elapsed = (previous - started) & 0xFFFFFFU;
        previous = started;
#if KB_TEST_NOISE_AB
        noise_stage_ticks += elapsed;
#endif
        remainder += elapsed;
        /* Acoustic diagnostic: freeze pixels while retaining normal scan/latch timing. */
        buttons_ms += remainder / 32000U;
        if (!KB_TEST_FREEZE_ANIMATION) {
            time_ms += remainder / 32000U;
            spatial_clock_ms += remainder / 32000U;
        }
        remainder %= 32000U;
        /* Avoid 32-bit time wrap while preserving the complete three-wave phase. */
        if (time_ms > keyboard_effect_config.travel_ms + period * 2U) time_ms -= period;
        bank = next;
        if (bank == 0U) ++keyboard_wave_frames;
    }
}
#endif
