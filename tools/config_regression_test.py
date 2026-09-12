#!/usr/bin/env python3
"""Compile and run config numeric-boundary regressions (no ROM required).

Requires SDL3 headers and nlohmann-json; --json-include overrides discovery.
Runs with UBSan, including float-to-integer overflow checks.
"""
import argparse
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--json-include', type=Path)
    args = parser.parse_args()
    json_include = args.json_include
    if json_include is None and not Path('/usr/include/nlohmann/json.hpp').exists():
        headers = sorted((Path.home() / '.xmake/packages/n/nlohmann_json').glob('**/include/nlohmann/json.hpp'))
        if not headers:
            parser.error('install nlohmann-json or pass --json-include')
        json_include = headers[0].parent.parent
    sdl_flags = shlex.split(subprocess.check_output(['pkg-config', '--cflags', 'sdl3'], text=True))
    command = shlex.split(os.environ.get('CXX', 'c++')) + [
        '-std=c++17', '-Iport', '-ffunction-sections', '-fdata-sections',
        '-fsanitize=undefined,float-cast-overflow', '-fno-sanitize-recover=all',
    ] + sdl_flags
    if json_include:
        command += ['-I' + str(json_include)]
    with tempfile.TemporaryDirectory(prefix='tmc-config-test-') as directory:
        tmp = Path(directory)
        binary = tmp / 'config-test'
        subprocess.run(command + ['port/port_runtime_config.cpp', 'tools/tests/runtime_config.cpp',
                                 '-Wl,--gc-sections', '-o', str(binary)], cwd=ROOT, check=True)
        subprocess.run([str(binary), str(tmp / 'config.json')], check=True)


if __name__ == '__main__':
    main()
