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

## Scroll-pipeline deep-dive (session 2 findings)

The actual flow from world tilemap to BG buffer to VRAM:

```
gMapBottom / gMapTop (per-area MapLayer)
        │
        ▼  RenderMapLayerToSubTileMap (beanstalk:1126)
        │  loader writes 128 u16 stride
        ▼
gMapDataBottomSpecial / gMapDataTopSpecial (u16[0x4000], 128KB)
        │
        ▼  UpdateScrollVram (port_linked_stubs.c:883), dispatched by gUpdateVisibleTiles
        │
        ├─ mode 1: ram_sub_080B197C_c (initial full fill, 23×32)
        ├─ mode 2: sub_0807D280 (screenTileMap.c:5)
        ├─ mode 3: sub_0807D46C (screenTileMap.c:85)
        └─ mode 4: sub_0807D6D8 (screenTileMap.c:217)
        │
        ▼  Each writes to gBG1Buffer + 0x20 / gBG2Buffer + 0x20
        │  (the +0x20 skips one row used as pre-load margin)
        ▼
gBG1Buffer / gBG2Buffer (u16[0x400], 32×32 currently)
        │
        ▼  Interrupt DMA (interrupts.c:210-212)
        │  DmaCopy32(3, &gBGnBuffer, VRAM + (control & 0x1f00) * 8, 0x5C0)
        │  0x5C0 = 1472 bytes = 23 rows × 32 tiles × 2 bytes
        ▼
VRAM (BG screen block at gScreen.bgN.control's screen_base)
```

Each scroll mode has up to 5 cases for `scroll_direction` (0-4),
each with its own DmaSet pattern. Sample magic constants in
sub_0807D280:

| Constant | Meaning | Phase-2 value (for 64-wide) |
|---|---|---|
| `0x20` | row stride in BG buffer (u16) | `0x40` |
| `0x80` | row stride in mapSpecial (u16) | `0x100` (no change — already wide) |
| `0x1e` | last-col offset within row | `0x3e` |
| `0x80000020` | DmaSet word count: copy 32 u16 | `0x80000040` |
| `0x800003c0` | DmaSet word count: copy 960 u16 (30 rows × 32) | `0x80000780` (60 rows × 32 = 1920) |
| `0x280` | offset into BG buffer | needs re-derivation |
| `0x2a0` | offset = 0x280 + 0x20 | `0x500 + 0x40` |

Multiplied across 3 scroll-mode functions × ~5 cases each = ~15
distinct DmaSet/memcpy invocations to update, plus 4 different buffer-
offset arithmetic expressions per case. Each error causes wrong-
columns-scroll rather than a clean crash, so detection requires
visual verification of every transition type:

- Single-screen room → loop in place (no scroll updates beyond
  initial fill)
- Multi-screen horizontal scroll (Hyrule Town → Lon Lon Ranch)
- Multi-screen vertical scroll
- Dungeon room-to-room transitions (fade-through patterns)
- Special triggers: bean travel, minish-cap shrink, falls

Each combination is its own test.

## Recommended phase-2 sequence (revised)

1. **Don't widen buffers yet**: keep gBGnBuffer at u16[0x400].
   Instead, do a one-line spike — bump BGCNT[BG2] to 512×256 only —
   and verify the PPU wraps the existing 32-tile buffer cleanly with
   no DMA changes. This isolates "did PPU survive the BGCNT change"
   from "did the buffer logic survive".
2. Spike the doubled buffer for ONE layer (BG2 only) with the
   simplest scroll mode (initial fill = mode 1 = `ram_sub_080B197C_c`).
   Don't touch modes 2/3/4 yet — accept stale right-half tiles after
   first scroll.
3. Add modes 2/3/4 ONE AT A TIME, with screenshot verification per
   transition type.
4. Generalise to BG1 once BG2 is stable.
5. Leave BG3 as the last (it's currently the vertical-pre-load
   buffer — its layout change is independent of horizontal widening).
6. Final pass: OAM viewport, UI offsets, camera bounds, cutscene
   blackouts.

Each numbered item is ~1 session. Total: 8-12 sessions for the
engine surgery + a final integration session. This matches the
earlier 10-15-session estimate but with concrete checkpoints.
