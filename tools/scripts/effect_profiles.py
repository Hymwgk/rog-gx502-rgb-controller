#!/usr/bin/env python3
"""Validate host-side effect JSON and generate a small firmware constant."""
import argparse
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
COLORS = {'red': 1, 'green': 2, 'blue': 4, 'white': 7,
          'cyan': 6, 'magenta': 5, 'yellow': 3}
COMMON = {'version', 'name', 'description', 'effect', 'colors', 'peak', 'gclk_hz'}
FIELDS = COMMON | {'steps_per_half', 'step_ms', 'curve'}
WAVE_FIELDS = COMMON | {'travel_ms', 'interval_ms', 'crest_ms', 'tail_ms'}


def load_profile(path, peak=None, gclk_hz=None):
    config = json.loads(Path(path).read_text(encoding='utf-8'))
    if not isinstance(config, dict):
        raise ValueError('配置必须为对象')
    kind = config.get('effect')
    if kind not in ('breathe', 'diagonal-wave'):
        raise ValueError('effect 支持 breathe、diagonal-wave')
    fields = FIELDS if kind == 'breathe' else WAVE_FIELDS
    optional = {'wave_curve', 'floor', 'color_blend'} if kind == 'diagonal-wave' else set()
    if not fields <= set(config) or set(config) - fields - optional:
        raise ValueError('配置字段必须为：' + ', '.join(sorted(fields)))
    if type(config['version']) is not int or config['version'] != 1:
        raise ValueError('仅支持 version=1')
    if not isinstance(config['name'], str) or not config['name']:
        raise ValueError('name 必须为非空字符串')
    if not isinstance(config['description'], str):
        raise ValueError('description 必须为字符串')
    colors = config['colors']
    if not isinstance(colors, list) or not 1 <= len(colors) <= 16:
        raise ValueError('colors 需要 1..16 个颜色')
    if any(not isinstance(c, str) or c not in COLORS for c in colors):
        raise ValueError('支持颜色：' + ', '.join(COLORS))
    if kind == 'diagonal-wave' and len(colors) != 3:
        raise ValueError('diagonal-wave 需要恰好三种波峰颜色')
    if peak is not None:
        config['peak'] = peak
    if gclk_hz is not None:
        config['gclk_hz'] = gclk_hz
    ranges = [('peak', 64, 16384)]
    if kind == 'breathe':
        ranges += [('steps_per_half', 1, 128), ('step_ms', 10, 1000)]
        if config['curve'] not in ('square', 'linear'):
            raise ValueError('curve 支持 square、linear')
    else:
        ranges += [('travel_ms', 500, 10000), ('interval_ms', 200, 10000),
                   ('crest_ms', 20, 2000), ('tail_ms', 100, 10000)]
    for field, low, high in ranges:
        value = config[field]
        if type(value) is not int or not low <= value <= high:
            raise ValueError(f'{field} 必须为 {low}..{high} 的整数')
    frequencies = (100000, 500000, 1000000, 2000000) if kind == 'breathe' else (1000000, 2000000)
    if type(config['gclk_hz']) is not int or config['gclk_hz'] not in frequencies:
        raise ValueError('gclk_hz 支持 ' + ', '.join(map(str, frequencies)))
    if kind == 'diagonal-wave' and config['interval_ms'] % 8 != 0:
        raise ValueError('interval_ms 必须是 8 的倍数')
    if kind == 'diagonal-wave' and config['crest_ms'] >= config['interval_ms']:
        raise ValueError('crest_ms 必须小于 interval_ms')
    if kind == 'diagonal-wave':
        if config.get('wave_curve', 'legacy') not in ('legacy', 'smooth', 'color-flow', 'color-flow-blended'):
            raise ValueError('wave_curve 支持 legacy、smooth、color-flow、color-flow-blended')
        floor = config.get('floor', 0)
        if type(floor) is not int or not 0 <= floor < config['peak']:
            raise ValueError('floor 必须是小于 peak 的非负整数')
        if config.get('wave_curve') not in ('smooth', 'color-flow', 'color-flow-blended') and 'floor' in config:
            raise ValueError('floor 仅用于 smooth、color-flow 曲线')
        blend = config.get('color_blend', 0)
        if type(blend) is not int or not 0 <= blend <= 1024:
            raise ValueError('color_blend 必须为 0..1024 的整数')
        if 'color_blend' in config and config.get('wave_curve') != 'color-flow-blended':
            raise ValueError('color_blend 仅用于 color-flow-blended 曲线')
    return config


