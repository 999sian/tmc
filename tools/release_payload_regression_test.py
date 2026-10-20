#!/usr/bin/env python3
"""Run the release workflow's real packaging shell against public-data fixtures."""
from pathlib import Path
import os
import subprocess
import tarfile
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
WORKFLOW = ROOT / '.github/workflows/_build.yaml'


def step_shell(name):
    lines = WORKFLOW.read_text().splitlines()
    start = lines.index('      - name: ' + name) + 1
    end = next((i for i in range(start, len(lines)) if lines[i].startswith('      - name: ')), len(lines))
    run = next(i for i in range(start, end) if lines[i] == '        run: |')
    return '\n'.join(line[10:] for line in lines[run + 1:end])


class ReleasePayloadTests(unittest.TestCase):
    def test_desktop_archive_contains_only_binary_and_public_defaults(self):
        for exe, system in (('tmc_pc', 'linux'), ('tmc_pc.exe', 'windows'), ('tmc_pc', 'macos')):
            with self.subTest(system=system), tempfile.TemporaryDirectory(prefix='tmc-release-payload-') as directory:
                root = Path(directory)
                expected = {
                    exe: b'fake executable',
                    'voxel_shapes.json': b'{"public": {}}\n',
                    'assets/rando/default.logic': b'public default logic\n',
                }
                fixtures = {
                    'build/pc/' + exe: expected[exe],
                    'assets/voxel_shapes.json': expected['voxel_shapes.json'],
                    'assets/rando/default.logic': expected['assets/rando/default.logic'],
                    'baserom.gba': b'private ROM must never ship',
                    'assets/usa/private.bin': b'ROM assets must never ship',
                    'artifact/stale': b'leftover must never ship',
                }
                for name, content in fixtures.items():
                    path = root / name
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_bytes(content)
                env = dict(os.environ, VERSION_LABEL='test', GITHUB_ENV=str(root / 'github-env'))
                values = {'exe_name': exe, 'os_name': system, 'arch': 'x86_64', 'region': 'multi'}
                for step in ('Prepare artifact payload', 'Create archive'):
                    shell = step_shell(step)
                    for key, value in values.items():
                        shell = shell.replace('${{ matrix.' + key + ' }}', value)
                    subprocess.run(['bash', '-e', '-o', 'pipefail', '-c', shell], cwd=root, env=env, check=True)
                with tarfile.open(root / f'tmc-multi-{system}-x86_64-test.tar.gz') as archive:
                    self.assertEqual(set(archive.getnames()), set(expected))
                    for name, content in expected.items():
                        self.assertEqual(archive.extractfile(name).read(), content)


if __name__ == '__main__':
    unittest.main()
