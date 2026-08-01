# Repository Guidelines

Read `WORKFLOW.md` and `GITHUB_RELEASE_WORKFLOW.md` before starting every
request. Their user-preference, legal, QA, packaging, and publishing rules are
mandatory for this Nintendo 3DS port.

## Project Overview

The Minish Cap 3DS is a native Nintendo 3DS port of Project Picori based on
the dual-screen Android work by samyost1. The public repository contains source
code, 3DS platform glue, build scripts, and redistributable artwork only.

The project never distributes a ROM, extracted Nintendo assets, save data,
runtime dumps, emulator logs, or firmware. Users provide their own clean USA
ROM on the SD card under `sdmc:/3ds/The Minish Cap 3DS/`. Any `.gba` filename
is accepted.

## Main 3DS Areas

- `platform/3ds/` - 3DS entry point, build scripts, CIA metadata, assets, GPU
  presenter, NDSP audio, input, storage, and platform services.
- `port/` - shared port layer used by the engine, including ROM loading,
  runtime config, audio integration, bottom-screen state, and save handling.
- `port/ppu/` - software GBA PPU. 3DS-specific render optimizations must keep
  visual parity with the desktop reference path.
- `src/` and `include/` - decompiled game logic and GBA data structures.
- `.github/workflows/` - public CI/release automation for 3DS packages.

## Development Rules

- Keep public documentation, release notes, HOME Menu metadata, and visible app
  text in English.
- Keep platform-specific changes behind `TMC_3DS` or inside `platform/3ds`
  unless a shared fix is necessary.
- Preserve original GBA behavior. Do not hide performance problems with
  unreported frame skipping or timing hacks.
- Prefer fixed-size buffers and zero-allocation frame paths for 3DS runtime
  code.
- Keep the top screen nearest-neighbor, correctly centered, and free of stale
  tiles, duplicated scanlines, gaps, or color-channel errors.
- Keep the bottom screen cached and responsive so UI updates do not dominate
  frame time.

## Build

Use devkitPro with devkitARM, libctru, Citro2D, Citro3D, CMake, makerom, and
bannertool.

```sh
./platform/3ds/build.sh
```

Expected public outputs:

```text
build-3ds/game/tmc-3ds-v0.2.cia
build-3ds/game/tmc-3ds-v0.2.3dsx
```

The build must not embed any ROM.

## QA Expectations

Before publishing, test at least splash, ROM validation, logos, title screen,
file select, gameplay, pause, transitions, audio, saves, controls, touch, and
HOME Menu close. New Nintendo 3DS is the 60 FPS target; Old Nintendo 3DS should
remain functional with measured limitations documented in release notes.

Before every commit or release:

```sh
git status --short
git diff --check
git diff --stat
git ls-files | rg -i '(^|/)(baserom.*\.gba|.*\.sav|.*\.ppu1|tmc3ds\.log|bugreport_.*)$'
```

Stage reviewed paths explicitly with `git add -- <paths>`. Never use
`git add .`, `git add -A`, or `git add --all` in this worktree.
