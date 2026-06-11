# Project Picori — Code Review (2026-06-10)

Repo-wide review of the PC port layer, randomizer, build/CI, and the
uncommitted player diffs. Findings are severity-classified; every cited
line was read directly during review. The decompiled `src/` engine is
treated as intentionally faithful to GBA behavior except where a PC-only
change leaks outside an `#ifdef PC_PORT` guard.

## Verdict

The port layer is mature and carefully written — error paths funnel
through idempotent shutdowns, widescreen pitch math is correct, and the
decomp seam is disciplined. Risk concentrates in **code that parses
external/untrusted input** (ROM, `config.json`, save files, `.logic`,
`.glslp`, `sounds.json`) and in **CI supply-chain hygiene**.

---

## CRITICAL

### C1 — `lz77_decomp` has zero bounds checking
`port/port_bios.c:606-632`

`decompSize` is a 24-bit ROM header value (up to 16 MB) written into
fixed host buffers (`gVram` = 96 KB) with no capacity limit;
`dst[written - distance]` underflows when `distance > written` (u32 wrap
→ wild read ~4 GB back); `*src++` is never bounded by `gRomSize`. On GBA
these wrapped in mirrored VRAM — on PC they corrupt the host heap. ROM
is only SHA-1-checked at picker-install, **not at every load**, so
crafted/corrupt input reaches here.

**Fix:** pass the destination region capacity into
`LZ77UnCompVram/Wram`, clamp `decompSize`, reject `distance > written`,
bound `src` by ROM end.

### C2 — `Sqrt` infinite loop
`port/port_bios.c:736-743`

`while (r * r <= num) r++;` — `r*r` wraps in u32, so `num` near
`0xFFFFFFFF` loops forever; even normal inputs do an O(65535) scan.

**Fix:** integer Newton / bit-by-bit `isqrt`.

### C3 — Save file corruption on interrupted write
`port/port_save.c:136-153,179-193`

`FlushEepromFile` opens `"wb"` (truncates immediately) then writes;
crash/power-loss between truncate and write leaves a zero/short file,
which next load treats as "start fresh" → total save loss.
`EEPROMWrite0_8k_Check` flushes the whole 8 KB on **every 8-byte block**.

**Fix:** atomic write (temp + `fsync` + `rename`); batch per-block
flushes.

### C4 — CI: unpinned third-party supply chain under `contents:write`
`.github/workflows/release.yaml:35-36`, `_build.yaml:267-270`

Release builds run unpinned actions/tools and persist the write-scoped
`GITHUB_TOKEN` on disk. A moved tag or compromised upstream can push to
the repo / tamper release assets (cf. `tj-actions/changed-files`, March
2025, 23k repos).

**Fix:** SHA-pin every action, pin tool versions,
`persist-credentials: false`.

---

## HIGH (Warning)

### Crash-on-bad-input
- **Config type errors uncaught** · `port_runtime_config.cpp:443-524` —
  the `try/catch` wraps only the JSON *parse*; the `j.value(key,default)`
  extractions throw `type_error` if a key exists with the wrong type →
  `std::terminate`.
- **agbplay `Xcept` escapes the audio callback** ·
  `port_m4a_backend.cpp:521,630` — bad `sounds.json` offset throws a C++
  exception that unwinds through `extern "C"` `Render` → `std::terminate`
  on the SDL audio thread.

### Randomizer
- **Cosmetic edit silently de-randomizes a live seed** ·
  `port_imgui_menu.cpp:1303,1332` + `rando.cpp:533` — F8 color override
  reparse `Reset()`s the ~45 ground-item keys only `ActivateSeed`
  rebinds.
- **Saving one sidecar slot wipes others on load failure** ·
  `rando_save.c` — `SaveActiveSlot` does `LoadAll()` (memsets) then
  `SaveAll()`; version mismatch / short read zeroes other slots. Also
  non-atomic.
- **`external_logic` inferred from location count** · `rando.cpp:706-709`
  — a small `.logic` reload misclassified as built-in → seed determinism
  breaks across save/reload. Persist an explicit flag.
- **`.logic` `!ifdef` nesting >64 corrupts gating** ·
  `rando_logic.cpp:1201-1247` — overflow drops the push but `!endif`
  still pops.

### Shader presets (`.glslp`, user-supplied)
- **`/tmp` symlink + PATH popen** · `port_glslp_parser.cpp:415-438`.
- **No LUT dimension cap** · `parser:1436` + `runtime:288` — `w*h*4`
  straight from PNG header; signed overflow in size math.
- **Degenerate `shaders=0` accepted** · `parser:242` + `runtime:478`.
- **PrevN binding bugs** · `runtime:752,760`.
- **`blur5h.frag:16` hardcodes `1.0/240.0`** vs live framebuffer width.

### ROM / asset input
- **Integer overflow in extracted-page loader** · `port_rom.c:235,275` —
  `offset + fsize > gRomSize` wraps in u32; ≥4 GB file overflows the
  16 MB buffer.
- **Sprite frame-data not length-validated** · `port_draw.c:345`.

### Audio thread races (UB)
- DSP filter statics cleared on game thread while audio thread reads ·
  `port_audio.c:248-269`.
