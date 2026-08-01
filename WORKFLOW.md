# The Minish Cap 3DS Working Protocol

Current baseline: v0.2
Last updated: 2026-08-01

## Mandatory First Step

Read this file, `GITHUB_RELEASE_WORKFLOW.md`, `README.md`, and the newest
GitHub release notes before editing code or publishing anything. Do not work
from chat memory alone.

## User Preferences

- Do exactly what Esteban asks first. Do not make extra user-experience,
  visual, logging, release, or repository changes unless they are necessary to
  satisfy the request or Esteban explicitly asks for them.
- Development builds may show the bottom-screen console/log output. Do not
  remove or hide it unless Esteban asks for that behavior.
- When fixing a bug, fix the cause. Do not hide symptoms by removing useful
  debug visibility.
- Public app text, README content, release notes, and GitHub metadata must be
  in English.
- Explain the cause of a problem before changing/publishing when Esteban asks
  what happened.
- Preserve unrelated user changes. If the remote has a new commit, inspect it
  and integrate it instead of overwriting it.

## Project Goal

Create and maintain a native dual-screen Nintendo 3DS port of The Minish Cap
based directly on:

- https://github.com/samyost1/tmc-android
- https://github.com/999sian/tmc
- https://github.com/zeldaret/tmc

The port should provide:

- Top-screen fullscreen gameplay.
- Bottom-screen map, status, dungeon and touch UI.
- CIA and Homebrew Launcher 3DSX builds.
- New Nintendo 3DS performance improvements while keeping Old Nintendo 3DS
  functional.
- Quick diagnostics through `L + R + A`.

## Legal Rules

1. Never commit, upload, archive, or release a ROM.
2. Never commit, upload, archive, or release save files, extracted Nintendo
   assets, RAM/VRAM/PPU dumps, emulator bug reports, or private diagnostics.
3. Release packages may include source code, 3DS build scripts, executable
   packages, redistributable artwork, QR images, and metadata only.
4. Users provide their own legally obtained clean USA ROM with SHA-1
   `b4bd50e4131b027c334547b4524e2dbbd4227130`.

Before every commit and release:

```sh
git status --short
git diff --check
git diff --stat
git ls-files | rg -i '(^|/)(baserom.*\.gba|.*\.sav|.*\.ppu1|tmc3ds\.log|bugreport_.*)$'
```

The last command must print nothing.

## Work Session Protocol

1. Read this file and the release workflow.
2. Inspect local `git status` and remote state before pushing.
3. Reproduce or reason from the exact user report, screenshots, logs or dumps.
4. Make the smallest correction that addresses the requested issue.
5. Keep debug tools visible unless Esteban asks to hide them.
6. Build locally with `./platform/3ds/build.sh`.
7. Commit only reviewed files using explicit `git add -- <paths>`.
8. Push to `EstebanPdN/zelda-tmc-3ds` only when publication is requested or
   when fixing already-published CI/release state.
9. Watch GitHub Actions after workflow changes and fix failures until green.
10. For releases, use GitHub release assets and QR codes that point directly to
    the CIA asset URL.

## Release Rules

- Use short development tags such as `v0.1`, `v0.2`, `v0.3`.
- Mark early builds as GitHub pre-releases.
- Release bodies should follow the ALttP style: QR image first, then
  `## Changelog`, `## Installation`, and `## Note`.
- Keep changelogs focused on what changed in that version.
- Do not pad release notes with unchanged behavior.
- Do not delete or rename releases casually. If Esteban asks for naming
  cleanup, keep URLs/assets consistent with the visible tag.

## Version Memory

### v0.2 - 2026-08-01

- Fixed a 3DS boot hang after `Port_InitDataStubs` by skipping the PC
  `rom_data/` page extraction cache on Nintendo 3DS.
- Added quick diagnostics: `L + R + A` writes `top-screen.bmp`,
  `bottom-screen.bmp`, `vram.bin`, `palettes.bin`, `oam.bin`, and `info.txt`
  under `sdmc:/3ds/The Minish Cap 3DS/dumps/`.
- Restored development bottom-screen console output after Esteban clarified it
  should remain visible.
- Fixed GitHub Actions expectations so missing CIA packaging tools in CI do
  not fail the entire 3DSX build.

### v0.1 - 2026-08-01

- First public pre-release.
- Added native 3DS platform, CIA/3DSX local packaging, splash/icon/banner,
  PICA200/Citro2D presenter, NDSP audio, ROM scanning for any `.gba` filename,
  and GitHub release/QR layout.
