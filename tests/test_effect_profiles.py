import json
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/scripts'))

from effect_profiles import ROOT, generate, load_profile, wave_sample


class EffectProfileTests(unittest.TestCase):
    def test_classic_preserves_confirmed_settings(self):
        config = load_profile(ROOT / 'configs/effects/classic-breathe.json')
        self.assertEqual(config['colors'], ['white', 'blue', 'red', 'green'])
        self.assertEqual((config['peak'], config['steps_per_half'], config['step_ms'],
                          config['curve'], config['gclk_hz']),
                         (4096, 64, 48, 'square', 2000000))

    def test_invalid_profiles_fail_before_compiling(self):
        original = load_profile(ROOT / 'configs/effects/classic-breathe.json')
        changes = [{'peak': 63}, {'peak': True}, {'steps_per_half': 129},
                   {'step_ms': 0}, {'colors': []}, {'colors': ['blu']},
                   {'curve': 'squre'}, {'gclk_hz': 3000000}, {'speed': 48}]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'bad.json'
            for change in changes:
                with self.subTest(change=change):
                    path.write_text(json.dumps(original | change))
                    with self.assertRaises(ValueError):
                        load_profile(path)

    def test_effective_snapshot_includes_overrides(self):
        config = load_profile(ROOT / 'configs/effects/classic-breathe.json', peak=2048)
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'effect_config.h'
            generate(config, output)
            self.assertEqual(json.loads(output.with_suffix('.json').read_text())['peak'], 2048)
            self.assertIn('.peak = 2048U', output.read_text())
            self.assertIn('.colors = { 7U, 4U, 1U, 2U }', output.read_text())

    def test_real_c_curve_endpoints_symmetry_and_quantization(self):
        compiler = shutil.which('cc') or shutil.which('gcc')
        if not compiler:
            self.skipTest('native C compiler unavailable')
        code = r'''
#include <assert.h>
#include "keyboard_effect.h"
int main(void) {
    keyboard_effect_config_t c = {.peak=4096, .steps_per_half=64, .curve=KEYBOARD_CURVE_SQUARE};
    assert(keyboard_effect_level(&c, 0)==0);
    assert(keyboard_effect_level(&c, 32)==1024);
    assert(keyboard_effect_level(&c, 64)==4096);
    assert(keyboard_effect_level(&c, 128)==0);
    const unsigned halves[] = {1, 17, 64, 128};
    for (unsigned curve=0; curve<2; ++curve) {
        c.curve=curve; c.peak=16384;
        for (unsigned n=0; n<4; ++n) {
            c.steps_per_half=halves[n]; unsigned previous=0;
            for (unsigned i=0; i<=2*halves[n]; ++i) {
                unsigned value=keyboard_effect_level(&c, i);
                assert(value<=c.peak && value%64==0);
                assert(value==keyboard_effect_level(&c, 2*halves[n]-i));
                if (i<=halves[n]) assert(value>=previous);
                previous=value;
            }
        }
    }
}
'''
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'curve.c'
            binary = Path(directory) / 'curve'
            source.write_text(code)
            subprocess.run([compiler, '-Wall', '-Wextra', '-Werror', '-DKB_TEST_MODE=0',
                            '-I', str(ROOT / 'firmware/include'), str(source),
                            str(ROOT / 'firmware/src/effects/keyboard_effect.c'),
                            '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

class WaveProfileTests(unittest.TestCase):
    def test_requested_colors_and_spacing(self):
        config = load_profile(ROOT / 'configs/effects/diagonal-wave.json')
        self.assertEqual(config['colors'], ['red', 'yellow', 'blue'])
        original = load_profile(ROOT / 'configs/effects/diagonal-wave-v3.json')
        for field in ('travel_ms', 'interval_ms', 'crest_ms', 'tail_ms'):
            self.assertEqual(config[field] * 2, original[field] * 3)
        self.assertEqual(config['wave_curve'], 'smooth')

    def test_wave_invalid_timing_and_color_count(self):
        original = load_profile(ROOT / 'configs/effects/diagonal-wave.json')
        changes = [{'colors': ['red', 'blue']}, {'interval_ms': 1601},
                   {'crest_ms': 2000, 'interval_ms': 1600}, {'floor': 6144}, {'floor': True}, {'wave_curve': 'bad'}, {'travel_ms': 100}, {'gclk_hz': 100000}]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'bad-wave.json'
            for change in changes:
                with self.subTest(change=change):
                    path.write_text(json.dumps(original | change))
                    with self.assertRaises(ValueError):
                        load_profile(path)

    def test_precomputed_tail_changes_color_and_stays_quantized(self):
        import re
        config = load_profile(ROOT / 'configs/effects/diagonal-wave.json')
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'effect_config.h'
            generate(config, output)
            text = (output.parent / 'keyboard_wave_tables.h').read_text().split('keyboard_wave_samples', 1)[1]
            values = [int(n) for n in re.findall(r'(\d+)U', text)]
            count = config['interval_ms'] // 8
            self.assertEqual(len(values), 3 * count * 3)
            self.assertTrue(all(0 <= n <= config['peak'] and n % 64 == 0 for n in values))
            # Red crest has no green/blue, then its fading tail acquires green.
            red = [values[i:i+3] for i in range(0, count * 3, 3)]
            crest = config['crest_ms'] // 8
            self.assertEqual(red[crest], [config['peak'], 0, 0])
            self.assertGreater(red[crest + 40][1], 0)
            self.assertLess(red[crest + 100][0], red[crest + 40][0])
            # Every phase retains light; each crest stays pure, and cyclic joins are smooth.
            for wave in range(3):
                samples = [wave_sample(config, wave, age)
                           for age in range(0, config['interval_ms'], 8)]
                self.assertTrue(all(max(rgb) >= 128 for rgb in samples))
                expected = [config['peak'] if (1 << c) & (1, 3, 4)[wave] else 0
                            for c in range(3)]
                self.assertEqual(samples[crest], expected)
                following = wave_sample(config, (wave + 1) % 3, 0)
                self.assertTrue(all(abs(a - b) <= 128 for a, b in zip(samples[-1], following)))
                self.assertGreater(sum(v > 0 for v in samples[-1]), 1)

    def test_smooth_wave_contrast_blend_and_no_abrupt_steps(self):
        config = load_profile(ROOT / 'configs/effects/diagonal-wave.json')
        for wave in range(3):
            samples = [wave_sample(config, wave, age)
                       for age in range(0, config['interval_ms'], 8)]
            trough = (config['crest_ms'] + config['interval_ms'] // 2) % config['interval_ms']
            self.assertLessEqual(max(wave_sample(config, wave, trough)), config['floor'])
            self.assertGreaterEqual(config['peak'] // max(samples[trough // 8]), 24)
            # Broad visibly mixed shoulders, rather than a last-moment hue change.
            for fraction in (0.25, 0.75):
                age = (config['crest_ms'] + int(config['interval_ms'] * fraction)) % config['interval_ms']
                rgb = wave_sample(config, wave, age)
                self.assertGreaterEqual(sum(v >= 192 for v in rgb), 2)
            for left, right in zip(samples, samples[1:]):
                self.assertTrue(all(abs(a - b) <= 128 for a, b in zip(left, right)))

    def test_saved_mode_and_small_amplitude_color_flow(self):
        original = load_profile(ROOT / 'configs/effects/diagonal-wave.json')
        saved = load_profile(ROOT / 'configs/effects/diagonal-wave-slow.json')
        for field in set(original) - {'name', 'description'}:
            self.assertEqual(saved[field], original[field])
        config = load_profile(ROOT / 'configs/effects/diagonal-color-flow.json')
        self.assertEqual(config['travel_ms'], 8000)
        self.assertEqual(config['colors'], saved['colors'])
        for wave in range(3):
            samples = [wave_sample(config, wave, age)
                       for age in range(0, config['interval_ms'], 8)]
            # Blending persists through all phases, including original crests.
            self.assertTrue(all(min(rgb) >= 64 for rgb in samples))
            self.assertTrue(all(sum(v >= 512 for v in rgb) >= 2 for rgb in samples))
            levels = [max(rgb) for rgb in samples]
            self.assertGreaterEqual(min(levels), config['floor'] - 63)
            self.assertLessEqual(max(levels), config['peak'])
            self.assertLessEqual(max(levels) - min(levels), 192)
            self.assertTrue(all(v % 64 == 0 for rgb in samples for v in rgb))
            for fraction in (0.25, 0.5, 0.75):
                age = (config['crest_ms'] + int(config['interval_ms'] * fraction)) % config['interval_ms']
                self.assertGreaterEqual(sum(v >= 384 for v in wave_sample(config, wave, age)), 2)
            following = wave_sample(config, (wave + 1) % 3, 0)
            for left, right in zip(samples, samples[1:] + [following]):
                self.assertTrue(all(abs(a - b) <= 128 for a, b in zip(left, right)))
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'effect_config.h'
            generate(config, output)
            self.assertIn('.wave_smooth = 3U', output.read_text())

    def test_original_wave_remains_reproducible(self):
        config = load_profile(ROOT / 'configs/effects/diagonal-wave-v1.json')
        for wave in range(3):
            self.assertEqual(wave_sample(config, wave, 0), [0, 0, 0])
            self.assertEqual(wave_sample(config, wave, config['interval_ms'] - 8), [0, 0, 0])

    def test_stronger_blend_retains_timing_and_increases_minor_channels(self):
        config = load_profile(ROOT / 'configs/effects/diagonal-color-flow-rby.json')
        original = load_profile(ROOT / 'configs/effects/diagonal-color-flow.json')
        self.assertEqual(config['colors'], ['red', 'blue', 'yellow'])
        for field in ('travel_ms', 'interval_ms', 'crest_ms', 'tail_ms', 'peak', 'floor', 'gclk_hz'):
            self.assertEqual(config[field], original[field])
        unblended = config | {'color_blend': 0}
        samples = []
        old_samples = []
        for wave in range(3):
            for age in range(0, config['interval_ms'], 8):
                rgb = wave_sample(config, wave, age)
                old = wave_sample(unblended, wave, age)
                self.assertEqual(max(rgb), max(old))
                self.assertGreaterEqual(min(rgb), 640)
                samples.append(rgb)
                old_samples.append(old)
        self.assertGreater(min(min(rgb) for rgb in samples),
                           min(min(rgb) for rgb in old_samples))
        self.assertGreater(sum(min(rgb) for rgb in samples),
                           sum(min(rgb) for rgb in old_samples))
        for left, right in zip(samples, samples[1:] + samples[:1]):
            self.assertTrue(all(abs(a - b) <= 64 for a, b in zip(left, right)))
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'blend.json'
            for change in ({'color_blend': -1}, {'color_blend': 1025},
                           {'color_blend': True}, {'wave_curve': 'smooth'}):
                path.write_text(json.dumps(config | change))
                with self.assertRaises(ValueError):
                    load_profile(path)

    def test_geometry_points_the_wave_from_bottom_left_to_top_right(self):
        records = json.loads((ROOT / 'configs/keyboard/keymap.json').read_text())
        populated = [v for v in records if v['key']]
        first = min(populated, key=lambda v: v['camera_xy'][0]-v['camera_xy'][1])
        last = max(populated, key=lambda v: v['camera_xy'][0]-v['camera_xy'][1])
        self.assertEqual(first['key'], 'Ctrl左')
        self.assertEqual(last['key'], 'Delete/Insert')
        self.assertEqual(len(populated), 109)


if __name__ == '__main__':
    unittest.main()
