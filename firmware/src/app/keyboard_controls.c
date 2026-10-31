#include "keyboard_controls.h"

void keyboard_controls_update(keyboard_controls_t *controls,
                              uint8_t pressed, uint32_t now_ms)
{
    uint8_t events = 0U;
    pressed &= 15U;
    for (uint8_t i = 0U; i < 4U; ++i) {
        uint8_t bit = 1U << i;
        if ((pressed ^ controls->candidate) & bit) {
            controls->candidate ^= bit;
            controls->changed_ms[i] = now_ms;
        }
        if (((controls->candidate ^ controls->stable) & bit) &&
            (uint32_t)(now_ms - controls->changed_ms[i]) >= KEYBOARD_BUTTON_DEBOUNCE_MS) {
            controls->stable ^= bit;
            events |= controls->stable & bit;
        }
    }
    /* Include raw inputs so staggered debounce cannot turn a chord into an action. */
    uint8_t held = pressed | controls->stable;
    if ((held & 3U) != 3U) {
        if (events & 1U)
            controls->mode = (controls->mode + KEYBOARD_MODE_COUNT - 1U) % KEYBOARD_MODE_COUNT;
        if (events & 2U)
            controls->mode = (controls->mode + 1U) % KEYBOARD_MODE_COUNT;
    }
    if ((held & 12U) != 12U) {
        if (events & 4U)
            controls->brightness = (controls->brightness + 1U) & 3U;
        if (events & 8U)
            controls->brightness = (controls->brightness + 3U) & 3U;
    }
}