def generate_geometry(output):
    records = json.loads((ROOT / 'configs/keyboard/keymap.json').read_text())
    if len(records) != 128 or {(v['ctrl'], v['slot']) for v in records} != {(c, n) for c in range(8) for n in range(16)}:
        raise ValueError('键盘映射需要完整的 8×16 唯一灯位')
    populated = [v for v in records if v['key']]
    projected = [v['camera_xy'][0] - v['camera_xy'][1] for v in populated]
    low, high = min(projected), max(projected)
    table = [[65535] * 16 for _ in range(8)]
    for v in populated:
        x, y = v['camera_xy']
        table[v['ctrl']][v['slot']] = round((x - y - low) * 1024 / (high - low))
    rows = ',\n'.join('    { ' + ', '.join(str(n) + 'U' for n in row) + ' }' for row in table)
    Path(output).write_text('/* Generated from configs/keyboard/keymap.json. */\n'
                           'static const uint16_t keyboard_projection[8][16] = {\n' + rows + '\n};\n')


def wave_sample(config, wave, age):
    """Quantized wave sample; optional smooth crest-to-crest brightness/hue."""
    masks = [COLORS[c] for c in config['colors']]
    if config.get('wave_curve') in ('smooth', 'color-flow', 'color-flow-blended'):
        # One continuous crest-to-crest phase controls both brightness and hue.
        phase = (age + config['interval_ms'] - config['crest_ms']) % config['interval_ms']
        index = (wave - (age < config['crest_ms'])) % 3
        def smoothstep(x):
            return x * x * (3072 - 2 * x) // (1024 * 1024)
        mix = smoothstep(phase * 1024 // config['interval_ms'])
        distance = min(phase, config['interval_ms'] - phase)
        shoulder = 1024 - smoothstep(min(1024, distance * 1024 // config['tail_ms']))
        gain = shoulder * shoulder // 1024
        floor = config.get('floor', 0)
        value = floor + (config['peak'] - floor) * gain // 1024
        current, following = masks[index], masks[(index + 1) % 3]
        weights = [(1024 - mix if current & (1 << c) else 0)
                   + (mix if following & (1 << c) else 0) for c in range(3)]
        if config.get('wave_curve') == 'color-flow-blended':
            # Periodic cubic B-spline: all three colors contribute, even at crests.
            u = phase * 1024 // config['interval_ms']
            u2, v = u * u // 1024, 1024 - u
            u3 = u2 * u // 1024
            previous_weight = (v * v // 1024 * v // 1024 + u3) // 6
            current_weight = (4096 - 6 * u2 + 3 * u3) // 6
            # Raise every control color's contribution without changing timing.
            blend = config.get('color_blend', 0)
            previous_weight = (previous_weight * (1024 - blend) * 3 + 1024 * blend) // 3072
            current_weight = (current_weight * (1024 - blend) * 3 + 1024 * blend) // 3072
            next_weight = 1024 - previous_weight - current_weight
            previous = masks[(index - 1) % 3]
            weights = [(previous_weight if previous & (1 << c) else 0)
                       + (current_weight if current & (1 << c) else 0)
                       + (next_weight if following & (1 << c) else 0) for c in range(3)]
        # Keep the strongest channel steady even when unrelated colors mix.
        scale = max(weights) if config.get('wave_curve') != 'smooth' else 1024
        return [(value * weight // scale) & ~63 for weight in weights]


    def tail_rgb(index, elapsed):
        if elapsed >= config['tail_ms']:
            return [0, 0, 0]
        remaining = (config['tail_ms'] - elapsed) * 255 // config['tail_ms']
        gain = remaining * remaining // 255
        mix = elapsed * 255 // config['tail_ms']
        value = config['peak'] * gain // 255
        current, following = masks[index % 3], masks[(index + 1) % 3]
        return [value * (((255 if current & (1 << c) else 0) * (255 - mix)
                         + (255 if following & (1 << c) else 0) * mix) // 255) // 255
                for c in range(3)]

    if age < config['crest_ms']:
        rise = age * 255 // config['crest_ms']
        previous = tail_rgb(wave - 1, age + config['interval_ms'] - config['crest_ms'])
        rgb = [(config['peak'] * rise // 255 if masks[wave] & (1 << c) else 0)
               + previous[c] * (255 - rise) // 255 for c in range(3)]
    else:
        rgb = tail_rgb(wave, age - config['crest_ms'])
    return [v & ~63 for v in rgb]


def generate_wave_tables(config, output):
    records = json.loads((ROOT / 'configs/keyboard/keymap.json').read_text())
    projected = [v['camera_xy'][0] - v['camera_xy'][1] for v in records if v['key']]
    low, high = min(projected), max(projected)
    arrivals = [[65535] * 16 for _ in range(8)]
    for v in records:
        if v['key']:
            x, y = v['camera_xy']
            projection = round((x - y - low) * 1024 / (high - low))
            arrivals[v['ctrl']][v['slot']] = projection * config['travel_ms'] // 1024
    rows = []
    for wave in range(3):
        samples = [wave_sample(config, wave, age)
                   for age in range(0, config['interval_ms'], 8)]
        rows.append('    { ' + ', '.join('{ ' + ', '.join(str(v) + 'U' for v in rgb) + ' }' for rgb in samples) + ' }')
    text = '/* Host-generated RGB samples at 8 ms intervals, avoiding division in scanning. */\n'
    text += 'static const uint16_t keyboard_wave_arrivals[8][16] = {\n'
    text += ',\n'.join('    { ' + ', '.join(str(v) + 'U' for v in row) + ' }' for row in arrivals) + '\n};\n'
    text += f"static const uint16_t keyboard_wave_samples[3][{config['interval_ms'] // 8}][3] = {{\n"
    text += ',\n'.join(rows) + '\n};\n'
    Path(output).write_text(text)


def generate(config, output):
    canonical = json.dumps(config, ensure_ascii=False, sort_keys=True).encode()
    digest = hashlib.sha256(canonical).hexdigest()
    colors = ', '.join(str(COLORS[c]) + 'U' for c in config['colors'])
    curve = 'KEYBOARD_CURVE_LINEAR' if config.get('curve') == 'linear' else 'KEYBOARD_CURVE_SQUARE'
    header = f'''/* Generated by effect_profiles.py; edit the JSON files in configs/effects instead.
 * Effective configuration SHA256: {digest}
 */
#ifndef KEYBOARD_EFFECT_CONFIG_H
#define KEYBOARD_EFFECT_CONFIG_H
static const keyboard_effect_config_t keyboard_effect_config = {{
    .peak = {config['peak']}U,
    .steps_per_half = {config.get('steps_per_half', 64)}U,
    .step_ms = {config.get('step_ms', 48)}U,
    .gclk_hz = {config['gclk_hz']}U,
    .curve = {curve},
    .color_count = {len(config['colors'])}U,
    .colors = {{ {colors} }},
    .travel_ms = {config.get('travel_ms', 0)}U,
    .interval_ms = {config.get('interval_ms', 0)}U,
    .crest_ms = {config.get('crest_ms', 0)}U,
    .tail_ms = {config.get('tail_ms', 0)}U,
    .wave_smooth = {3 if config.get('wave_curve') == 'color-flow-blended' else 2 if config.get('wave_curve') == 'color-flow' else int(config.get('wave_curve') == 'smooth')}U,
    .floor = {config.get('floor', 0)}U,
    .color_blend = {config.get('color_blend', 0)}U
}};
#endif
'''
    output = Path(output)
    output.write_text(header, encoding='utf-8')
    generate_geometry(output.parent / 'keyboard_geometry.h')
    if config['effect'] == 'diagonal-wave':
        generate_wave_tables(config, output.parent / 'keyboard_wave_tables.h')
    output.with_suffix('.json').write_text(json.dumps(config, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--profile', type=Path, default=ROOT / 'configs/effects/classic-breathe.json')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--peak', type=int)
    parser.add_argument('--gclk-hz', type=int)
    args = parser.parse_args()
    try:
        generate(load_profile(args.profile, args.peak, args.gclk_hz), args.output)
    except (ValueError, OSError) as exc:
        parser.error(str(exc))


if __name__ == '__main__':
    main()
