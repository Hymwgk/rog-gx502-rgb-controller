#include "keyboard_settings.h"
#include "board.h"

#define SETTINGS_MAGIC 0x4B420000U /* format version 1: KB, sequence, mode/level */
#define SETTINGS_SLOTS (BOARD_SETTINGS_WORDS / 2U)

static int latest(uint32_t *record)
{
    int found = -1;
    for (uint8_t slot = 0U; slot < SETTINGS_SLOTS; ++slot) {
        uint32_t data = board_settings_read(slot * 2U);
        if ((data & 0xFFFF0000U) != SETTINGS_MAGIC ||
            board_settings_read(slot * 2U + 1U) != ~data ||
            ((data >> 4) & 15U) >= KEYBOARD_MODE_COUNT ||
            (data & 15U) > KEYBOARD_BRIGHTNESS_STEPS) continue;
        uint8_t distance = (uint8_t)((data >> 8) - (*record >> 8));
        if (found < 0 || (distance != 0U && distance < 128U)) {
            *record = data;
            found = slot;
        }
    }
    return found;
}

int keyboard_settings_load(keyboard_controls_t *controls)
{
    uint32_t data = 0U;
    if (latest(&data) < 0) return -1;
    controls->mode = (data >> 4) & 15U;
    controls->brightness = data & 15U;
    return 0;
}

int keyboard_settings_save(const keyboard_controls_t *controls)
{
    if (controls->mode >= KEYBOARD_MODE_COUNT ||
        controls->brightness > KEYBOARD_BRIGHTNESS_STEPS) return -1;
    uint32_t old = 0U;
    int slot = latest(&old);
    uint8_t value = (controls->mode << 4) | controls->brightness;
    if (slot >= 0 && (old & 255U) == value) return 0;
    uint8_t sequence = slot < 0 ? 0U : (uint8_t)((old >> 8) + 1U);
    slot = slot < 0 ? 0 : (slot + 1) % SETTINGS_SLOTS;
    uint32_t data = SETTINGS_MAGIC | ((uint32_t)sequence << 8) | value;
    /* Commit the complement last; never overwrite the latest good slot.
     * Interrupted writes leave the previous complete record available. */
    if (board_settings_write(slot * 2U, data) != 0 ||
        board_settings_write(slot * 2U + 1U, ~data) != 0) return -1;
    return 0;
}
