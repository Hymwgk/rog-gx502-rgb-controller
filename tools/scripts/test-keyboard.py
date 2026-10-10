#!/usr/bin/env python3
"""Build keyboard test firmware on Ubuntu; flash only with --flash."""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path
from effect_profiles import load_profile

ROOT = Path(__file__).resolve().parents[2]
MODES = {name: index for index, name in enumerate(
    ('safe', 'signals', 'ctrl', 'channel', 'rgb-c', 'two-key', 'scan', 'c-brightness', 'breathe', 'wave'))}


def bounded(low, high):
    def parse(value):
        try:
            number = int(value)
        except ValueError:
            raise argparse.ArgumentTypeError('需要整数') from None
        if not low <= number <= high:
            raise argparse.ArgumentTypeError(f'范围必须为 {low}..{high}')
        return number
    return parse


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mode', choices=MODES, default=None)
    parser.add_argument('--profile', help='configs/effects/ 中的模式名，或 JSON 文件路径；默认 classic-breathe')
    parser.add_argument('--list-profiles', action='store_true', help='列出保存的效果配置')
    parser.add_argument('--ctrl-index', type=bounded(0, 7), default=0)
    parser.add_argument('--level', type=int, choices=(0, 1), default=1)
    parser.add_argument('--channel', type=bounded(0, 47), default=0)
    parser.add_argument('--brightness', type=bounded(1, 16384), default=None)
    parser.add_argument('--gclk-hz', type=int, choices=(100000, 500000, 1000000, 2000000), default=None)
    parser.add_argument('--seconds', type=bounded(1, 60), default=20)
    parser.add_argument('--flash', action='store_true', help='构建后烧录并校验')
    args = parser.parse_args()
    if args.list_profiles:
        for path in sorted((ROOT / 'configs/effects').glob('*.json')):
            profile = load_profile(path)
            print(f"{path.stem}: {profile['description']}")
        return
    if args.profile and args.mode not in (None, 'breathe', 'wave'):
        parser.error('--profile 只能用于 breathe 或 wave 模式')
    build_env = os.environ.copy()
    profile = None
    if args.profile or args.mode in ('breathe', 'wave'):
        name = args.profile or ('diagonal-wave' if args.mode == 'wave' else 'classic-breathe')
        profile_path = Path(name) if name.endswith('.json') else ROOT / 'configs/effects' / f'{name}.json'
        try:
            profile = load_profile(profile_path, args.brightness, args.gclk_hz)
        except (ValueError, OSError) as exc:
            parser.error(str(exc))
        expected = 'wave' if profile['effect'] == 'diagonal-wave' else 'breathe'
        if args.mode and args.mode != expected:
            parser.error(f'配置 effect 与 --mode {args.mode} 不匹配')
        args.mode = expected
        build_env['KB_EFFECT_PROFILE_FILE'] = str(profile_path.resolve())
        build_env['KB_EFFECT_PEAK'] = str(profile['peak'])
        build_env['KB_EFFECT_GCLK_HZ'] = str(profile['gclk_hz'])
        args.brightness = profile['peak']
        args.gclk_hz = profile['gclk_hz']
        print(f"效果配置：{profile['name']}；颜色 {'→'.join(profile['colors'])}；峰值 {profile['peak']}", flush=True)
    args.mode = args.mode or 'safe'
    if args.brightness is None:
        args.brightness = 1024
    if args.gclk_hz is None:
        args.gclk_hz = 2000000 if args.mode == 'breathe' else 100000
    if args.gclk_hz == 2000000 and args.mode not in ('breathe', 'wave'):
        parser.error('2 MHz GCLK 仅用于 breathe 或 wave 模式')
    defines = {
        'MODE': MODES[args.mode], 'CTRL': args.ctrl_index, 'LEVEL': args.level,
        'CHANNEL': args.channel, 'BRIGHTNESS': args.brightness,
        'GCLK_HZ': args.gclk_hz, 'SECONDS': args.seconds,
    }
    subprocess.run(['bash', str(ROOT / 'build.sh'),
                    *[f'KB_TEST_{key}={value}' for key, value in defines.items()]], check=True, env=build_env)
    snapshot = ROOT / 'build' / f'test-{args.mode}.elf'
    shutil.copyfile(ROOT / 'build/demo.elf', snapshot)
    if profile:
        # Save effective JSON beside the named binary, including CLI overrides.
        tag = re.sub(r'[^A-Za-z0-9_-]', '_', profile['name'])
        named = ROOT / 'build' / f'effect-{tag}.elf'
        shutil.copyfile(snapshot, named)
        named.with_suffix('.json').write_text(json.dumps(profile, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
        print(f'模式快照：{named}（同名 JSON 保存实际参数）', flush=True)
    print(f'已生成 {snapshot}', flush=True)
    if args.flash:
        subprocess.run(['bash', str(ROOT / 'flash.sh'), str(snapshot)], check=True)
        print('烧录并校验完成。')
    else:
        print('仅构建；添加 --flash 才会烧录。')
    if args.mode in ('scan', 'c-brightness', 'breathe', 'wave'):
        print('此模式持续循环，--seconds 不限制运行时间；烧录 safe 模式停止。')
    elif args.mode == 'safe':
        print('预期：CTRL 高阻、GCLK 停止、灰度数据为零。')
    else:
        print(f'测试约 {args.seconds} 秒后自动关闭。具体行为见 README.md。')


if __name__ == '__main__':
    try:
        main()
    except (subprocess.CalledProcessError, OSError) as exc:
        print(f'执行失败：{exc}', file=sys.stderr)
        sys.exit(1)
