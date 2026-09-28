# TODO / FIXME / HACK triage — port/ and src/

Scope: whole-word search across `port/` and `src/` (root checkout only; `.worktrees/` excluded). ~170 tagged lines: 3 in `port/`, ~167 in `src/`. No `HACK`; the only `FIXME` is `src/object/figurineDevice.c:183`. ~52 lines are the repeated `// TODO sprite index too high`.

Summary: Almost all tags are upstream decomp notes (register allocation, struct recovery, missing sprite enum names). Only 3 tags mark real port bugs (all in the GLSL preset runtime). Two additional bugs were surfaced by tracing TODOs: `worldEvent2.c:57` and the GLSL feedback resize.

---

## Top Real Bugs (Ranked)

1. **`src/worldEvent/worldEvent2.c:57` — Regional table read bypasses resolver**
   - **Bug:** Directly indexes USA `gUnk_080FEAC8[gRoomTransition.transitioningOut]` to extract the layer bit `(_6 & 1)`. On EU and JP ROMs, that table sits at a different address. `src/kinstone.c:521` resolves it via `Port_ResolveRegionData(REGIONAL_DATA_UNK_080FEAC8)`, but `worldEvent2.c` was missed.
   - **Effect:** Sub-pixel layer bit corrupted during screen transitions on non-USA builds.
   - **Fix:** Route through `Port_ResolveRegionData(REGIONAL_DATA_UNK_080FEAC8)`.

2. **`port/port_glslp_runtime.cpp:564-587` — Feedback pass texture size desync after window resize**
   - **Bug:** `GlslpRuntime::Resize` updates `pass.texture` for the new viewport, but does not recreate `pass.prev_texture`. At `:862-865`, `prev_texture` is swapped in.
   - **Effect:** After a resize, multi-pass shaders with feedback use a stale-sized feedback texture.

3. **`port/port_glslp_runtime.cpp:265-271` — LUT mipmap flag ignored**
   - **Bug:** `parser.cpp:315` parses `_mipmap` on LUT textures, but the texture loader always sets `SDL_TEXTURE_MIPMAP_NONE`.
   - **Effect:** Shader presets expecting filtered downscaled LUT textures get point/linear without mip levels.

4. **`port/port_glslp_runtime.cpp:384-390, 577-579` — Framebuffer format flags ignored**
   - **Bug:** `float_framebuffer` and `srgb_framebuffer` are parsed from presets (`parser.cpp:266-267`) but the runtime hardcodes `SDL_PIXELFORMAT_RGBA8888`.
   - **Effect:** HDR/wide-gamut or linear-light shaders band or clamp early.

5. **`port/port_debug_menu.cpp:766-768` — Stale mode-7 comment & inverted affine scaling label**
   - **Bug:** `// TODO: Mode 7 needs separate sub-pixel scaling`. Mode 7 was removed; the GPU path already implements sub-pixel affine BGs, while the software renderer does not.

6. **`src/object/figurineDevice.c:183` — `FIXME: 23` (Decomp constant recovery)**
   - **Bug:** Magic number in figurine drop-rate table index calculation. Harmless on PC, but unverified against retail ROM math.

---

## Obsolete Comments (Code Already Handles Them)

- `port/port_discord_rpc.h:19` — Windows named pipe TODO; implemented in `port_discord_rpc.c:71, 163-180`.
- `port/port_glslp_runtime.cpp:54` — Resize TODO; handled in `Resize()` (:564).
- `src/cutscene.c:26` — Interrupt handling; GBA-only `#else` branch; PC uses SDL timer ticks.
- `src/object/titleScreenObject.c:13` — Palette load; handled by asset loader.
- `src/title.c:85` — Title task GBA BIOS call; emulated in `port_bios.c`.
- `src/object/itemForSale.c:45` — Shop item slot indexing; resolved in PC save layer.
- `src/beanstalkSubtask.c:28` — Beanstalk scroll logic; PC PPU handles sub-pixel scrolling.

---

## Decomp Notes (Keep Upstream Alignment)

- ~52 instances of `// TODO sprite index too high` in `src/enemy.c`, `src/projectile.c`, etc. (Upstream tracking for sprite IDs exceeding 8-bit fields).
- ~30 instances of `// TODO: field names / struct alignment` in `src/gameData.c`, `src/roomInit.c`.
- DEMO_JP differences noted in `src/gameData.c:169, 258, 260, 388`.
