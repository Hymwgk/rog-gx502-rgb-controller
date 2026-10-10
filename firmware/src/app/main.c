#include <stdint.h>
#include "keyboard_rgb.h"
#include "keyboard_effect.h"
#include "keyboard_wave.h"

#ifndef KB_TEST_MODE
#define KB_TEST_MODE 0
#endif
#ifndef KB_TEST_CTRL
#define KB_TEST_CTRL 0
#endif
#ifndef KB_TEST_LEVEL
#define KB_TEST_LEVEL 1
#endif
#ifndef KB_TEST_CHANNEL
#define KB_TEST_CHANNEL 0
#endif
#ifndef KB_TEST_BRIGHTNESS
#define KB_TEST_BRIGHTNESS 1024
#endif
#ifndef KB_TEST_GCLK_HZ
#define KB_TEST_GCLK_HZ 100000
#endif
#ifndef KB_TEST_SECONDS
#define KB_TEST_SECONDS 20
#endif

#if KB_TEST_MODE != 0
#define SYST_CSR (*(volatile uint32_t *)0xE000E010U)
#define SYST_RVR (*(volatile uint32_t *)0xE000E014U)
#define SYST_CVR (*(volatile uint32_t *)0xE000E018U)

#if KB_TEST_MODE != 8 && KB_TEST_MODE != 9
static void delay_ms(uint32_t milliseconds)
{
    /* MCU reset MSI ~= 2.097 MHz. SysTick keeps the test time bounded. */
    SYST_CSR = 0U;
    SYST_RVR = 2097U - 1U;
    SYST_CVR = 0U;
    SYST_CSR = 5U;
    while (milliseconds-- != 0U) {
        while ((SYST_CSR & (1U << 16)) == 0U) { }
    }
    SYST_CSR = 0U;
}
#endif
#endif

#if KB_TEST_MODE == 6
/* Debugger-readable address; UINT32_MAX marks a transition. */
volatile uint32_t keyboard_scan_address = UINT32_MAX;
#endif


int main(void)
{
    keyboard_rgb_init();

#if KB_TEST_MODE == 1
    /* Electrical signal test only: CTRL pins remain high impedance. */
    keyboard_rgb_set_channel(0U, 0x5555U);
    for (uint32_t i = 0U; i < KB_TEST_SECONDS * 10U; ++i) {
        keyboard_rgb_apply();
        keyboard_rgb_gclk_start(KB_TEST_GCLK_HZ);
        delay_ms(50U);
        keyboard_rgb_gclk_stop();
        delay_ms(50U);
    }
    keyboard_rgb_emergency_off();
#elif KB_TEST_MODE == 2
    /* Explicit raw GPIO level, with all LED grayscale data zero. */
    keyboard_rgb_ctrl_set(KB_TEST_CTRL, KB_TEST_LEVEL);
    delay_ms(KB_TEST_SECONDS * 1000U);
    keyboard_rgb_emergency_off();
#elif KB_TEST_MODE == 3
    /* One electrical (CTRL, channel) address at limited brightness. */
    keyboard_rgb_probe(KB_TEST_CTRL, KB_TEST_LEVEL, KB_TEST_CHANNEL,
                       KB_TEST_BRIGHTNESS, KB_TEST_GCLK_HZ);
    delay_ms(KB_TEST_SECONDS * 1000U);
    keyboard_rgb_emergency_off();
#elif KB_TEST_MODE == 4
    /* Confirm that channels 0/16/32 mix at the same C-key LED. */
    keyboard_rgb_set_channel(0U, KB_TEST_BRIGHTNESS);
    keyboard_rgb_set_channel(16U, KB_TEST_BRIGHTNESS);
    keyboard_rgb_set_channel(32U, KB_TEST_BRIGHTNESS);
    keyboard_rgb_apply();
    keyboard_rgb_ctrl_only(0U, 1U);
    keyboard_rgb_gclk_start(KB_TEST_GCLK_HZ);
    delay_ms(KB_TEST_SECONDS * 1000U);
    keyboard_rgb_emergency_off();
#elif KB_TEST_MODE == 5
    /* Two confirmed electrical addresses: C and space, both channel 0. */
    keyboard_rgb_set_channel(0U, KB_TEST_BRIGHTNESS);
    keyboard_rgb_apply();
    for (uint32_t seconds = 0U; seconds < KB_TEST_SECONDS; ++seconds) {
        keyboard_rgb_gclk_stop();
        keyboard_rgb_ctrl_all_off();
        delay_ms(2U);
        keyboard_rgb_ctrl_only((seconds & 1U) ? 2U : 0U, 1U);
        keyboard_rgb_gclk_start(KB_TEST_GCLK_HZ);
        delay_ms(1000U);
    }
    keyboard_rgb_emergency_off();
#elif KB_TEST_MODE == 6
    /* Continuously scan every raw (CTRL, channel) address. */
    for (;;) {
        for (uint8_t ctrl = 0U; ctrl < 8U; ++ctrl) {
            for (uint8_t channel = 0U; channel < MBIA045_RAW_CHANNELS; ++channel) {
                keyboard_scan_address = UINT32_MAX;
                keyboard_rgb_emergency_off();
                if (keyboard_rgb_probe(ctrl, KB_TEST_LEVEL, channel,
                                       KB_TEST_BRIGHTNESS, KB_TEST_GCLK_HZ) != 0) {
                    keyboard_rgb_emergency_off();
                    for (;;) { __asm volatile ("wfi"); }
                }
                keyboard_scan_address = ctrl * MBIA045_RAW_CHANNELS + channel;
                delay_ms(500U);
            }
        }
    }
#elif KB_TEST_MODE == 7
    /* C key: confirmed CTRL0 and channels 0/16/32, known working steps. */
    static const uint16_t levels[] = { 128U, 256U, 512U, 1024U, 2048U,
                                       4096U, 8192U, 4096U, 2048U, 1024U,
                                       512U, 256U };
    keyboard_rgb_ctrl_only(0U, 1U);
    for (;;) {
        for (uint32_t i = 0U; i < sizeof(levels) / sizeof(levels[0]); ++i) {
            keyboard_rgb_set_channel(0U, levels[i]);
            keyboard_rgb_set_channel(16U, levels[i]);
            keyboard_rgb_set_channel(32U, levels[i]);
            keyboard_rgb_apply();
            if (keyboard_rgb_gclk_start(KB_TEST_GCLK_HZ) == 0U) {
                keyboard_rgb_emergency_off();
                for (;;) { __asm volatile ("wfi"); }
            }
            delay_ms(500U);
        }
    }
#elif KB_TEST_MODE == 8
    keyboard_effect_run();
#elif KB_TEST_MODE == 9
    keyboard_wave_run();
#endif

    for (;;) {
        __asm volatile ("wfi");
    }
}
