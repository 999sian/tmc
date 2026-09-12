#!/usr/bin/env python3
"""Check production audio scratch declarations outlive a main-registered exit handler."""
from pathlib import Path
import re
import subprocess
import tempfile

source = (Path(__file__).resolve().parents[1] / 'port/port_m4a_backend.cpp').read_text()
# Preserve each declaration's namespace/function scope in a small lifetime probe.
declarations = re.findall(
    r'^( *)(static )?std::vector<(float|M4ACommand)> '
    r'(accL|accR|local|sMixAccL|sMixAccR|sDrainedCommands);$', source, re.M)
assert len(declarations) == 3, 'Update probe if the audio scratch declarations change'
globals_, locals_, uses = [], [], []
for indent, static, element, name in declarations:
    declaration = f'{static}TrackedVector<{element}> {name};'
    (locals_ if indent else globals_).append(declaration)
    uses.append(f'{name}.resize(4);')

code = r'''
#include <cassert>
#include <cstdlib>
#include <memory>
#include <vector>
static bool audioStopped = false;
struct M4ACommand { int value; };
template<class T> struct TrackedAllocator : std::allocator<T> {
    template<class U> struct rebind { using other = TrackedAllocator<U>; };
    void deallocate(T* p, std::size_t n) {
        assert(audioStopped && "audio scratch destroyed before callback shutdown");
        std::allocator<T>::deallocate(p, n);
    }
};
template<class T> using TrackedVector = std::vector<T, TrackedAllocator<T>>;
''' + '\n'.join(globals_) + '\nvoid render() {\n' + '\n'.join(locals_ + uses) + r'''
}
int main() {
    std::atexit([] { audioStopped = true; });
    render();
}
'''
with tempfile.TemporaryDirectory() as directory:
    root = Path(directory)
    (root / 'test.cpp').write_text(code)
    subprocess.run(['c++', '-std=c++17', str(root / 'test.cpp'), '-o', str(root / 'test')], check=True)
    subprocess.run([str(root / 'test')], check=True)
print('Audio scratch destructor ordering passed.')
