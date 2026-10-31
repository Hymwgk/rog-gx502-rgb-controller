/* Dedicated device test: intentionally writes the settings EEPROM journal.
 * Build with -DSETTINGS_DEVICE_TEST_MODE=0 -DSETTINGS_DEVICE_TEST_LEVEL=3
 * to leave first-boot defaults after testing. */
#include "board.h"
#include "keyboard_settings.h"
#ifndef SETTINGS_DEVICE_TEST_MODE
#define SETTINGS_DEVICE_TEST_MODE 2
#endif
#ifndef SETTINGS_DEVICE_TEST_LEVEL
#define SETTINGS_DEVICE_TEST_LEVEL 0
#endif
volatile uint32_t keyboard_settings_device_test_result;
#define assert(condition) do { if (!(condition)) { keyboard_settings_device_test_result = __LINE__; return 1; } } while (0)
int main(void)
{
    board_init();
    static keyboard_controls_t c;
    static keyboard_controls_t restored;
    for (unsigned i=0; i<20; ++i) {
        c.mode = i % KEYBOARD_MODE_COUNT; c.brightness = i % 4;
        assert(keyboard_settings_save(&c) == 0);
        assert(keyboard_settings_load(&restored) == 0);
        assert(restored.mode == c.mode && restored.brightness == c.brightness);
    }
    c.mode = SETTINGS_DEVICE_TEST_MODE;
    c.brightness = SETTINGS_DEVICE_TEST_LEVEL;
    assert(keyboard_settings_save(&c) == 0);
    assert(keyboard_settings_load(&restored) == 0);
    assert(restored.mode == c.mode && restored.brightness == c.brightness);
    keyboard_settings_device_test_result = 0x600DCAFEU;
    return 0;
}
