#!/usr/bin/env bash
# Exercise dialogue geometry and the production message-canvas query without SDL.
set -euo pipefail
cd "$(dirname "$0")/.."
test_dir=$(mktemp -d)
trap 'rm -rf "$test_dir"' EXIT
"${CXX:-c++}" -std=c++17 -Wall -Wextra -pedantic tools/hd_text_regression_test.cpp -o "$test_dir/geometry"
"$test_dir/geometry"
flags=(-std=gnu11 -DPC_PORT -DGAME_VERSION_USA -Iinclude -Iport -I. -Ibuild/USA -ffunction-sections -fdata-sections)
"${CC:-cc}" "${flags[@]}" -c src/message.c -o "$test_dir/message.o"
"${CC:-cc}" "${flags[@]}" tools/tests/hd_text_canvas.c "$test_dir/message.o" -Wl,--gc-sections -o "$test_dir/canvas"
"$test_dir/canvas"
printf '%s\n' 'HD dialogue canvas: passed'
