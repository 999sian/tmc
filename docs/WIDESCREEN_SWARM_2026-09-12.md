# Widescreen swarm investigation — 2026-09-12

**Update: all six findings have been addressed.** See the fix verification
at the end. The investigation below records the original evidence and limits;
its source line references precede the fixes.

Scope: three parallel read-only reviews of rendering, gameplay/camera, and UI/config, plus coordinator verification of build/test coverage. Existing local edits were preserved. No production code was changed. Findings describe the working tree inspected, including its pre-existing edits.

## Findings

### 1. P2 — Revealed columns erase valid sprites and backdrop

**Reproduced with a standalone CPU render.** `port/ppu/src/mode1.c:1075-1083` replaces the final composite with black at x >= 240 unless some background produced a nontransparent pixel. This happens after sprite composition, so a valid sprite over transparent backgrounds is discarded. The nonblack backdrop is discarded as well. The GPU shader repeats the condition at `port/shaders/ppu_core.glsl:612-618`; GPU behavior was inspected, not executed.

Synthetic setup: framebuffer capacity 384, rendered width 284, an 8-pixel sprite starting at x=236, transparent backgrounds, green backdrop. Actual output:

```text
sprite x239=ff0000f8 x240=ff000000; backdrop x239=ff00f800 x240=ff000000
```

The sprite should continue through x=243 and the backdrop should retain its color. Temporary source and binary: `/tmp/ws_composite_repro.c`, `/tmp/ws_composite_repro`. Reproduction command:

```bash
gcc -O2 -I port/ppu/include -DMODE1_GBA_WIDTH=384 /tmp/ws_composite_repro.c port/ppu/src/mode1.c -lm -o /tmp/ws_composite_repro
/tmp/ws_composite_repro
```

Fix direction: distinguish invalid room coverage from transparent BG pixels; preserve valid OBJ/backdrop composition inside the viewport. Add an expected-output regression, since CPU/GPU parity alone cannot detect a shared mistake. No particular gameplay room was demonstrated to encounter this condition.

### 2. P2 — GPU parity target silently excludes widescreen tests

**Confirmed through source and `xmake show -t ppu_gpu_parity`.** `xmake.lua:639-640` injects `MODE1_GBA_WIDTH` into `tmc_pc`, but the separate parity target at `xmake.lua:1167-1182` does not receive it. Its compiler flags contain no width define even with the current project configured for 384. The header therefore defaults to 240 (`port/ppu/include/cpu/mode1.h:28-29`), excluding the four widescreen scenes guarded at `tools/ppu_gpu_parity.cpp:716` and `:968`.

Fix direction: apply the configured width consistently to the parity target and its renderer sources; verify that wide builds actually enumerate the widescreen scenes. The committed live PPU corpus also contains only logo/title captures, not gameplay (`tools/ppu_corpus.txt`).

### 3. P2 — Digging-cave transition window remains 240 pixels wide

**Source-confirmed condition; visual reproduction outstanding.** `src/scroll.c:330` initializes WIN1 to 240. Closing and opening paths cap its right edge at `DISPLAY_WIDTH` at `:369-370` and `:436-437`. Widescreen activation does not exclude this transition. The compositor tests the literal window boundary (`port/ppu/src/mode1.c:959-963`).

Inside control is `0x17`, while outside control is `7` (`src/scroll.c:328-329`): OBJ display is disabled outside. In a wide view, sprites in columns >=240 therefore fall outside this window during the effect and can disappear until normal rendering resumes.

Fix direction: define widescreen transition-window semantics or deliberately use native fallback for this effect. Simply replacing 240 with 384 is insufficient: the window endpoints are packed into eight-bit fields (`src/scroll.c:448`). Capture both entry and exit in a wide room before selecting the implementation.

### 4. P2 — Dampe's script still treats visible widescreen positions as offscreen

**Source-confirmed predicate/callers; gameplay reproduction outstanding.** `src/npc/dampe.c:119` checks screen x + 16 < 272, rejecting x >=256 regardless of the effective viewport width. The outside script waits for this predicate to become false before enabling Dampe (`data/scripts/graveyard/script_DampeOuside.inc:14-19`). The inside script also branches on it (`data/scripts/graveyard/script_DampeInside2.inc:9-12`). At widths 284/384, the predicate can allow a visibility change while Dampe remains in the revealed strip.

Fix direction: use effective viewport width while preserving the original margins and 240px behavior. Exercise the graveyard script's required flag state and both camera edges.

### 5. P2 — Positioned dialogue can use stale widescreen centering coordinates

**Source-confirmed conditional mismatch; affected dialogue not identified.** The widescreen rectangle reads `gMessage` (`port/port_linked_stubs.c:1355-1359`). Message initialization copies that request into the active renderer; position token 10 subsequently updates only `gTextRender.message.textWindowPosY` (`src/message.c:644-645`). Actual window placement uses the updated renderer state (`src/message.c:902-903`).

A message using that token can move outside the published centering band, leaving the actual box uncentered or subject to the HUD remap. Fix direction: publish the actual active rendered rectangle and verify a token-positioned message, including opening/closing animation.

### 6. P2 — Negative screen shake opens a seam at column 240

