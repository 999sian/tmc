# Regional and widescreen bug investigation — 2026-09-13

## Changes

- **Festival doors:** `CheckRegionOnScreen` used the fixed 240px width even
  when the camera and renderer were wide. Exterior-door parents could leave
  visible doors unspawned. The PC check now uses the effective viewport width;
  original native bounds and the GBA build remain unchanged. Retail festival
  door property records were checked and are identical in USA, EU, and JP.
- **Minish path foliage:** per-frame updates applied byte offsets to `u16*`,
  doubling both the scrolling page stride and the second layer's base offset.
  PC updates now use byte arithmetic, matching initialization. This is shared
  by all regions; it is not an EU-only asset change.
- **Woods fog/light rays:** BG3's repeating screenblock had no widescreen
  shadow and stopped at x=240. Publish the complete 32-column screenblock,
  retaining its native scroll, HBlank wave offsets, blending and priority.
  CPU and GPU use the same existing shadow interface. Other BG3 canvases
  retain their previous behavior.
- **Rolling transitions:** their VRAM update routines carry only 240 pixels
  while room maps already describe the destination. Keep scroll actions 2/4
  native, position the departure camera before replacing room bounds, and
  restore the wide camera with a full tilemap refresh at completion. These
  effects intentionally narrow temporarily; this does not implement seamless
  full-width two-room compositing. The existing action-5 iris fallback remains.

## Verification

- `xmake build -y tmc_pc`: passed, with existing compiler warnings.
- `python3 tools/door_visibility_regression_test.py`: door spawn boundary
  regression failed against the old check, passed after correction.
- `python3 tools/minish_foliage_regression_test.py`: passed USA/EU/JP;
  checks initial selection, scrolling, clamping, unchanged frames and re-entry.
  The old update failed the initial second-layer offset assertion.
- `python3 tools/widescreen_regression_test.py`: expected CPU pixels pass,
  including repeating fog across horizontal offsets and native pitch padding.
  The old partial shadow failed the fog pixel expectation.
- `python3 tools/widescreen_engine_regression_test.py`: passed shadow copying,
  shake, transition gates, camera and message rectangle checks.
- `python3 tools/widescreen_scroll_regression_test.py`: passed production
  transition entry/completion checks; old source failed entry positioning.
- `xmake build -y ppu_gpu_parity` and `build/pc/ppu_gpu_parity`: all 44 scenes
  byte-exact on Vulkan and GLES, including the new `ws_fog_wrap` scene.
- `tools/ppu_parity_check.sh`: both committed native boot goldens unchanged.
- USA/EU/JP festival runtime checks: right-hand child door spawned at
  (408,323), draw enabled, with valid regional sprite and VRAM allocation.
  USA/EU before-fix checks contained only the invisible parent.
- Actual USA-ROM north/east adjacent-room probes: native intermediate frames,
  wide completion, correct final camera and full tilemap refresh. These invoke
  real production routines with loaded room data, rather than player input.
- USA/EU/JP headless room captures reached `AgbMain` and exited successfully.
  Woods fog captures checked at 284px (USA) and 384px (EU/JP). A USA 384px
  comparison with the new overlay disabled in GDB changes exactly x=240..383;
  the original 240 pixels are identical.
- Independent diff review and `git diff --check`: no actionable findings.

## Follow-up: green ChuChus in Minish Woods

The user's room/color clarification led to a full-loop reproduction. Natural
Woods type-0 ChuChus remained hidden for 400 frames with Link eight pixels
from the selected enemy. Its home range was `1,64` instead of the room's
`18,14`: the horizontal activation area was only eight pixels wide and nowhere
near the enemy's spawn point.

`RegisterRoomEntity` writes spawn parameters through `GE_FIELD`, accounting
for the widened PC `Enemy.child` pointer. `sub_0804A720` instead cast the extra
area to `GenericEntityData`, reading the preceding enemy ID/type bytes as
rangeX/rangeY. It now reads typed `Enemy` fields, preserving original GBA
layout semantics. This fixes the shared initializer rather than changing AI.

`tools/enemy_home_regression_test.py` compiles the production initializer for
USA/EU/JP and checks explicit room bounds, species defaults and repeated
initialization. The old initializer failed the range assertion; fixed checks
pass. `tools/tests/chuchu_natural_runtime.gdb` observes normal top-level updates
of a real room-spawned ChuChu without forcing enemy actions or timers. It
supplies the normal progression flags needed for room enemies to load.

The natural-room regression failed on the old USA binary (only actions 0/1).
The rebuilt USA/EU/JP runs all pass: the selected Woods ChuChu has ranges
18/14 and progresses through emergence, movement, attack and recovery using
ordinary engine updates. Build and independent layout review passed.
Logs: `/tmp/chuchu_natural_before.log` and
`/tmp/chuchu_natural_after_{usa,eu,jp}.log`.

The earlier isolated state-machine probe bypassed room-spawn parameters, so it
could not catch this bug. Its color labels were also corrected to numeric types:
the Woods green ChuChu is type 0. This supersedes the earlier conclusion that
no source defect had been reproduced.

No complete playthrough, natural-input transition video, Metal execution,
or exhaustive scale/backend/room matrix was performed.

Temporary evidence: `/tmp/tmc-fog-regions-sz2kzqmx`,
`/tmp/tmc-woods-before-pu5hwevc`, `/tmp/ws-scroll-runtime.log`,
`/tmp/ws-scroll-horizontal.log`, `/tmp/door-{USA,EU,JP}`, and
`/tmp/chuchu_current_{usa,eu,jp}.log`.
