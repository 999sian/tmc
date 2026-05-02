# Changelog

## 0.1.1-experimental — 2026-05-02

First end-to-end release-tarball flow: download `tmc-usa-{linux,windows}-0.1.1-experimental.tar.gz`, drop a `baserom.gba` next to it, run `./asset_extractor` once, then `./tmc_pc`. Works on Linux and Windows (real and via wine). Tarball ships only `tmc_pc[.exe]`, `asset_extractor[.exe]`, and `sounds.json` (104 KB metadata).

### Fixed

- **Title-screen sword + hilltop BG render correctly on Windows.** Mingw's static-CRT heap allocates inside the simulated GBA address window (0x02000000-0x0A000000), so `port_resolve_addr` mistranslated heap pointers to `&gEwram[N]` and the title-scene palette load read zeros from EWRAM. Fixed by reserving the GBA address window with `VirtualAlloc(MEM_RESERVE)` before any heap is opened on Windows. Linux glibc keeps malloc above 0x55... so it was never affected. (`port_main.c`)
- **Title sword routing restored.** The `matheo/master` merge regressed `ad9b4d94`'s GBA-mode-1 → VirtuaPPU-mode-2 routing via a "case handling" cleanup. BG2 affine was reading text-BG indexing and the title sword came out as garbage tiles. (`port_ppu.cpp`)
- **`asset_extractor` works from a release tarball.** Old logic required walking up to find an `xmake.lua` and bailed otherwise; new logic always writes `assets/` and `assets_src/` next to the executable, which is the same path `tmc_pc` looks under, in both dev and release layouts. (`tools/src/assets_extractor/assets_extractor_main.cpp`)
- **`tmc_pc` finds assets and ROM next to the binary on Linux/macOS too.** Replaced the cwd-only lookup with `readlink(/proc/self/exe)` / `_NSGetExecutablePath`, mirroring the Windows `GetModuleFileNameW` path. Double-clicking `tmc_pc` from a file manager now works. (`port/port_asset_loader.cpp`, `port/port_rom.c`)
- **`sounds.json` discovered next to the binary.** Audio backend now probes `<exe_dir>/sounds.json` and `<exe_dir>/assets/sounds.json` before the cwd-relative dev paths. Release tarballs ship `sounds.json` at the top level. (`port/port_m4a_backend.cpp`)
- **ViruaPPU patches re-apply automatically every build.** `xmake.lua`'s `before_build` callback was disabled in a stash; re-enabled with `git apply -3` so partial-upstream drift no longer aborts the patch step. The mosaic patch (drifted, has known issues) is removed from the auto-apply list — `TMC_NO_MOSAIC` (`ea33eb71`) covers the gameplay paths.

### Added

- Merged `matheo/master`: `build.py` end-to-end build helper and `.github/workflows/{_build,ci,release}.yaml` release workflow. Tagging `v*` on a fork with `ASSETS_REPO`/`ASSETS_TOKEN` configured will produce `tmc-{usa,eur}-{linux,windows}-<tag>.tar.gz` artifacts.

### Known issues

- `~900` animations (Gyorg, Vaati cutscene, MazaalHand, etc.) are skipped during extraction with `Animation loop byte missing` / `Animation data has trailing bytes`. Bug in `infer_asset_size` / `WriteEditableAnimation`'s end-of-stream detection — affects both platforms identically. Animations not used by the title or early gameplay; will surface during specific cutscenes.
- Mosaic effect on title fade and certain spell charges is disabled (kill-switched). Patch needs re-porting against current ViruaPPU `mode1.c`.

## 0.1.0-experimental — earlier

Initial PC port snapshot.
