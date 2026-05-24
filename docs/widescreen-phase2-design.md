# Widescreen Phase 2 — design notes

Status: NOT IMPLEMENTED — this is the design surface for a future
session. Phase 1 (256-wide, scroll-buffer reveal) is committed and
working; Phase 2 is the engine-side surgery for true 320+ widescreen.

## Current state (Phase 1)

- `MODE1_GBA_WIDTH` overridable at compile time via xmake
  `--widescreen_width=N`. Default 240. Spike value 256.
- `MODE1_GBA_BG_CLIP_X` follows `MODE1_GBA_WIDTH` up to 256, then
  caps at 256.
- Post-render stretch in `port/port_ppu.cpp` only fires for >256;
  at ≤256 the BG renderer fills the wider buffer natively.
- OAM viewport stays at 240 unconditionally — bumping it leaks the
  engine's parked off-screen sprites into the visible widescreen
  area.

## Why >256 doesn't "just work"

The original PPU comment ("real widescreen needs a 64-tile sa2-style
BGCNT_TXT512x256 extension") implies the BG buffers are 32-tile wide
and need to grow. Survey of the actual code shows the situation is
more layered:

### BG buffer layout (`include/vram.h`)

```
gBG0Buffer: u16[0x400]   (32×32 = 1024 entries)
gBG1Buffer: u16[0x400]
gBG2Buffer: u16[0x400]
gBG3Buffer: u16[0x800]   (32×64 = 2048 entries — NOT 64×32)
```

**gBG3 is taller, not wider.** Confirmed by `src/enemy/waterDrop.c:76`
which addresses the bottom half as `(y & 0x1fU) * 0x20 + 0x400`. The
extra 0x400 entries are a vertical pre-load buffer used during
vertical room transitions, not a wider BG.

### Tile rendering pipeline

1. `RenderMapLayerToSubTileMap(gMapDataBottomSpecial, &gMapBottom)`
   in `src/beanstalkSubtask.c:1126` renders the full room layer into
   `gMapDataBottomSpecial` (`u16[0x4000]` = 128KB intermediate).
   Uses **0x80 row stride** = 128 u16 = 64 subtiles wide. This is the
   intermediate's layout, NOT any BG buffer.

2. The "scroll modes" in `src/scroll.c` (`Scroll0`..`Scroll5`) copy a
   32-tile window from `gMapDataTopSpecial` / `gMapDataBottomSpecial`
   into `gBGnBuffer` based on camera position. Each mode handles a
   different room transition pattern (continuous scroll, screen-by-
   screen, vertical-only, fade-in, etc.).

3. DMA pushes `gBGnBuffer` → VRAM at the configured screen-base.

### Hardware-side BG screen-size encoding

- BGCNT bits 14–15: `00` = 256×256 (1 screen block), `01` = 512×256,
  `10` = 256×512, `11` = 512×512.
- TMC sets BG0–3 to `00` at `src/common.c:647`. Title screen at
  `src/title.c:251` already overrides BG2 to `512×256` (`| BGCNT_TXT512x256`),
  proving the PPU + hardware path handles it.
- 512×256 mode requires 2 screen blocks in VRAM (left + right). The
  EWRAM-side buffer needs to mirror this: u16[0x800] with the right
  block at offset 0x400.

## The actual work for Phase 2

### Step A: BG buffer horizontal expansion

For each BG layer that should be widescreen (BG1, BG2, BG3 — leave
BG0 as 32-tile so UI offsets stay stable):

1. Grow `gBGnBuffer` from `u16[0x400]` to `u16[0x800]` in
   `include/vram.h`. For gBG3, expand from 32×64 to a layout that
   covers both horizontal (64) and vertical (64) — i.e.
   `u16[0x1000]`. (Or accept losing the vertical pre-load buffer in
   exchange for horizontal — needs to be evaluated.)
2. Update every `MemClear`/`MemCopy` `sizeof(gBGnBuffer)` / hardcoded
   `0x800` to track the new size. ~30 sites already mapped:
   - `src/debug.c:29-30`
   - `src/cutscene.c:263`
   - `src/gameUtils.c:972-975`
   - `src/common.c:628`
   - `src/menu/figurineMenu.c:121-122,577-578,648-650`
   - `src/staffroll.c:117,134,201-203`
   - `src/subtask.c:109`
   - `src/demo.c:69-70`
   - `src/ui.c:181`
   - `src/fileselect.c:398,410-411,800`
3. Update BGCNT init at `src/common.c:647-653` to OR in
   `BGCNT_TXT512x256` for each widened layer.

### Step B: Scroll-mode widening

Each scroll mode currently copies a 32-tile window. Update to copy
64 tiles, with the right half going to gBGnBuffer offset 0x400:

- `Scroll0` (most common — single-screen rooms with camera tracking)
- `Scroll1` (cont'd scroll between rooms)
- `Scroll2` (transition with fade)
- `Scroll4` (vertical scroll)
- `Scroll5` (special — boss rooms?)

Each `ScrollNSubM` variant also needs updating. The 0x80 stride in
the intermediates already covers 64 tiles, so the SOURCE side is fine
— it's the destination-write loop that needs widening.

### Step C: OAM viewport extension

`MODE1_GBA_VIEWPORT_X` in `libs/ViruaPPU/include/cpu/mode1.h` must
track `MODE1_GBA_WIDTH` so sprites in the widescreen area render.
Catch: the engine "parks" off-screen sprites by setting their OAM x
to 240+. Bumping the viewport without changing the engine's parking
position makes those parked sprites visible.

Either:
- Patch the engine's "park sprite" code to use a position past the
  widescreen viewport (e.g. `MODE1_GBA_WIDTH + 16`)
- Add a port-side OAM filter that hides sprites flagged as
  off-screen-parked
- Detect parked sprites by a sentinel attribute and clip them in the
  PPU

### Step D: UI offset audit

`src/ui.c`, `src/message.c`, `src/fileselect.c` use ~30 hardcoded
offsets like `&gBG0Buffer[0x219]`. If BG0 stays at 32-tile width,
none of these need to change. If BG0 widens too (to support widescreen
HUDs), every offset needs to be re-derived for the new stride.

**Recommendation**: keep BG0 at 32-tile, widen only BG1/BG2/BG3.

### Step E: Camera bounds

`UpdateCamera` / equivalent currently limits camera scroll so the
32-tile window stays inside the room. With a 64-tile window, the
limit changes. The room can also become too small for the wider
view — needs runtime check + fallback (centre the room with bars).

### Step F: Cutscene / dialogue blackout

Cutscenes often draw text/effects assuming a 240-wide playfield.
Either let the wider area show through (game world visible behind
cutscene text) or render black side-bars during cutscenes.

## Verification approach

Each step needs visual verification before moving to the next.
Recommend the loop:

1. Make a single isolated change.
2. Build with `xmake build -y tmc_pc`.
3. Run `./build/pc/tmc_pc` and visit a representative area:
   - Hyrule Town (BG2 + BG1 dense)
   - Minish Woods (BG3 dense forest)
   - Temple of Droplets (BG3 dynamic water effects)
   - File select (BG0 UI-heavy)
   - Title screen (already uses BG2 512×256)
4. F9 bug-report capture for any regression.
5. If clean, commit; otherwise revert and try a different approach.

## Scope estimate

- Step A: 1–2 sessions of mechanical edits + memcpy audit
- Step B: 2–3 sessions per scroll mode (5 modes + sub-variants)
- Step C: 1 session
- Step D: 0 sessions if BG0 stays 32-tile
- Step E: 1 session
- Step F: 1 session

Total: ~10–15 focused sessions of engineering + verification.