**Reproduced with a standalone CPU render.** The shadow starts at the first unshaken reveal tile (`port/port_linked_stubs.c:1263-1264`), while screen shake changes BG offsets (`src/scroll.c:975-979`). At camera phase zero with shake -1, column 240 needs the tile immediately preceding that base. The shadow lookup computes index 31, outside the 22 columns allocated at width 384, and substitutes tile zero (`port/ppu/src/mode1.c:508-511`).

The same temporary reproduction source includes a flat opaque background/shadow case with BGHOFS=-1 and shadow base 30. Fresh compilation produced:

```text
shake -1: x239=ff0000f8 x240=ff000000 x241=ff0000f8
```

Fix direction: provide left-side shadow coverage or a valid native-tile fallback for this boundary, preserving world alignment. Verify positive/negative shake across camera tile phases. This establishes the renderer failure under the engine-generated offset condition; no live shaking gameplay capture was performed.

## Lower-priority observations

- Console-Parity forces effective widescreen off (`port/port_runtime_config.cpp:981-990`), but the checkbox remains interactive and can toast “Widescreen enabled” (`port/port_imgui_display_tab.inc:298-302`). Indicate the override in the UI.
- `docs/widescreen-status-2026-05-30.md` describes reverted work and obsolete stretching/submodule behavior. `xmake.lua:99-102` also describes wide builds as stretched. Neither accurately describes the current runtime implementation.

## Verification and limits

The existing `build/pc/tmc_pc` was run in three separate temporary working directories with dummy SDL video/audio, `--no-audio`, and `TMC_ROOMCAP_PAN_PROBE=1`. Each run reached `Port layer initialized. Entering AgbMain...`, warped to room 03/08, and exited successfully after satisfying the camera script wait:

| Requested/effective width | PASS frame | Rest scroll x |
| --- | --- | --- |
| 240 | 590 | 1128 |
| 284 | 585 | 1106 |
| 384 | 572 | 1056 |

Logs: `/tmp/tmc-widescreen-investigation-smaybxjm/{240,284,384}/run.log`. These runs used an existing binary, not a rebuild of the dirty working tree; they establish that binary's camera behavior only. Config and save effects were isolated to temporary directories.

The CPU compositor reproduction was freshly compiled from the inspected source. No full game build, GPU execution, visual transition/dialogue/graveyard playthrough, or exhaustive room/region/backend matrix was performed. Camera follow/snap and script wait share `Port_Widescreen_CameraRestX`; central culling and several enemy-specific bounds already use widescreen-aware values. The passed pan probe is useful regression evidence, not an all-clear for widescreen.

Recommended next work: repair the compositor and shake seam with expected-output tests, enable the parity target's wide coverage, then reproduce and fix the three scene-specific issues above.

## Fixes and verification

Implemented on 2026-09-12:

- CPU and GPU composition preserve valid sprites/backdrop across column 240; rebuilt the committed Vulkan fragment shader.
- GPU parity builds inherit the configured framebuffer capacity and include an independent expected-pixel assertion for the original compositor bug.
- Shadow population includes a leading tile for negative shake without changing world alignment.
- Digging-cave irises temporarily use native width; the exit frame restores the wide camera and refreshes the tilemap. Native-width and disabled-widescreen behavior is preserved.
- Dampe's predicate uses the effective viewport with its original margins.
- Dialogue centering reads the window actually drawn into BG0, including animated size and token-driven position changes.
- The widescreen checkbox is disabled with an explanation during Console-Parity. Historical status/build comments are clarified.

Passed checks:

| Check | Result |
| --- | --- |
| `xmake build -y tmc_pc` | Build succeeds; existing compiler warnings remain |
| `python3 tools/widescreen_regression_test.py` | Expected sprite/backdrop/window pixels, native padding, and Dampe boundaries at compiled widths 240/384 pass |
| `python3 tools/widescreen_engine_regression_test.py` | All 16 scroll phases with shake -3..3 across the revealed strip, room-edge padding, iris width/camera, and drawn dialogue rectangles pass |
| `xmake build -y ppu_gpu_parity` and `build/pc/ppu_gpu_parity` | All 43 scenes at 384x160 match CPU byte-for-byte on Vulkan and GLES; widescreen scenes explicitly enabled |
| `tools/ppu_parity_check.sh` | Both committed native boot scenes retain their Tier A and Tier B golden hashes |
| Live camera pan at 240/284/384 | All reach `AgbMain` and satisfy the script wait |
| Top-positioned dialogue at 240/284/384 | All capture successfully; 384px capture visually checked for centered, intact frame |
| Live digging-cave entry and exit | Both remain 240px during action 5 and restore 384px after completing |
| Peer review and `git diff --check` | No actionable review findings or whitespace errors |

Runtime artifacts are temporary: `/tmp/tmc-widescreen-fixed-p9hwmv2j` contains pan logs and dialogue captures; `/tmp/tmc-iris-probe-qpred845` and `/tmp/tmc-iris-exit-probe-hwhkyjed` contain GDB scripts/logs/captures for natural transitions between Eastern Hills North (03/04) and its digging cave (13/00). The source-based regressions demonstrated failures before the fixes. Save/config effects were isolated to these temporary directories.

No complete game playthrough, region matrix, or Apple Metal runtime test was performed. This tree does not track the optional generated Metal PPU source; its existing CPU fallback remains in place. The fixed SPIR-V and shared GLSL paths were executed on this Linux host.
