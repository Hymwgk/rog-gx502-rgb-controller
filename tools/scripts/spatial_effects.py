"""Compile measured keyboard geometry and light envelopes for spatial effects."""
import json
import math
from pathlib import Path


def spatial_data(keymap, colors, peak):
    records = json.loads(Path(keymap).read_text())
    mapped = [r for r in records if r['key']]
    xmin = min(r['camera_xy'][0] for r in mapped)
    xmax = max(r['camera_xy'][0] for r in mapped)
    ymin = min(r['camera_xy'][1] for r in mapped)
    ymax = max(r['camera_xy'][1] for r in mapped)
    scale = 1000 / math.hypot(xmax - xmin, ymax - ymin)
    points = [None] * 128
    for r in mapped:
        x, y = r['camera_xy']
        points[r['ctrl'] * 16 + r['slot']] = (round((x-xmin)*scale), round((y-ymin)*scale))
    ids = [i for i, p in enumerate(points) if p is not None]
    width = max(p[0] for p in points if p is not None)
    height = max(p[1] for p in points if p is not None)
    distances = [[255]*128 for _ in range(128)]
    lane = [255]*128
    gain = [0]*128
    for i in ids:
        x, y = points[i]
        lanes = [height*f + width*0.075 - x*0.15 for f in (0.25, 0.60, 0.95)]
        lane[i] = min(range(3), key=lambda j: abs(y-lanes[j]))
        gain[i] = round(255*math.exp(-0.5*((y-lanes[lane[i]])/50)**2))
        for j in ids:
            xx, yy = points[j]
            distances[i][j] = round(math.hypot(x-xx, y-yy)/4)

    def envelope(front, tail, white_width, white_amount):
        result = []
        for mask in colors:
            samples = []
            for index in range(front+tail):
                back = index-front
                if back < 0:
                    u = index/front
                    strength = u*u*(3-2*u)
                else:
                    strength = max(0, (tail-1-back)/(tail-1))**2
                white = white_amount*math.exp(-0.5*(back/white_width)**2)
                samples.append([round(peak*strength*(1 if mask & (1 << c) else white))
                                for c in range(3)])
            result.append(samples)
        return result

    return dict(points=points, ids=ids, width=width, distances=distances,
                distance_max=max(distances[i][j] for i in ids for j in ids),
                lane=lane, gain=gain, meteor=envelope(24, 232, 12, 0.65),
                ripple=envelope(8, 56, 5, 0.35), peak=peak)


def generate_spatial_tables(keymap, colors, peak, output):
    data = spatial_data(keymap, colors, peak)

    def values(v):
        return '{ ' + ', '.join(values(x) if isinstance(x, list) else str(x)+'U' for x in v) + ' }'

    text = '/* Generated geometry and envelopes; no floating point on the MCU. */\n'
    text += '#ifndef KEYBOARD_SPATIAL_TABLES_H\n#define KEYBOARD_SPATIAL_TABLES_H\n'
    text += f"#define SPATIAL_MAPPED_COUNT {len(data['ids'])}U\n"
    text += f"#define SPATIAL_WIDTH {data['width']}U\n"
    text += f"#define SPATIAL_DISTANCE_MAX {data['distance_max']}U\n"
    text += f"#define SPATIAL_PEAK {peak}U\n"
    text += f"static const uint8_t spatial_mapped_ids[{len(data['ids'])}] = {values(data['ids'])};\n"
    text += f"static const uint16_t spatial_x[128] = {values([p[0] if p else 0 for p in data['points']])};\n"
    for name in ('lane', 'gain'):
        text += f"static const uint8_t spatial_{name}[128] = {values(data[name])};\n"
    text += f"static const uint8_t spatial_distances[128][128] = {values(data['distances'])};\n"
    text += f"static const uint16_t spatial_meteor[3][256][3] = {values(data['meteor'])};\n"
    text += f"static const uint16_t spatial_ripple[3][64][3] = {values(data['ripple'])};\n"
    text += '#endif\n'
    Path(output).write_text(text)
