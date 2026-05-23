#!/bin/bash
# Compile the SDL_GPU shader sources to SPIR-V.
#
# Run this from the repo root or from port/shaders/ — either works.
# Output lands in port/shaders/build/ and is committed (the binaries
# are small, ~1 KB each, and committing them avoids forcing every
# build host to install a Vulkan SDK).
#
# Re-run whenever a .vert or .frag file changes. CI will not re-run
# this; the .spv files in port/shaders/build/ are the source of truth
# at build time.
#
# Dependencies:
#   - glslangValidator (Vulkan SDK; or `apt install glslang-tools`,
#     `brew install glslang`, `pacman -S glslang`)

set -euo pipefail

cd "$(dirname "$0")"
out=build
mkdir -p "$out"

for vert in *.vert; do
    [ -f "$vert" ] || continue
    name="${vert%.vert}"
    echo "  vert  $vert"
    glslangValidator -V -S vert -o "$out/$name.vert.spv" "$vert"
done

for frag in *.frag; do
    [ -f "$frag" ] || continue
    name="${frag%.frag}"
    echo "  frag  $frag"
    glslangValidator -V -S frag -o "$out/$name.frag.spv" "$frag"
done

echo "done — emitted SPIR-V to $out/"
