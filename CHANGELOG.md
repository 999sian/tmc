# Changelog

## 0.1.4-experimental — 2026-05-03

Hyrule Town + South Hyrule field playthrough fixes, driven by a live GDB-under-the-game session against the issue tracker. Three crashes and one stuck-state bug closed; CI build stability improvements; matheo upstream merged.

### Fixed (issue tracker)

- **#16 Hyrule Town kinstone-bag crash chain.** Five layered bugs all on the kinstone-fusion path that compound after picking up the bag: (1) `gUnk_08001A7C` and `gUnk_08001DCC` are packed 4-byte GBA-pointer tables but were declared as `T*[]` so `arr[idx]` read 8 bytes on x86-64 → garbage pointer → SIGSEGV the moment a kinstone fuser scripted up; (2) `(u8*)(fuserProgress + (u32)fuserData)` truncated a 64-bit pointer in `common.c::GetFusionToOffer`; (3) `TextDispEnquiry`'s post-A_BUTTON modulo divided by zero on x86 (ARM silently returns garbage) after MemClear zeroed `gMessageChoices.choiceCount`; (4) `kinstoneMenu.c::sub_080A4418` wrote to `DMA3->sourceAddress` (raw GBA hardware register address 0x040000D4, unmapped on PC); (5) `KinstoneMenu_080A4468` dereferenced `gPossibleInteraction.currentObject` without checking it pointed inside `candidates[]`. Fixed each + added `Port_PackedRomEntry()` helper for the ~5 packed-pointer-table call sites. (commit `b825b5fd`)
- **#19 South Hyrule field loading-zone crash.** Two bugs on the spear moblin: (a) `gUnk_080CC944` was the same packed-pointer-table pattern as #16, handing out garbage Hitbox pointers on x86-64; (b) `definition->ptr.hitbox` lives in read-only mmap'd ROM (gRomData), but the spear moblin's action routines write `hitbox->offset_x/y/width/height` every frame. Allocate a mutable `Hitbox3D` copy at init (`AllocMutableHitbox()` would `zFree` the const ROM pointer, so do it manually). (commit `c749c609`)
- **#20 Peahat (and other gust-jar-killable enemies) corpse never despawns.** `GetNextFunction()` returned 5 (OnGrabbed) for any entity with `gustJarState & 4` set, before the `health == 0` check. When a peahat is killed by gust jar, `Peahat_OnGrabbed_Subaction5` sets `health=0` but `gj=0x04` stays set until ENT_COLLIDE toggles, which doesn't happen in that subaction's else branch. Result: dead-while-grabbed enemies loop in OnGrabbed forever — corpse stays, no death animation. PC port now prefers the `health==0` dispatch so dead-while-grabbed enemies flow into GenericDeath → DEATH_FX → DeleteThisEntity. (commit `c708678a`)

### Build / CI