- `sStateMutex` held across context rebuild + disk I/O ·
  `port_m4a_backend.cpp:794`.

### Save path validation gap
`port_save.c:214,239` — `Port_Save_SetActivePath`/`SaveAsProfile` skip
the `IsManagedProfilePath` allowlist that Delete/Rename enforce.

### Build/CI robustness
- `git apply -3` conflict can satisfy `missing_markers()` while leaving
  conflict markers in the submodule · `xmake.lua:603-665`.
- `version_label`/tag interpolated into a `run:` script · `_build.yaml:539`.
- `build.py:551` uses CWD-relative `baserom.gba`.

---

## INFO / QUICK WINS
- **`CpuFastSet` is wrong (×8) but dead code** · `port_bios.c:672` — no
  callers repo-wide; latent landmine.
- **`Div` doesn't match GBA** · `port_bios.c:746` — returns 0 on
  div-by-zero (GBA returns ±1); `INT_MIN / -1` is UB.
- **Windows SAPI quoting** · `port_tts.cpp:413` — `"` in spoken text can
  break out of the PowerShell `-Command` string (POSIX path is clean).
- Per-frame `std::vector` allocs in `.glslp` present path
  (`runtime:668,833`) and ImGui render — violates zero-alloc frame rule.
- Crash handler uses non-async-signal-safe calls from `SIGSEGV` ·
  `port_bugreport.cpp:537-591` — acknowledged + mitigated.

---

## UNCOMMITTED DIFFS
- **`port_ppu.cpp` / `port_gpu_renderer.cpp`** (sampler plumbing): no
  regressions; commit-worthy.
- **`src/player.c` / `src/playerUtils.c`** (soft-slot effective-B): one
  fix needed — `SurfaceAction_CloneTile` (`player.c:3770`) makes the
  uninitialized-`n` `default:` case reachable for a non-sword soft-slot
  → OOB. Restrict to `ItemIsSword(effective)`.

---

## CROSS-CUTTING RECOMMENDATIONS
1. Treat every file the game opens as hostile — shared validated-read
   helper (bounds + exception barrier).
2. One crash-safe write primitive (temp+fsync+rename) reused by
   `port_save.c`, `rando_save.c`, config.
3. SHA-pin CI, drop persisted credentials, add `zizmor`/Scorecard.
4. Exception barriers at every `extern "C"` C++→C boundary that can throw.

## References
- Atomic write pattern — https://lwn.net/Articles/789600/
- GitHub Actions hardening — https://docs.github.com/en/actions/reference/security/secure-use
- SDL3 audio stream locking — https://wiki.libsdl.org/SDL3/SDL_LockAudioStream

---

## REMEDIATION LOG (2026-06-10)

Fixes applied and verified after the review. Build: `xmake build tmc_pc`
clean; headless autoplay reaches `Entering AgbMain` and runs.

### Round 1 — Criticals + localized Highs
- **C1** `port_bios.c` `lz77_decomp` now takes dest-region capacity + ROM-end
  bound; clamps `decompSize`, refuses `distance > written`, stops at `srcEnd`.
- **C2** `port_bios.c` `Sqrt` replaced with bit-by-bit integer sqrt
  (terminates at `0xFFFFFFFF`, returns `floor(sqrt)`).
- **C3** `port_save.c` `WriteEepromAtomic` (temp + `fsync`/`_commit` +
  `rename`/`MoveFileEx`); `FlushEepromFile` routes through it.
- `port_runtime_config.cpp` extraction wrapped in a re-appliable lambda under
  `try/catch` → malformed `config.json` degrades to defaults, no `terminate`.
- `port_save.c` `SetActivePath`/`SaveAsProfile` enforce `IsManagedProfilePath`.
- `port_rom.c` extracted-page loader clamps in 64-bit + guards `ftell<0`.
- `port_bios.c` `Div` guards `INT_MIN/-1`; `CpuFastSet` word-count fixed.
- `src/player.c` `SurfaceAction_CloneTile` substitutes the soft-slot item only
  when `ItemIsSword`, keeping the uninitialized-`n` `default:` unreachable;
  stray include tabs removed.
- Verified: standalone unit harnesses for the save round-trip (atomic write,
  no temp litter, path-traversal refusal) and the BIOS algorithms
  (`Sqrt(0xFFFFFFFF)`, `INT_MIN/-1`, malformed-LZ77 clamp/refuse) — both PASS.

### Round 2 — audio barrier + CI hardening
- **agbplay exception barrier** (`port_m4a_backend.cpp`): `AudioGuardWarn` +
  `try/catch` around `m4aSoundMain` (RenderChunkLocked), `m4aMPlayStart`
  (StartSongById/StartSong), and context rebuild. `Xcept` (: `std::exception`)
  can no longer unwind across the `extern "C"` boundary. Verified by
  fault-injecting in-range bad `sounds.json` offsets: the process now logs
  `[AUDIO] contained exception …` and survives instead of `SIGABRT`.
