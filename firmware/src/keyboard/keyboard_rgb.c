#include "keyboard_rgb.h"
#include "board.h"

static mbia045_frame_t frame;

void keyboard_rgb_set_uniform(uint16_t red, uint16_t green, uint16_t blue)
{
    /* docs/KEYMAP.md: union of populated slots across the eight CTRL banks.
       Uniform color permits one shared frame without rewriting each bank. */
    static const uint16_t populated[8] = {
        0x77EFU, 0xF7E8U, 0xFFFFU, 0x7FFFU,
        0xE7FDU, 0xF5FEU, 0xFDFAU, 0xFFFDU
    };
    uint16_t slots = 0U;
    for (uint8_t ctrl = 0U; ctrl < 8U; ++ctrl) slots |= populated[ctrl];
    keyboard_rgb_set_all_channels(0U);
    for (uint8_t n = 0U; n < 16U; ++n) {
        if (slots & (1U << n)) {
            keyboard_rgb_set_channel(n, red);
            keyboard_rgb_set_channel(n + 16U, green);
            keyboard_rgb_set_channel(n + 32U, blue);
        }
    }
}

void keyboard_rgb_ctrl_all_off(void) { board_ctrl_release_all(); }

int keyboard_rgb_ctrl_set(uint8_t index, uint8_t level)
{
    return board_ctrl_drive(index, level);
}

int keyboard_rgb_ctrl_only(uint8_t index, uint8_t level)
{
    if (index >= 8U || level > 1U) return -1;
    board_ctrl_release_all();
    return board_ctrl_drive(index, level);
}

int keyboard_rgb_ctrl_set_mask(uint8_t mask, uint8_t level)
{
    if (level > 1U) return -1;
    board_ctrl_release_all();
    for (uint8_t i = 0U; i < 8U; ++i) {
        if (mask & (1U << i)) board_ctrl_drive(i, level);
    }
    return 0;
}

int keyboard_rgb_set_channel(uint8_t index, uint16_t brightness)
{
    if (index >= MBIA045_RAW_CHANNELS) return -1;
    frame[index / MBIA045_CH_PER_IC][index % MBIA045_CH_PER_IC] = brightness;
    return 0;
}

void keyboard_rgb_set_all_channels(uint16_t brightness)
{
    for (uint32_t ic = 0; ic < MBIA045_NUM_IC; ++ic) {
        for (uint32_t ch = 0; ch < MBIA045_CH_PER_IC; ++ch) {
            frame[ic][ch] = brightness;
        }
    }
}

void keyboard_rgb_set_raw48(const uint16_t channels[MBIA045_RAW_CHANNELS])
{
    if (!channels) return;
    for (uint32_t i = 0; i < MBIA045_RAW_CHANNELS; ++i) {
        frame[i / MBIA045_CH_PER_IC][i % MBIA045_CH_PER_IC] = channels[i];
    }
}

void keyboard_rgb_apply(void) { mbia045_send_frame(frame); }
uint32_t keyboard_rgb_apply_live(uint32_t hz) { return mbia045_send_frame_live(frame, hz); }
uint32_t keyboard_rgb_gclk_start(uint32_t hz) { return mbia045_gclk_start(hz); }
void keyboard_rgb_gclk_stop(void) { mbia045_gclk_stop(); }

void keyboard_rgb_emergency_off(void)
{
    board_ctrl_release_all();
    mbia045_gclk_stop();
    keyboard_rgb_set_all_channels(0U);
    keyboard_rgb_apply();
}

void keyboard_rgb_init(void)
{
    mbia045_init();
    keyboard_rgb_emergency_off();
}

int keyboard_rgb_probe(uint8_t ctrl, uint8_t level, uint8_t channel,
                       uint16_t brightness, uint32_t gclk_hz)
{
    if (ctrl >= 8U || level > 1U || channel >= MBIA045_RAW_CHANNELS ||
        brightness > 0x4000U) return -1;
    keyboard_rgb_emergency_off();
    keyboard_rgb_set_channel(channel, brightness);
    keyboard_rgb_apply();
    if (keyboard_rgb_ctrl_only(ctrl, level) != 0) return -1;
    if (keyboard_rgb_gclk_start(gclk_hz) == 0U) {
        keyboard_rgb_emergency_off();
        return -1;
    }
    return 0;
}