- **Linux runner pinned to `ubuntu-22.04`** (glibc 2.35) so the prebuilt tarball runs on Nobara 43, Fedora 43, Kubuntu 25.10, and any other distro that lags behind the Arch host's glibc 2.43. Addresses #2's class plus #17. (commits `91b5c9fd`, `a5f546da`)
- **Pre-generated USA `map_offsets.h` + `gfx_offsets.h` committed** under a `.gitignore` exception so CI can build without needing a private ROM repository. Drops `python build.py` from the workflow in favour of direct `xmake build tmc_pc` + `xmake build asset_extractor`, which don't need a ROM at compile time. (commit `fb36e928`)
- **Merged matheo/master twice today**: ubuntu fmt12 build fix (#15 root cause); update-check infrastructure (`port/port_update_check.{c,h}`); Minish Woods Ezlo fight + lilypad rail data fallbacks; bootstrap-based asset management (`port/port_asset_bootstrap.cpp`); affineSet cleanup. (commits `b5cf8067`, `d37edeb3`)

### Implicit fixes confirmed

- **#23 Broken map (grey tiles)** — works locally on the post-merge branch; one of the struct-alignment / BG-flush fixes resolved it. Awaiting Proton/Bazzite retest on this tarball.

### Known issues (still open)

- **#4 Pulled mushroom**, **#5 backflip at walls**, **#8 blue/red teleport icons**, **#9 Steam Deck packages**, **#11 boss asset rendering** (deferred to next sprite-extraction pass).
- **#21 Link's house glitched doors** — likely same class as the door-priority issue carried from 0.1.2.
- **#22 BGM doesn't mute on item-get** — audio priority/ducking, same area as the festival-house BGM-reset fix in 0.1.2 but with a different interaction.
- **#24 Tall-grass shoes overlay** — known carry-forward.
- **Inside-the-rolling-barrel scene**, **festival house facades**, **Minish Woods fog**, **mosaic effect** — renderer-iteration deferred from 0.1.2.

## 0.1.3-experimental — 2026-05-03

Bug-fix pass on top of 0.1.2 driven by the GitHub issue tracker — Deepwood Shrine playthrough now reaches the boss-clear warp, and a class of x86-64 struct-alignment bugs that was silently breaking ~30 entity subclasses is fixed at the source.

### Fixed (issue tracker)

- **#2 Linux white-screen on launch.** Asset loader fell back to `/proc/self/exe` to locate `assets/`, but Kubuntu users running through a custom `ld-linux` interpreter saw the loader path instead of the binary's directory. Now also probes the current working directory, and the missing-asset message points at `./asset_extractor` instead of "ROM fallback disabled". (`port/port_asset_loader.cpp`, `src/common.c`, commit `dc31679e`)
- **#3 Minish Village entrance vegetation missing.** Gfx group 30 has destinations in EWRAM (`gMapDataTopSpecial` at `0x02002F00`); the asset loader was writing those through the wrong resolver and the foliage tilemap stayed zeroed. Now routed through `Port_ResolveEwramPtr` like other heap-allocated game variables. (`port/port_asset_loader.cpp`, commit `0f728182`)
- **#6 Deepwood Shrine barrel doors render as flat colour bands** + **gust-jar barrel soft-lock.** Two unrelated bugs in the same room. (a) HBlank-DMA wasn't honouring `DEST_FIXED` vs `DEST_INC` vs `DEST_RELOAD` modes, so the cylindrical barrel-stave affine warp showed as solid bands. (b) The middle-hatch fall-through was gated on a barrel-rotation angle window the port never reached (max 0xF0 < required 0x118 because the port stops simulating once you're aligned); PC build now bypasses the angle gate. (`port/port_hdma.c`, `src/manager/rollingBarrelManager.c`, commits `107e7451`, `cd99dd4d`)
- **#7 "Mysterious Shells" textbox showed 0 obtained.** Number-variable substitution in textboxes wasn't being initialised on the port — `gUnk_08107BE0[1]` (the variable-slot pointer table) needed to point inside `gTextRender` so the code that copies the rupee count into the message buffer would land in the right place. Also fixed a `DecToHex` BCD-encoder regression that relied on GBA Div SWI's r1 remainder side effect; replaced with plain divmod. (`src/message.c`, `src/common.c`, commit `580ff28c`)
- **#10 Drop shadows missing.** `sShadowFrameTable` was never populated because the GBA original loads it via the IWRAM overlay copy (`sub_080B197C..RAMFUNCS_END`) that the PC port deliberately skips. Now loaded directly from ROM at first use, with IWRAM↔ROM offset translation for each region. Also fixed a draw-order bug where shadows rendered above their owning sprite. (`port/port_draw.c`, commits `4a191f01`, `cd99dd4d`)
- **#12 Boss reward chain (heart container + green warp) didn't spawn after defeating the Deepwood Shrine boss.** Three layered fixes:
  - `gUnk_additional_a_DeepwoodShrineBoss_Main` (the post-defeat entity list) was a zeroed 64-byte stub in `port_linked_stubs.c` because the asset extractor doesn't index it. Now populated from ROM 0x0DF94C in `Port_InitDataStubs`. (commit `cd99dd4d`)
  - **Root cause** (the data fix alone wasn't enough): GenericEntity has a `void*`-aligned `scriptContext` union that pushes `cutsceneBeh`/`field_0x86` to PC offset 0xB0/0xB2, but most entity subclass structs (`WarpPointEntity`, `HeartContainerEntity`, `GentariCurtainEntity`, `bossDoorEntity`, `lockedDoorEntity`, ~25 more) lay out a `flag` field at GBA offset 0x84/0x86 *without* that void* trick, landing at PC 0xAE. `RegisterRoomEntity` was writing `spritePtr` halves via `GE_FIELD` which always lands at 0xB0/0xB2, so subclass reads got junk — warps never armed and heart containers stayed in their hidden first-action state forever. Fix mirrors the writes to PC 0xAC/0xAE for all non-Enemy kinds, catching every affected subclass at once. (`src/room.c`, commit `bfd80ec6`)
- **#14 Minish Village elder (Gentari) wouldn't open the curtain after the first dungeon.** Same root cause as #12 — `GentariCurtainEntity::flags` at GBA 0x86 lands at PC 0xAE, was reading junk so the curtain animated as already-open or not at all. Universal struct-alignment fix from #12 covers it. (`src/room.c`, commit `bfd80ec6`)

### Other fixes carried since 0.1.2

- **Doorway crash on `HouseDoorExterior_Type3`** — guard against NULL script context when the spawner failed to resolve. (`src/object/houseDoorExterior.c`, commit `6bc17ac2`)
- **Map-hint cutscene black BG** — `sub_0807C4F8` now iterates native 24-byte `MapDataDefinition` structs from the asset loader; `RestoreGameTask` force-flushes the BG buffer→VRAM copy. (`src/playerUtils.c`, `src/gameUtils.c`, commits `01948f13`, `86f1463a`)

### Known issues (still open)

- **#4 Pulled mushroom (B1) renders as a flat tan rectangle while held.** Sprite-frame extraction edge case — `gSpriteAnimations_PullableMushroom` is one of ~900 animations the extractor flags as `Animation loop byte missing` / `Animation data has trailing bytes`. Held state falls back to zeroed frame data. The unmounted mushroom renders correctly. Tracked under the broader extractor fix.
- **#5 Backflip / vault triggers off solid walls in Deepwood Shrine.** Likely a `gMapTileTypeToActTile` mismatch — a wall tile is being mapped to a vaultable act-tile (43, 44, 65, 66, 76-79). Needs world coordinates / tile type to pin down.
- **#8 Blue/red teleport-icon parallax sprite missing.** PARALLAX_ROOM_VIEW spawns but its sprite frame may be in the same extraction-loss bucket as #4. Worth retesting with this release's struct-alignment fix.
- **#11 Boss texture renders incorrectly during the fight.** Likely also in the animation-extraction loss bucket; struct fix may help indirectly. Retest requested in 0.1.3.
- **#13 Re-entering the boss room after defeat breaks textures.** Per-room state (tilemap / palette / gfx group) may not be fully refreshed on re-entry. Needs a fresh repro on 0.1.3.
- **#15 Fedora 43: `libfmt.so.12: cannot open shared object`** when running the prebuilt `asset_extractor`. The xmake config sets `header_only = true` for fmt but the linker still pulls the host's `libfmt.so.12` SONAME. Workaround: install fmt 12 from upstream, or build from source with `python3 build.py --usa`. Build-side fix coming in 0.1.4.
- **Inside-the-rolling-barrel scene** still renders as flat brown bands — the `BG2PA` per-scanline DMA driving the cylindrical roll isn't honoured by VirtuaPPU even with the dest-mode fix above. Visible only inside the barrel; the room is otherwise playable.
- **Festival house facades, doorway sprite glitches, map-screen grey patches, cloud-shadow lines, Minish Woods fog, tall-grass shoes overlay** — renderer-iteration deferred from 0.1.2.
- **~900 animation entries** still skipped during extraction (`Animation loop byte missing` / `Animation data has trailing bytes`). Affects boss frames, Vaati cutscene, MazaalHand, mushroom carry pose, possibly the teleport icons.
- **Mosaic effect** on title fade and certain spell charges still kill-switched.

## 0.1.2-experimental — 2026-05-02

Bug-fix pass over a USA playthrough from prologue through Hyrule Field, plus a hard-found extraction bug that was silently dropping whole asset subtrees from release tarballs.

### Fixed

- **Doors in Hyrule town & overworld no longer make the game crash on entry.** `HouseDoorExterior_Type3` (the fully-scripted door variant) called `ExecuteScript(super, this->context)` even when the spawner failed to set `context` (port script resolution can return NULL where the GBA original always resolved). Reproduced entering `HyruleField/LonLonRanch`. Skip the script invocation under PC_PORT instead of segfaulting. (`src/object/houseDoorExterior.c`)
- **BG no longer goes black after a map-hint cutscene.** After the Mountain Minish elder shows the world map and the dialog continues, the room behind it was rendering pure black instead of returning to the room art. Two issues in `Subtask_FadeOut → RestoreGameTask → sub_0801AE44`: (a) `sub_0807C4F8` couldn't iterate native 24-byte `MapDataDefinition` structs from the asset loader (only handled ROM-packed 12-byte entries), so `gMapDataBottomSpecial` stayed zeroed; (b) even when the BG buffer got refilled correctly, no one raised `gScreen.bg.updated` so the buffer→VRAM copy never fired and VRAM kept the stale map tilemap. (`src/playerUtils.c`, `src/gameUtils.c`)
- **Lily pads in Minish Forest move again.** `data_080D5360/` was missing from extracted assets, so the lily-pad rail data couldn't be loaded and the pads sat still. See "Asset extraction" below for the underlying fix.
- **Festival house BGM stops resetting on every room transition.** `Port_M4A_Backend_StartSongById` was unconditionally restarting the same song each time `SoundReq` fired with the room's queued BGM. Now skipped when the same BGM (songId 1..99) is already playing on the same player. SFX still re-trigger correctly. (`port/port_m4a_backend.cpp`)
- **Hyrule Field / Castle Garden plays the correct prologue BGM.** USA region was using `BGM_FESTIVAL_APPROACH` for the first prologue scenes; matched the EU mapping (`BGM_BEANSTALK`) on the port so the music matches. (`src/roomInit.c`)
- **Stray location-name textbox no longer appears during Zelda's intro call.** `EnterRoomTextboxManager` was outliving an unrelated message and reasserting itself. Restored the GBA-original kill condition `(gMessage.state & MESSAGE_ACTIVE)` so the textbox dies when another message starts. (`src/manager/enterRoomTextboxManager.c`)
- **Magic-stump exit animation grows from small → big** (PC-port deviation from the canonical giant→normal animation, requested by user). Player entity's `unk_80` / `unk_84` now start at `0x80` and increment to `0x100`, matching the OOT-style growing-Link feel. (`src/player.c`)

### Asset extraction

- **`data_*/` subtrees no longer drop from release tarballs.** Two pipeline gaps caused fresh extractions to silently miss whole directories (most user-visibly `data_080D5360/`, where lily-pad rails and door data live):
  - `extract_area_tables` skipped property indices 4..7 unconditionally — they're usually room callbacks but some rooms put data pointers there. Now follows the offset when it matches an indexed asset entry, and a final sweep extracts every `EmbeddedAssetIndex` entry the specialized passes missed (~7300 additional files). (`tools/src/assets_extractor/assets_extractor.hpp`)
  - `CopyRuntimePassthroughAssets` had a fixed directory whitelist that excluded all `data_<addr>/` subtrees. Now lists them explicitly so they survive the `assets_src/ → assets/` runtime build. (`port/port_asset_pipeline.cpp`)
- `asset_processor` now creates `assets/` before scanning JSON configs, so a fresh checkout doesn't crash in extract mode. (`tools/src/asset_processor/main.cpp`)

### Known issues (still open)

- **Inside-the-rolling-barrel scene (Deepwood Shrine) renders as flat brown bands** instead of the textured barrel-stave + window view. Per-scanline HBlank-DMA on `REG_ADDR_BG2PA` (the affine matrix that creates the cylindrical roll) isn't being applied by VirtuaPPU. The same root cause likely affects light-ray/parallax effects (`vaatiAppearingManager`, `steamOverlayManager`, `lightRayManager`, `pauseMenuScreen6`) and the iris/circle window effects (`common.c` ×4). Visible: cosmetic; the room is otherwise playable.
- **Festival house facades, doorway sprite glitches, map-screen grey patches, cloud-shadow line artifacts, Minish Woods fog, tall-grass shoes overlay** — all renderer-iteration heavy and deferred until they can be debugged with eyes-on-screen.
- **`asset_extractor` warnings about "Animation loop byte missing" / "Animation data has trailing bytes"** still affect ~900 animations (Gyorg, Vaati cutscene, MazaalHand, etc.). Not used in the title or early gameplay; surfaces during specific cutscenes. Same root cause as 0.1.1.
- **Mosaic effect on title fade and certain spell charges is disabled** (kill-switched). Patch needs re-porting against current ViruaPPU `mode1.c`. Same as 0.1.1.

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
