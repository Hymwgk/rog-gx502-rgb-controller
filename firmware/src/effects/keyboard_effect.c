#include "keyboard_effect.h"

uint16_t keyboard_effect_level(const keyboard_effect_config_t *config, uint32_t step)
{
    uint32_t half = config->steps_per_half;
    uint32_t ramp = step <= half ? step : 2U * half - step;
    uint32_t value;
    if (config->curve == KEYBOARD_CURVE_SQUARE)
        value = (config->peak * ramp * ramp) / (half * half);
    else
        value = (config->peak * ramp) / half;
    return (uint16_t)(value & ~63U);
}

#if KB_TEST_MODE == 8
#include "keyboard_rgb.h"
#include "board.h"
#include "effect_config.h"
#define SYST_CSR (*(volatile uint32_t *)0xE000E010U)
#define SYST_RVR (*(volatile uint32_t *)0xE000E014U)
#define SYST_CVR (*(volatile uint32_t *)0xE000E018U)

volatile uint32_t keyboard_refresh_ticks;
static uint8_t refresh_ctrl;

void SysTick_Handler(void)
{
    refresh_ctrl = (refresh_ctrl + 1U) & 7U;
    keyboard_rgb_ctrl_only(refresh_ctrl, 1U);
    ++keyboard_refresh_ticks;
}

/* All banks share one uniform RGB frame; never select two CTRLs together. */
void keyboard_effect_run(void)
{
    const keyboard_effect_config_t *config = &keyboard_effect_config;
    board_use_fast_msi();
    /* 1024 GCLK periods per bank, a complete S-PWM subcycle. */
    uint32_t requested = config->gclk_hz;
    uint32_t actual = keyboard_rgb_gclk_start(requested);
    if (actual == 0U) return;
    uint32_t bank_ticks = (4194304U / actual) * 1024U;
    uint32_t hold_ticks = (actual * config->step_ms / 1000U + 1023U) / 1024U;
    keyboard_rgb_ctrl_only(0U, 1U);
    SYST_CSR = 0U;
    SYST_RVR = bank_ticks - 1U;
    SYST_CVR = 0U;
    SYST_CSR = 7U;
    for (;;) {
        for (uint8_t color = 0U; color < config->color_count; ++color) {
            for (uint32_t step = 0U; step <= 2U * config->steps_per_half; ++step) {
                uint16_t value = keyboard_effect_level(config, step);
                uint8_t color_mask = config->colors[color];
                keyboard_rgb_set_uniform((color_mask & 1U) ? value : 0U,
                                         (color_mask & 2U) ? value : 0U,
                                         (color_mask & 4U) ? value : 0U);
                uint32_t started = keyboard_refresh_ticks;
                if (keyboard_rgb_apply_live(requested) == 0U) {
                    SYST_CSR = 0U;
                    keyboard_rgb_emergency_off();
                    return;
                }
                while ((uint32_t)(keyboard_refresh_ticks - started) < hold_ticks) {
                    __asm volatile ("wfi");
                }
            }
        }
    }
}

#endif
