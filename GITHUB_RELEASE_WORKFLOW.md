# The Minish Cap 3DS Workflow

Read this file before working on every new request. It defines the permanent
development, validation, legal, and release process for the Nintendo 3DS port.

## Project Rules

- Keep all user-facing application, repository, and release text in English.
- Treat New Nintendo 3DS as the performance target. Keep Old Nintendo 3DS
  functional, and document measured limitations instead of hiding them.
- Preserve original GBA behavior. Platform-specific changes belong behind
  `TMC_3DS` or inside `platform/3ds` unless a shared fix is genuinely required.
- Never trade visual correctness for an unreported frame skip or timing hack.
- Keep the top image nearest-neighbor, correctly proportioned, centered, and
  free of stale tiles, gaps, duplicated scanlines, or cache artifacts.
- Keep the bottom screen useful and responsive without making its repaint cost
  block the game renderer.

## Legal Rules

- Never commit, upload, archive, or attach a ROM.
- Never publish extracted Nintendo graphics, audio, maps, save data, RAM dumps,
  VRAM dumps, PPU snapshots, or emulator bug reports.
- Release packages may contain source code, redistributable project artwork,
  configuration, the icon, banner, splash screen, and build metadata.
- Every user must provide a legally obtained clean USA ROM with SHA-1
  `b4bd50e4131b027c334547b4524e2dbbd4227130` in
  `sdmc:/3ds/The Minish Cap 3DS/`. Any `.gba` filename is accepted.

Before every commit and release, run:

```sh
git ls-files | rg -i '(^|/)(baserom.*\.gba|.*\.sav|.*\.ppu1|tmc3ds\.log|bugreport_.*)$'
```

The command must print nothing.

## Build

Requirements:

- devkitPro with devkitARM, libctru, Citro2D, and Citro3D
- CMake with the devkitPro Nintendo 3DS toolchain
- `makerom` and `bannertool` for CIA packaging

Build both public packages with:

```sh
./platform/3ds/build.sh
```

Expected outputs:

```text
build-3ds/game/tmc-3ds-vX.Y.Z.cia
build-3ds/game/tmc-3ds-vX.Y.Z.3dsx
```

## Required QA

Do not publish a build merely because it compiles. Complete these checks:

1. Confirm a clean build produces both CIA and 3DSX packages.
2. Install the CIA in Azahar using the New 3DS profile.
3. Verify splash, ROM validation, Nintendo/Capcom logos, title screen, file
   selection, intro, gameplay, pause menu, transitions, and HOME Menu close.
4. Verify D-pad, Circle Pad, A, B, L, R, Start, Select, and bottom-screen touch.
5. Verify music, sound effects, save creation, save reload, and a cold restart.
6. Inspect both screens for stale cache blocks, missing pixels, bad scaling,
   color-channel errors, flicker, overlap, and unreadable text.
7. Measure full-frame cadence on New 3DS in representative gameplay. A release
   may claim stable 60 FPS only when sustained measurements support it.
8. Run an Old 3DS profile smoke test and record its actual performance.
9. Confirm visible application text and public documentation are English.
10. Remove diagnostic frame dumps and noisy timing logs from release builds.

When PPU code changes, compare deterministic snapshots or frame hashes against
the desktop reference path. Any intended difference must be documented.

## Version Metadata

Update the version consistently in:

- `platform/3ds/CMakeLists.txt`
- `platform/3ds/build.sh`
- `platform/3ds/source/main_3ds.c`
- README and release notes
- output filenames and HOME Menu metadata

If a prior installed build keeps stale HOME Menu metadata, assign a new RSF
`UniqueId` for the next public version.

## Public Repository

The public repository is `EstebanPdN/zelda-tmc-3ds`. Preserve upstream credit
to samyost1, Project Picori, Raekwon1603, 999sian, and zeldaret as applicable.
Do not rewrite upstream history or imply ownership of Nintendo material.

Before pushing:

```sh
git status --short
git diff --check
git diff --stat
git ls-files | rg -i '(^|/)(baserom.*\.gba|.*\.sav|.*\.ppu1|tmc3ds\.log|bugreport_.*)$'
```

Stage only reviewed files. Never use `git add .` or `git add -A` in this
worktree.

## Pre-Releases

Use semantic tags such as `v0.2`. Early public builds must be marked as GitHub
pre-releases until gameplay, save, audio, input, and performance testing are
complete enough for a stable release.

The release title must be:

```text
The Minish Cap 3DS vX.Y.Z
```

Upload only:

```text
tmc-3ds-vX.Y.Z.cia
tmc-3ds-vX.Y.Z.3dsx
tmc-3ds-vX.Y.Z-source.zip
QR-vX.Y.Z-github.png
```

The QR image must encode the direct CIA asset URL:

```text
https://github.com/EstebanPdN/zelda-tmc-3ds/releases/download/vX.Y.Z/tmc-3ds-vX.Y.Z.cia
```

Release notes should contain a short legal notice, installation requirements,
known limitations, and only user-visible changes. Keep internal profiling,
hashes, and file-by-file implementation notes out of the public release body.

## Post-Publish Checks

After publishing, verify:

- the repository default branch and tag point to the intended commit;
- the release is marked as a pre-release when appropriate;
- CIA, 3DSX, source zip, and QR are the only attached assets;
- the QR image is visible and resolves to the direct CIA download;
- no ROM-derived or private diagnostic file is present in tracked source or
  release assets.
