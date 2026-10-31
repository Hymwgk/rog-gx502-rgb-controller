import json
import math
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/scripts'))
from spatial_effects import generate_spatial_tables, spatial_data


class SpatialEffectTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.keymap = ROOT / 'configs/keyboard/keymap.json'
        cls.data = spatial_data(cls.keymap, [1, 4, 3], 3072)

    def test_geometry_preserves_aspect_ratio_and_mapped_slots(self):
        records = json.loads(self.keymap.read_text())
        expected = {r['ctrl']*16+r['slot'] for r in records if r['key']}
        self.assertEqual(set(self.data['ids']), expected)
        self.assertEqual(len(expected), 109)
        for i in range(128):
            if i in expected:
                self.assertIn(self.data['lane'][i], range(3))
                self.assertGreater(self.data['gain'][i], 0)
            else:
                self.assertIsNone(self.data['points'][i])
                self.assertEqual(self.data['lane'][i], 255)
                self.assertTrue(all(d == 255 for d in self.data['distances'][i]))
        coordinates = [p for p in self.data['points'] if p]
        self.assertLess(max(p[1] for p in coordinates), self.data['width'])

    def test_radial_distance_is_symmetric_and_isotropic(self):
        data = self.data
        for i in data['ids']:
            self.assertEqual(data['distances'][i][i], 0)
            for j in data['ids']:
                distance = data['distances'][i][j]
                self.assertEqual(distance, data['distances'][j][i])
                self.assertLessEqual(distance, 250)
                a, b = data['points'][i], data['points'][j]
                self.assertLessEqual(abs(distance*4-math.dist(a,b)), 2)

    def test_heads_glow_and_tails_exit_at_zero_without_exceeding_peak(self):
        for name, front in (('meteor',24),('ripple',8)):
            for color in self.data[name]:
                self.assertEqual(color[0], [0,0,0])
                self.assertEqual(color[-1], [0,0,0])
                self.assertTrue(all(v > 0 for v in color[front]))
                for sample in color:
                    self.assertTrue(all(0 <= v <= 3072 for v in sample))
                tail = color[front:]
                for a, b in zip(tail, tail[1:]):
                    self.assertTrue(all(y <= x for x,y in zip(a,b)))

    def test_generation_is_reproducible_and_has_no_runtime_floats(self):
        with tempfile.TemporaryDirectory() as directory:
            a, b = Path(directory)/'a.h', Path(directory)/'b.h'
            for path in (a,b):
                generate_spatial_tables(self.keymap, [1,4,3], 3072, path)
            self.assertEqual(a.read_bytes(), b.read_bytes())
            self.assertIn('spatial_distances[128][128]', a.read_text())
            self.assertIn('spatial_meteor[3][256][3]', a.read_text())
            self.assertNotIn('float ', a.read_text())
