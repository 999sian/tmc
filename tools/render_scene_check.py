#!/usr/bin/env python3
"""Check named deterministic rendering scenes; --update explicitly approves pixels.

Requires Pillow and a ROM. This presentation gate is separate from the byte-exact
PPU oracle. Baselines contain hashes, never ROM-derived screenshots. PNGs, logs,
and mismatch diffs remain in --artifacts for visual review.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile

from PIL import Image, ImageChops

ROOT = Path(__file__).resolve().parent.parent


def read_scenes(path, region, selected):
    scenes = []
    names = set()
    for line in path.read_text().splitlines():
        if not line.strip() or line.startswith('#'):
            continue
        name, regions, overrides = line.split('\t')
        if not re.fullmatch(r'[a-z0-9_]+', name) or name in names:
            raise ValueError(f'invalid/duplicate scene: {name}')
        names.add(name)
        env = dict(token.split('=', 1) for token in overrides.split())
        if any(not key.startswith('TMC_ROOMCAP_') for key in env):
            raise ValueError(f'{name}: only ROOMCAP overrides are allowed')
        if (regions == 'all' or region in regions.split(',')) and (not selected or name in selected):
            scenes.append((name, env))
    if selected and set(selected) - names:
        raise ValueError(f'unknown scenes: {set(selected) - names}')
    if not scenes:
        raise ValueError('no scenes selected for this region')
    return scenes


def image_hash(path):
    with Image.open(path) as image:
        image.load()
        image = image.convert('RGBA')
        if image.convert('RGB').getextrema() == ((0, 0), (0, 0), (0, 0)):
            raise ValueError(f'capture is blank: {path}')
        if image.width < 240 or image.height < 160:
            raise ValueError(f'capture is too small: {path}')
        return hashlib.sha256(f'{image.width}x{image.height}:'.encode() + image.tobytes()).hexdigest()


def compare(actual, expected, baseline_image, diff_path):
    digest = image_hash(actual)
    if digest == expected:
        return digest, True
    if baseline_image.is_file():
        with Image.open(actual) as got, Image.open(baseline_image) as want:
            if got.size == want.size:
                ImageChops.difference(got.convert('RGB'), want.convert('RGB')).save(diff_path)
    return digest, False


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, default=Path(os.environ.get('TMC_PC', ROOT / 'build/pc/tmc_pc')))
    parser.add_argument('--rom', type=Path, default=Path(os.environ.get('TMC_BASEROM', ROOT / 'baserom.gba')))
    parser.add_argument('--region', choices=['usa', 'eu', 'jp'], default='usa')
    parser.add_argument('--profile', choices=['original', 'widescreen', 'hd'], default='original')
    parser.add_argument('--backend', choices=['software', 'gpu'], default='software')
    parser.add_argument('--manifest', type=Path, default=ROOT / 'tools/render_scenes.tsv')
    parser.add_argument('--baseline', type=Path, default=ROOT / 'tools/render_scene_hashes.json')
    parser.add_argument('--artifacts', type=Path, default=ROOT / 'build/render-scenes')
    parser.add_argument('--scene', action='append')
    parser.add_argument('--timeout', type=float, default=90)
    parser.add_argument('--video-driver', default='dummy', help='use x11/wayland for a real display')
    parser.add_argument('--update', action='store_true', help='explicitly accept all selected captures')
    args = parser.parse_args()
    if args.timeout <= 0:
        parser.error('--timeout must be positive')
    try:
        scenes = read_scenes(args.manifest, args.region, args.scene)
        baseline = json.loads(args.baseline.read_text()) if args.baseline.exists() else {}
        if not isinstance(baseline, dict):
            raise ValueError('baseline must be an object')
        binary, rom = args.binary.resolve(strict=True), args.rom.resolve(strict=True)
        with rom.open('rb') as file:
            file.seek(0xAC)
            game_code = file.read(4)
        if game_code != {'usa': b'BZME', 'eu': b'BZMP', 'jp': b'BZMJ'}[args.region]:
            raise ValueError(f'ROM game code {game_code!r} does not match --region {args.region}')
    except (OSError, ValueError) as error:
        parser.error(str(error))
    binary_mtime = binary.stat().st_mtime_ns
    run_key = f'{args.region}/{args.profile}'
    artifacts = args.artifacts.resolve() / args.region / args.profile
    if args.backend == 'gpu':
        run_key += '/gpu'
        artifacts /= 'gpu'
    artifacts.mkdir(parents=True, exist_ok=True)
    actual = {}
    failed = False
    for name, overrides in scenes:
        # Remove only this run's outputs: a failed process must never reuse a stale PNG.
        outputs = [name + '_a.png', name + '_b.png'] if overrides.get('TMC_ROOMCAP_FILESELECT') else [name + '.png']
        for output in outputs:
            (artifacts / output).unlink(missing_ok=True)
            (artifacts / ('diff_' + output)).unlink(missing_ok=True)
        with tempfile.TemporaryDirectory(prefix='tmc-scene-') as directory:
            cwd = Path(directory)
            config = dict(widescreen_enabled=args.profile != 'original', hd_dialogue=args.profile == 'hd',
                          window_scale=3, internal_scale=1, upscale_method='nearest', render_backend=args.backend,
                          color_correction=False, lcd_persistence=False, vsync=False,
                          menu_art_background=args.profile != 'original', menu_hint_seen=True,
                          decouple_render=False, show_fps=False, ra_enabled=False, discord_rpc=False)
            (cwd / 'config.json').write_text(json.dumps(config))
            # A clean environment prevents unrelated repro/autoplay/config switches leaking in.
            env = {key: value for key, value in os.environ.items()
                   if not key.startswith(('TMC_', 'SDL_'))}
            env.update(XDG_CONFIG_HOME=directory, XDG_DATA_HOME=directory,
                       TMC_AUTOPLAY='1', TMC_ROOMCAP='1', TMC_ROOMCAP_SETTLE='300',
                       TMC_ROOMCAP_OUT=str(artifacts / (name if len(outputs) == 2 else outputs[0])),
                       TMC_ROOMCAP_PRESENTATION='0' if args.profile == 'original' else '1',
                       TMC_BASEROM=str(rom), TMC_COLOR_CORRECTION='0', TMC_LCD_PERSISTENCE='0',
                       TMC_NO_UPDATE_CHECK='1', SDL_VIDEODRIVER=args.video_driver,
                       SDL_AUDIODRIVER='dummy', SDL_RENDER_DRIVER='software', OMP_NUM_THREADS='1')
            if args.profile != 'original':
                env['TMC_ROOMCAP_WINDOW'] = '960,540'
            env.update(overrides)
            log_path = artifacts / (name + '.log')
            with log_path.open('wb') as log:
                try:
                    result = subprocess.run([str(binary), '--no-audio'], cwd=cwd, env=env,
                                            stdout=log, stderr=subprocess.STDOUT, timeout=args.timeout)
                    if result.returncode:
                        raise ValueError(f'game exited {result.returncode}')
                    log.flush()
                    if args.backend == 'gpu' and 'PPU initialized with SDL_GPU backend' not in log_path.read_text(errors='replace'):
                        raise ValueError('requested GPU backend was unavailable; refusing software fallback')
                    for output in outputs:
                        path = artifacts / output
                        key = f'{run_key}/{Path(output).stem}'
                        digest, matches = compare(path, baseline.get(key), artifacts / ('baseline_' + output),
                                                  artifacts / ('diff_' + output))
                        actual[key] = digest
                        if not args.update and not matches:
                            raise ValueError(f'pixel mismatch or missing baseline: {key}')
                except (OSError, ValueError, subprocess.TimeoutExpired) as error:
                    print(f'FAIL {name}: {error}; artifacts: {artifacts}')
                    failed = True
                    continue
        print(f'{"RECORD" if args.update else "PASS"} {run_key}/{name}')
    if binary.stat().st_mtime_ns != binary_mtime:
        print('FAIL: game binary changed during capture; rerun after the build finishes.')
        failed = True
    (artifacts / 'actual_hashes.json').write_text(json.dumps(actual, indent=2, sort_keys=True) + '\n')
    if args.update and not failed:
        baseline.update(actual)
        args.baseline.parent.mkdir(parents=True, exist_ok=True)
        temporary = args.baseline.with_suffix('.tmp')
        temporary.write_text(json.dumps(baseline, indent=2, sort_keys=True) + '\n')
        temporary.replace(args.baseline)
        for name, _ in scenes:
            for path in artifacts.glob(name + '*.png'):
                with Image.open(path) as image:
                    image.save(artifacts / ('baseline_' + path.name))
        print(f'Updated {args.baseline}; review PNGs in {artifacts}')
    elif args.update:
        print('Baseline unchanged: one or more scenes failed.')
    return 1 if failed else 0


if __name__ == '__main__':
    raise SystemExit(main())
