#include <stdint.h>
#include "keyboard_controls.h"
#include "keyboard_settings.h"
#include "keyboard_uniform.h"
#include "board.h"
volatile uint32_t keyboard_controls_test_result;
#define assert(condition) do { if (!(condition)) { keyboard_controls_test_result = __LINE__; return 1; } } while (0)
static void press(keyboard_controls_t *c, uint8_t keys, uint32_t *t) {
    keyboard_controls_update(c, keys, ++*t);
    *t += 20;
    keyboard_controls_update(c, keys, *t);
    keyboard_controls_update(c, 0, ++*t);
    *t += 20;
    keyboard_controls_update(c, 0, *t);
}
/* Journal tests use RAM, including simulated failures between word writes. */
static uint32_t storage[BOARD_SETTINGS_WORDS];
static int writes_until_failure = -1;
static unsigned writes;
uint32_t board_settings_read(uint8_t word) { return storage[word]; }
int board_settings_write(uint8_t word, uint32_t value) {
    if (writes_until_failure == 0) return -1;
    if (writes_until_failure > 0) --writes_until_failure;
    storage[word] = value;
    ++writes;
    return 0;
}
int main(void) {
    static keyboard_controls_t c = {.brightness = 3};
    uint32_t t = 0;
    keyboard_controls_update(&c, 2, 0);
    keyboard_controls_update(&c, 0, 4);
    keyboard_controls_update(&c, 2, 8);
    keyboard_controls_update(&c, 2, 27);
    assert(c.mode == 0);
    keyboard_controls_update(&c, 2, 28);
    assert(c.mode == 1);
    keyboard_controls_update(&c, 2, 1000);
    assert(c.mode == 1); /* held key must not repeat */
    keyboard_controls_update(&c, 0, 1001);
    keyboard_controls_update(&c, 0, 1021);
    t = 1021;
    press(&c, 1, &t); assert(c.mode == 0);
    press(&c, 1, &t); assert(c.mode == KEYBOARD_MODE_COUNT - 1);
    press(&c, 2, &t); assert(c.mode == 0);
    for (unsigned i=0; i<3; ++i) press(&c, 8, &t);
    assert(c.brightness == 0 && keyboard_controls_level(&c, 3072) == 0);
    press(&c, 8, &t); assert(c.brightness == 3); /* dim past off wraps */
    press(&c, 4, &t); assert(c.brightness == 0); /* brighten past max wraps */
    for (unsigned i=1; i<=3; ++i) {
        press(&c, 4, &t);
        assert(c.brightness == i);
        assert(keyboard_controls_level(&c, 3072) == 1024U * i);
    }
    for (unsigned i=0; i<20; ++i) press(&c, 4, &t);
    assert(c.brightness == 3);
    press(&c, 8, &t); assert(c.brightness == 2);
    press(&c, 15, &t); assert(c.brightness == 2 && c.mode == 0);
    for (c.brightness=0; c.brightness<=3; ++c.brightness)
        for (unsigned level=0; level<=16384; level+=64) {
            unsigned scaled = keyboard_controls_level(&c, level);
            assert(scaled == ((level * c.brightness / 3U) & ~63U));
        }
    c.candidate = c.stable = c.mode = 0;
    c.brightness = 3;
    keyboard_controls_update(&c, 8, UINT32_MAX-10);
    keyboard_controls_update(&c, 8, 8);
    assert(c.brightness == 3);
    keyboard_controls_update(&c, 8, 9);
    assert(c.brightness == 2); /* timestamp wrap */
    static keyboard_controls_t restored = {.brightness=3};
    assert(keyboard_settings_load(&restored) == -1 && restored.brightness == 3);
    c.mode = 2; c.brightness = 0;
    assert(keyboard_settings_save(&c) == 0);
    assert(keyboard_settings_load(&restored) == 0 && restored.mode == 2 && restored.brightness == 0);
    unsigned before = writes;
    assert(keyboard_settings_save(&c) == 0 && writes == before);
    c.mode = 1; c.brightness = 2;
    writes_until_failure = 0;
    assert(keyboard_settings_save(&c) == -1);
    assert(keyboard_settings_load(&restored) == 0 && restored.mode == 2 && restored.brightness == 0);
    writes_until_failure = 1; /* payload written, commit interrupted */
    assert(keyboard_settings_save(&c) == -1);
    assert(keyboard_settings_load(&restored) == 0 && restored.mode == 2 && restored.brightness == 0);
    writes_until_failure = -1;
    /* More than 16 slots and 256 sequence values exercises both wraparounds. */
    for (unsigned i=0; i<300; ++i) {
        c.mode = i % KEYBOARD_MODE_COUNT; c.brightness = i % 4;
        assert(keyboard_settings_save(&c) == 0);
        assert(keyboard_settings_load(&restored) == 0);
        assert(restored.mode == c.mode && restored.brightness == c.brightness);
    }
    before = writes;
    c.brightness = 4;
    assert(keyboard_settings_save(&c) == -1 && writes == before);
    c.brightness = 0; c.mode = KEYBOARD_MODE_COUNT;
    assert(keyboard_settings_save(&c) == -1 && writes == before);
    /* Invalid mode/level, magic, or incomplete checksum must be ignored. */
    for (unsigned i=0; i<BOARD_SETTINGS_WORDS; ++i) storage[i] = 0;
    storage[0] = 0x4B420000U | (KEYBOARD_MODE_COUNT << 4); storage[1] = ~storage[0];
    assert(keyboard_settings_load(&restored) == -1);
    storage[0] = 0x4B420004U; storage[1] = ~storage[0];
    assert(keyboard_settings_load(&restored) == -1);
    storage[0] = 0x4B430003U; storage[1] = ~storage[0];
    assert(keyboard_settings_load(&restored) == -1);
    storage[0] = 0x4B420003U; storage[1] = 0;
    assert(keyboard_settings_load(&restored) == -1);
    /* Walk every new mode in both directions, and persist each identifier. */
    c.mode = 0; c.brightness = 3;
    for (unsigned i=1; i<=KEYBOARD_MODE_COUNT; ++i) {
        press(&c, 2, &t);
        assert(c.mode == i % KEYBOARD_MODE_COUNT);
        assert(keyboard_settings_save(&c) == 0);
        assert(keyboard_settings_load(&restored) == 0 && restored.mode == c.mode);
    }
    for (unsigned i=1; i<=KEYBOARD_MODE_COUNT; ++i) {
        press(&c, 1, &t);
        assert(c.mode == (KEYBOARD_MODE_COUNT - i) % KEYBOARD_MODE_COUNT);
    }
    static keyboard_effect_config_t config = {.peak=3072, .interval_ms=3600, .color_count=3, .colors={1,4,3}};
    uint16_t rgb[3];
    const uint8_t modes[] = {KEYBOARD_MODE_WHITE, KEYBOARD_MODE_RED, KEYBOARD_MODE_GREEN, KEYBOARD_MODE_BLUE};
    const uint8_t masks[] = {7, 1, 2, 4};
    for (unsigned i=0; i<4; ++i) {
        for (unsigned ms=0; ms<10800; ms+=257) {
            keyboard_uniform_pixel(&config, modes[i], ms, rgb);
            for (unsigned ch=0; ch<3; ++ch)
                assert(rgb[ch] == ((masks[i] & (1U << ch)) ? 3072 : 0));
        }
    }
    /* Whole-keyboard breathing changes color, without a fade-to-black ramp. */
    keyboard_uniform_pixel(&config, KEYBOARD_MODE_COLOR_BREATHE, 0, rgb);
    assert(rgb[0] == 3072 && rgb[1] == 0 && rgb[2] == 0);
    keyboard_uniform_pixel(&config, KEYBOARD_MODE_COLOR_BREATHE, 1800, rgb);
    assert(rgb[0] >= 2112 && rgb[0] <= 2176 && rgb[1] == 0 && rgb[2] == rgb[0]);
    keyboard_uniform_pixel(&config, KEYBOARD_MODE_COLOR_BREATHE, 3600, rgb);
    assert(rgb[0] == 0 && rgb[1] == 0 && rgb[2] == 3072);
    keyboard_uniform_pixel(&config, KEYBOARD_MODE_COLOR_BREATHE, 7200, rgb);
    assert(rgb[0] >= 2112 && rgb[0] <= 2176 && rgb[1] == rgb[0] && rgb[2] == 0);
    keyboard_uniform_pixel(&config, KEYBOARD_MODE_COLOR_BREATHE, 10800, rgb);
    assert(rgb[0] == 3072 && rgb[1] == 0 && rgb[2] == 0);
    uint16_t previous_rgb[3] = {3072,0,0};
    for (unsigned ms=0; ms<=10800; ms+=8) {
        uint16_t periodic[3];
        keyboard_uniform_pixel(&config, KEYBOARD_MODE_COLOR_BREATHE, ms, rgb);
        keyboard_uniform_pixel(&config, KEYBOARD_MODE_COLOR_BREATHE, ms+10800, periodic);
        unsigned energy = 0;
        for (unsigned ch=0; ch<3; ++ch) {
            assert(rgb[ch] <= config.peak && rgb[ch] % 64 == 0);
            assert(rgb[ch] == periodic[ch]);
            unsigned difference = rgb[ch] > previous_rgb[ch] ? rgb[ch]-previous_rgb[ch] : previous_rgb[ch]-rgb[ch];
            assert(difference <= 128); /* smooth at 8 ms, including color boundaries */
            energy += rgb[ch] * rgb[ch];
            previous_rgb[ch] = rgb[ch];
        }
        assert(energy <= config.peak * config.peak);
        assert(config.peak * config.peak - energy <= 3U * config.peak * 128U);
    }
    /* No backwards color steps inside any of the three transitions. */
    for (unsigned segment=0; segment<3; ++segment) {
        keyboard_uniform_pixel(&config, KEYBOARD_MODE_COLOR_BREATHE, segment*3600, rgb);
        for (unsigned ms=8; ms<3600; ms+=8) {
            uint16_t next[3];
            keyboard_uniform_pixel(&config, KEYBOARD_MODE_COLOR_BREATHE, segment*3600+ms, next);
            if (segment == 0) assert(next[0] <= rgb[0] && next[2] >= rgb[2]);
            if (segment == 1) assert(next[0] >= rgb[0] && next[1] >= rgb[1] && next[2] <= rgb[2]);
            if (segment == 2) assert(next[0] >= rgb[0] && next[1] <= rgb[1]);
            for (unsigned ch=0; ch<3; ++ch) rgb[ch] = next[ch];
        }
    }
    /* Transition timing follows the existing profile interval. */
    config.interval_ms = 7200;
    keyboard_uniform_pixel(&config, KEYBOARD_MODE_COLOR_BREATHE, 3600, rgb);
    assert(rgb[0] >= 2112 && rgb[0] <= 2176 && rgb[2] == rgb[0]);
    keyboard_uniform_pixel(&config, KEYBOARD_MODE_COLOR_BREATHE, 7200, rgb);
    assert(rgb[0] == 0 && rgb[2] == 3072);
    /* Regression: max-channel normalization abruptly changed slope at the
     * red/blue midpoint. Compare both sides before 64-level quantization can
     * hide the corner at the ordinary 3072 peak. */
    config.interval_ms = 3600; config.peak = 16384;
    uint16_t left[3], middle[3], right[3];
    keyboard_uniform_pixel(&config, KEYBOARD_MODE_COLOR_BREATHE, 1760, left);
    keyboard_uniform_pixel(&config, KEYBOARD_MODE_COLOR_BREATHE, 1800, middle);
    keyboard_uniform_pixel(&config, KEYBOARD_MODE_COLOR_BREATHE, 1840, right);
    for (unsigned ch=0; ch<3; ++ch) {
        int corner = (int)left[ch] + right[ch] - 2 * middle[ch];
        assert(corner >= -256 && corner <= 256);
    }
    /* The renderer wraps time by a full palette cycle. Test its actual wrap
     * boundary as well as every transition and full-cycle boundary above. */
    config.peak = 3072;
    keyboard_uniform_pixel(&config, KEYBOARD_MODE_COLOR_BREATHE, 29601, left);
    keyboard_uniform_pixel(&config, KEYBOARD_MODE_COLOR_BREATHE, 18801, right);
    for (unsigned ch=0; ch<3; ++ch) assert(left[ch] == right[ch]);
    keyboard_controls_test_result = 0x600DCAFEU;
    return 0;
}
