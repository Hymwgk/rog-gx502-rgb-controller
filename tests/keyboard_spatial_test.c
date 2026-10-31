#include "keyboard_spatial.h"
#include "effect_config.h"
volatile uint32_t keyboard_spatial_test_result;
#define assert(condition) do { if (!(condition)) { keyboard_spatial_test_result = __LINE__; return 1; } } while (0)
int main(void)
{
    static keyboard_spatial_state_t state = {.mode=255U};
    uint16_t rgb[3];
    uint32_t peak = keyboard_effect_config.peak;
    uint32_t interval = keyboard_effect_config.interval_ms;
    keyboard_spatial_prepare(&state, &keyboard_effect_config, KEYBOARD_MODE_RIPPLE, 0, 12345);
    assert(state.origin < 128 && state.color < 3 && state.ripple_radius == -8);
    for (unsigned id=0; id<128; ++id) {
        keyboard_spatial_pixel(&state, id/16, id%16, rgb);
        assert(rgb[0] == 0 && rgb[1] == 0 && rgb[2] == 0);
    }
    unsigned origin = state.origin;
    keyboard_spatial_prepare(&state, &keyboard_effect_config, KEYBOARD_MODE_RIPPLE, interval/36, 0);
    keyboard_spatial_pixel(&state, origin/16, origin%16, rgb);
    assert(rgb[0] || rgb[1] || rgb[2]); /* source lights before distant keys */
    keyboard_spatial_prepare(&state, &keyboard_effect_config, KEYBOARD_MODE_RIPPLE, interval/3, 0);
    keyboard_spatial_pixel(&state, origin/16, origin%16, rgb);
    assert(rgb[0] == 0 && rgb[1] == 0 && rgb[2] == 0); /* ring left source */
    static uint32_t visited[4];
    for (unsigned cycle=1; cycle<=32; ++cycle) {
        keyboard_spatial_prepare(&state, &keyboard_effect_config, KEYBOARD_MODE_RIPPLE, cycle*interval, 0);
        assert(state.origin < 128 && state.origin != origin);
        origin = state.origin;
        visited[origin/32] |= 1U << (origin%32);
        for (unsigned id=0; id<128; ++id) {
            keyboard_spatial_pixel(&state, id/16, id%16, rgb);
            assert(rgb[0] == 0 && rgb[1] == 0 && rgb[2] == 0);
        }
    }
    unsigned distinct = 0;
    for (unsigned i=0; i<4; ++i)
        for (uint32_t bits=visited[i]; bits; bits &= bits-1U) ++distinct;
    assert(distinct >= 8); /* not just a short fixed list of start points */
    unsigned lit = 0, white_head = 0;
    for (unsigned mode=KEYBOARD_MODE_METEOR; mode<=KEYBOARD_MODE_RIPPLE; ++mode) {
        state.mode = 255;
        for (unsigned ms=0; ms<3*interval; ms+=120) {
            keyboard_spatial_prepare(&state, &keyboard_effect_config, mode, ms, 99);
            for (unsigned id=0; id<128; ++id) {
                keyboard_spatial_pixel(&state, id/16, id%16, rgb);
                if (rgb[0] || rgb[1] || rgb[2]) ++lit;
                if (rgb[0] && rgb[1] && rgb[2]) ++white_head;
                for (unsigned ch=0; ch<3; ++ch) {
                    assert(rgb[ch] <= peak);
                    static keyboard_controls_t controls;
                    for (controls.brightness=0; controls.brightness<=3; ++controls.brightness) {
                        uint16_t scaled = keyboard_controls_level(&controls, rgb[ch]);
                        assert(scaled <= peak && scaled % 64 == 0);
                        if (controls.brightness == 0) assert(scaled == 0);
                    }
                }
            }
        }
    }
    assert(lit > 100 && white_head > 10);
    state.mode = KEYBOARD_MODE_RIPPLE;
    state.phase_ms = 123;
    state.last_ms = UINT32_MAX-5;
    keyboard_spatial_prepare(&state, &keyboard_effect_config, KEYBOARD_MODE_RIPPLE, 5, 0);
    assert(state.phase_ms == 134); /* real-time counter wrap */
    keyboard_spatial_pixel(&state, 8, 0, rgb);
    assert(rgb[0] == 0 && rgb[1] == 0 && rgb[2] == 0);
    keyboard_spatial_test_result = 0x600DCAFEU;
    return 0;
}
