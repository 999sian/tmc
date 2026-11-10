#!/usr/bin/env python3
"""Exercise production widescreen engine helpers without a ROM or SDL build."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent


def function(source, signature):
    start = source.index(signature)
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


def main():
    stubs = (ROOT / 'port/port_linked_stubs.c').read_text()
    message = (ROOT / 'src/message.c').read_text()
    roll_start = stubs.index('static u16 sWsRollBottom[')
    roll_state = stubs[roll_start:stubs.index('} sWsRoll;', roll_start) + len('} sWsRoll;')]
    source_start = stubs.index('#define WS_BG_SOURCE_SLOTS')
    source_state = stubs[source_start:stubs.index('} sWsBgSource[', source_start) + len('} sWsBgSource[WS_BG_SOURCE_SLOTS];')]
    functions = [roll_state, source_state] + [function(stubs, name) for name in (
        'static int Port_WidescreenSteadyFallback(void)',
        'int Port_Widescreen_FallbackNative(void)',
        'int Port_Widescreen_EffectiveViewWidth(void)',
        'static int Port_WidescreenSteadyViewWidth(void)',
        'int Port_Widescreen_LatchedFrameWidth(void)',
        'static int Port_WidescreenRollComposite(void)',
        'static u16 Port_WidescreenRollFetch(',
        'static void Port_WidescreenRollComposeBuffer(',
        'void Port_Widescreen_RollBegin(void)',
        'static void Port_WidescreenRollClamp(',
        'void Port_Widescreen_RollStart(void)',
        'void Port_Widescreen_RollStep(void)',
        'int Port_Widescreen_RollEnd(void)',
        'void Port_Widescreen_RollHandoff(void)',
        'int Port_Widescreen_CameraRestX(int target_x)',
        'int Port_Widescreen_ShadowsLive(void)',
        'static void Port_WidescreenShadow_Populate(',
        'static void Port_WidescreenShadow_PopulateOverlay(',
        'static int Port_WidescreenPpuBgForControl(',
        'void Port_Widescreen_BgWindowSource(',
        'static int Port_WidescreenShadow_PopulateSource(',
    )]
    functions.append(function(message, 'bool Message_GetWindowRect('))
    functions.append(function(stubs, 'static int Port_WidescreenHoldBlankFrame(void)'))
    functions.append(function(stubs, 'void Port_Widescreen_UpdateShadows(void)'))
    with tempfile.TemporaryDirectory(prefix='tmc-ws-engine-') as directory:
        generated = Path(directory) / 'widescreen_helpers.h'
        generated.write_text('\n'.join(functions))
        binary = Path(directory) / 'test'
        command = shlex.split(os.environ.get('CC', 'cc')) + [
            '-std=c11', '-Wall', '-Wextra', '-Werror', '-I' + directory,
            str(ROOT / 'tools/tests/widescreen_engine.c'), '-o', str(binary)]
        subprocess.run(command, check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    main()
