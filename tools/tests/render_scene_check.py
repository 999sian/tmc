#!/usr/bin/env python3
"""ROM-free regressions for scene capture validation and baseline safety."""
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from PIL import Image

SCRIPT = Path(__file__).resolve().parents[1] / 'render_scene_check.py'
spec = importlib.util.spec_from_file_location('render_scene_check', SCRIPT)
scene = importlib.util.module_from_spec(spec)
spec.loader.exec_module(scene)


class SceneChecks(unittest.TestCase):
    def test_pixels_not_png_compression(self):
        with tempfile.TemporaryDirectory() as directory:
            a, b = Path(directory) / 'a.png', Path(directory) / 'b.png'
            image = Image.new('RGB', (240, 160), 'red')
            image.save(a, compress_level=0)
            image.save(b, compress_level=9)
            self.assertEqual(scene.image_hash(a), scene.image_hash(b))
            Image.new('RGB', (240, 160), 'blue').save(b)
            self.assertNotEqual(scene.image_hash(a), scene.image_hash(b))
            Image.new('RGB', (240, 160), 'black').save(b)
            with self.assertRaisesRegex(ValueError, 'blank'):
                scene.image_hash(b)

    def test_bad_manifest(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'scenes.tsv'
            for row in ['../escape\tall\tTMC_ROOMCAP_MENU=1',
                        'scene\tall\tTMC_AUTOPLAY=0',
                        'scene\tall\tTMC_ROOMCAP_MENU=1\nscene\tall\tTMC_ROOMCAP_MENU=1']:
                path.write_text(row)
                with self.assertRaises(ValueError):
                    scene.read_scenes(path, 'usa', [])

    def test_ppu_update_rejects_capture_failure(self):
        with tempfile.TemporaryDirectory() as directory:
            tmp = Path(directory)
            compiler, game = tmp / 'gcc', tmp / 'game'
            compiler.write_text("#!/bin/sh\nwhile [ \"$1\" != '-o' ]; do shift; done\n"
                                "printf '#!/bin/sh\\necho 0x123\\n' > \"$2\"\nchmod +x \"$2\"\n")
            compiler.chmod(0o755)
            corpus, golden, rom = tmp / 'corpus', tmp / 'golden', tmp / 'rom'
            corpus.write_text('scene\tall\tatframe=1\ttest\n')
            rom.touch()
            golden.write_text('usa\tscene\told\told\neu\tscene\t0xe\t0xe\n')
            env = dict(os.environ, PATH=str(tmp) + os.pathsep + os.environ['PATH'],
                       TMC_PC=str(game), TMC_BASEROM=str(rom), PPU_CORPUS=str(corpus), PPU_GOLDEN=str(golden))
            command = ['bash', str(SCRIPT.parent / 'ppu_parity_check.sh'), '--update']
            for body in ['exit 2', 'echo snapshot > "$TMC_PERFCAP_DUMP"']:
                game.write_text('#!/bin/sh\n' + body + '\n')
                game.chmod(0o755)
                before = golden.read_bytes()
                result = subprocess.run(command, env=env, capture_output=True)
                self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertEqual(golden.read_bytes(), before)
            game.write_text('#!/bin/sh\necho snapshot > "$TMC_PERFCAP_DUMP"\necho frame_hash=0x123\n')
            result = subprocess.run(command, env=env, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual(sum(line.startswith('usa\t') for line in golden.read_text().splitlines()), 1)
            self.assertIn('eu\tscene\t0xe\t0xe', golden.read_text())

    def test_failed_update_preserves_baseline(self):
        with tempfile.TemporaryDirectory() as directory:
            tmp = Path(directory)
            rom, manifest, baseline = tmp / 'rom.gba', tmp / 'scenes.tsv', tmp / 'baseline.json'
            rom.write_bytes(bytes(0xAC) + b'BZME')
            manifest.write_text('example\tall\tTMC_ROOMCAP_MENU=1\n')
            baseline.write_text('{"kept": "unchanged"}\n')
            fake = tmp / 'game'
            prefix = f'#!{sys.executable}\nimport os,json,time\nfrom pathlib import Path\n'
            command = [sys.executable, str(SCRIPT), '--binary', str(fake), '--rom', str(rom),
                       '--manifest', str(manifest), '--baseline', str(baseline), '--artifacts', str(tmp / 'artifacts')]
            # A stale screenshot cannot make a failed or empty capture pass.
            stale = tmp / 'artifacts/usa/original/example.png'
            stale.parent.mkdir(parents=True)
            Image.new('RGB', (240, 160), 'red').save(stale)
            for body in ['pass', 'raise SystemExit(2)', 'time.sleep(2)']:
                fake.write_text(prefix + body + '\n')
                fake.chmod(0o755)
                result = subprocess.run(command + ['--update', '--timeout', '0.15'], capture_output=True)
                self.assertNotEqual(result.returncode, 0, result.stdout)
                self.assertEqual(json.loads(baseline.read_text()), {'kept': 'unchanged'})
            # Successful capture checks both isolation and inherited repro removal.
            fake.write_text(prefix + '''assert not Path('tmc.sav').exists()
assert 'TMC_REPRO_LEAK' not in os.environ
assert json.loads(Path('config.json').read_text())['menu_hint_seen'] is True
from PIL import Image
Image.new('RGB', (240,160), 'red').save(os.environ['TMC_ROOMCAP_OUT'])
''')
            env = dict(os.environ, TMC_REPRO_LEAK='1')
            result = subprocess.run(command + ['--update'], env=env, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('usa/original/example', json.loads(baseline.read_text()))
            before = baseline.read_bytes()
            for extra in [['--backend', 'gpu', '--update'], ['--region', 'eu', '--update']]:
                result = subprocess.run(command + extra, env=env, capture_output=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertEqual(baseline.read_bytes(), before)
            result = subprocess.run(command, env=env, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            original_script = fake.read_text()
            fake.write_text(original_script + '\nos.utime(__file__, (0, 0))\n')
            result = subprocess.run(command + ['--update'], env=env, capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(baseline.read_bytes(), before)
            fake.write_text(original_script)
            fake.write_text(fake.read_text().replace("(240,160), 'red'", "(240,160), 'blue'"))
            result = subprocess.run(command, env=env, capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertTrue((stale.parent / 'diff_example.png').is_file())


if __name__ == '__main__':
    unittest.main()