- **C4 CI** (`.github/workflows/*.yaml`): all 5 external actions SHA-pinned
  (`actions/checkout`, `upload-artifact`, `download-artifact`,
  `softprops/action-gh-release`, `xmake-io/github-action-setup-xmake`);
  `persist-credentials: false` on both checkouts; `version_label` moved out of
  the `run:` shell into `env:`; `release.yaml` `contents: write` scoped to the
  `create-release` job only. YAML validated.

### Round 3 — randomizer
- **Live-seed de-randomize on mid-game reparse** (`port_imgui_menu.cpp`): the
  F8 cosmetic edit/remove paths now call `Rando_Keymap_Apply()` after
  `RandoLogic_Reparse()` (guarded by `Rando_IsActive()`), rebinding the
  ground-item/scripted location keys the reparse cleared — dungeon items no
  longer revert to vanilla. File-menu reparses are pre-generation, unaffected.
- **`external_logic` determinism** (`rando.cpp` `Rando_ActivateTable`):
  classify by `RandoLogic_IsLoaded()` instead of the `count >
  RANDO_LOCATION_BUILTIN_COUNT` heuristic, so a small external `.logic`
  (≤ builtin count) reloads as external. Backward-compatible (no sidecar
  version bump). Generation paths already set the flag directly.
- **Sidecar wipe + non-atomic write** (`rando_save.c`): `WriteSidecarFile`
  (temp + `fsync`/`_commit` + `rename`/`MoveFileEx`); `SaveActiveSlot`/
  `ClearSlot` back up an existing-but-unreadable sidecar to `.bak` before
  overwriting instead of silently zeroing the other slots.
- **`.logic` `!ifdef` nesting overflow** (`rando_logic.cpp`
  `ProcessDirective`): depth keeps counting past the 64-frame cap so every
  `!endif` stays balanced; over-deep regions are forced inactive and never
  touch `stack[]`. A >64-deep malformed file can no longer desync the parse.
- Verified: `rando_logic_test` (offline self-test) PASS; full headless smoke
  parses real `default.logic` (882 locations) and runs clean.

### Build note
The build/host clock runs ahead of the file-write clock, so freshly edited
sources can look *older* than existing objects and xmake skips them. After
editing, `touch` the changed sources forward (or `xmake -r`) and confirm the
`compiling.release <file>` lines appear before trusting a binary.

### Round 4 — `.glslp` hardening + remaining robustness
- **LUT dimension cap** (`port_glslp_parser.cpp` `DecodePng`): reject `w/h==0`
  or `> 8192` before the `w*h*4` allocation — a crafted PNG header can no
  longer force a multi-GB alloc / `bad_alloc`.
- **Secure temp** (`port_glslp_parser.cpp`): `mkstemp` (O_EXCL + unpredictable
  suffix, honoring `$TMPDIR`) replaces the guessable `/tmp/tmc_glslp_in_<pid>`
  path, closing the symlink-follow window; write via the fd.
- **`shaders=0` / present failure** (`parser` + `runtime` + `port_gpu_renderer.cpp`):
  parser requires `1..64` passes; `IsActive()` also requires a non-empty pass
  list; the present caller now gates on `PresentFrame()`'s return and falls
  through to the stock path, so ImGui never overlays an undefined swapchain.
- **PrevN bindings** (`port_glslp_runtime.cpp`): ring guard `n < kPrevCount`
  (was `<=`, which aliased the current frame as the oldest history); `PassPrev`
  guard `> 8+7` (was `> 12+7`, which made `PassPrev1Texture` dead code).
- **Blur texel size** (`blur5h.frag`, `crt_composite.frag`, `crt_rf.frag`):
  `1.0/float(textureSize(src,0).x)` instead of the hardcoded `1.0/240.0`, so
  neighbor taps are correct on the 384 widescreen / internal-scaled framebuffer.
  SPIR-V regenerated via `port/shaders/build.sh`.
- **Audio DSP filter-state race** (`port_audio.c`): the game-thread reset /
  GBA-accurate toggle now set an atomic `sFilterClearPending` flag; the audio
  thread memsets the HPF/LPF history at the top of `PostProcess`. No more
  cross-thread memset of statics the callback is reading.
- **Sprite over-read** (`port_draw.c` `RenderSpritePieces`): when frame data
  lies inside `gFrameObjLists`, clamp `count` to the bytes remaining so a
  corrupt/truncated ROM can't read 5×count past the array.
- **xmake patch conflict detection** (`xmake.lua`): `missing_markers()` now
  treats a marker file containing `<<<<<<<`/`>>>>>>>` as not-applied, so a
  3-way-merge conflict triggers the self-heal reset + hard-fail instead of
  compiling a conflict-marked renderer source.
- Verified: default and `--gpu_renderer=y` builds both compile clean (every
  edited file, incl. the GPU-gated runtime/renderer paths); shaders recompile;
  headless smoke runs clean.

### Still outstanding
None from the original review's HIGH/INFO list remain. Residual notes: the
xmake self-heal `git checkout -- .` still discards uncommitted submodule WIP
(intentional CI behavior); GPU shader-path fixes are compile-verified but
need real-GPU runtime exercise; `version_label` env fix and CI pinning assume
maintainers re-pin SHAs on future action bumps.
