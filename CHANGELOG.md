# Changelog

## 0.2.0-experimental — 2026-05-06

Two-day bug-fix and tooling pass on top of 0.1.6.x. Six tracker issues closed (cucco round 9, figurine minigame, Deepwood barrels, max-hearts, Cave of Flames boss round 3, Link's house warp); F8 internal-render-scale page + sub-pixel OAM affine added; bug-report capture upgraded to PNG with auto-on-crash trigger and raw-IP/maps emit before unsafe calls; F8 "All areas (raw, by index)" warp submenu so any room is one keystroke away.

### Fixed (issue tracker)

- **#46 Hyrule Town: cucco minigame round-9 SEGV.** Levels 8 and 9 only define 2-3 cuccos; the remaining heap slots are NULL. `sub_080A1270` evaluated `pEnt->next != NULL && pEnt != NULL` — wrong order, dereferences `pEnt->next` before the NULL guard fires. Harmless on GBA (NULL reads return BIOS data) but SEGVs on the PC port at the start of round 9. Swap the operand order so short-circuit evaluation actually gates the deref. (commit `868159bd`)
- **#51 Cave of Flames: boss round-transition hang + death-path SEGV.** Two-part Gleerok fix. (a) The round-3 transition stuck because `Gleerok_KnockBack` ran the round-bump path even after death; gate on `super->action != ACTION_DEATH` to keep the death sequence linear. (b) `sub_0802E7E4` deref'd `heap->entity1` unconditionally on death; the heap entry was NULL on PC where GBA hardware silently returned BIOS data. Added the same NULL guard pattern as the rest of the gleerok work. (commit `2c9f0a60`)
- **#52 Debug menu: max-hearts sets 20, not 10.** `Port_DebugAction_MaxHearts` set `maxHealth = 80` (10 hearts in eighths) where the player's actual cap is `0xA0` (20 hearts). The previous value matched the fileselect/UI's silent 10-cap special-case. Fixed to `0xA0`. (commit `1045174f`)
- **#57 Figurine viewer: SEGV the moment you owned a figurine + glitched sprite.** Three layered bugs. (a) `gFigurines` was a 512-byte zeroed stub in `port_linked_stubs.c`; the menu deref'd `fig->pal` / `fig->gfx` and crashed. New `port/port_figurines.c` defines a real `Figurine[137]`, populated from a compile-time table of 136 GBA ROM addresses + sizes resolved via `Port_ResolveRomData` after `gRomData` is mapped (same pattern as `gPalette_549` / `gSpritePtrs`). (commit `0114e8bf`) (b) The first table version stored ROM-relative offsets (`0x005B5EC0`) but `Port_ResolveRomData` requires `>= 0x08000000`; every entry resolved to NULL, `port_DmaTransfer` early-returned on `!src`, and the viewer rendered with stale palette/VRAM ("glitched"). OR-in `0x08000000` when calling. (c) `sub_080A4CBC` deref'd `(u16*)0x02021f72 + 0x80` — a hardcoded GBA EWRAM scan address — bypassing the `ShowTextBox` write path's `Port_ResolveEwramPtr` routing. Resolve through `Port_ResolveEwramPtr` inside the PC_PORT guard before the scan. (commit `1599a004`)
- **#61 Deepwood barrels invisible on Windows.** `Port_LoadGfxGroupFromAssets` and `Port_LoadPaletteGroupFromAssets` ran heap-allocated `std::vector::data()` source pointers through `port_resolve_addr`. On Windows MinGW, malloc occasionally hands out addresses inside `[0x02000000, 0x0A000000)` which the resolver remaps to `gEwram[]` — the gfx/palette never reaches its destination. Linux glibc never allocates in that window so the bug never fires there. Resolve the destination ourselves in both loaders and use plain `std::memcpy`; source pointer never touches the resolver. (commit `8f15a9e3`)
- **#65 Debug menu: Link's house warp lands a mile off.** Entry warped to area 3 room 0 (Western_Woods_South) instead of room 1 (SOUTH_HYRULE_FIELD). Updated the coords to match `gExitList_HouseInteriors2_LinksHouseEntrance` (`0x290, 0x19c, layer 1`). (commit `89a575a1`)

### Fixed (other)

- **Ocarina-of-Wind / windcrest fast-travel SIGSEGV.** `Subtask_FastTravel_Functions` was an unpopulated function-pointer stub. Filled in the table from the decompiled fast-travel state machine. (commit `bb962804`)
- **#50 asset extractor missing sounds.json.** `sounds.json` wasn't being embedded into the extractor binary (the v0.1.6 packaging bug). Added it via xmake's `bin2c` rule. (commit `8dfb6a72`)

### Tooling / new features

- **F8 → Display settings page + internal-render-scale.** New display submenu lets you switch render scale (1x..4x) at runtime. Includes a sub-pixel OAM affine overlay path so rotated sprites (Vaati's tornado, screen-shrink cinematic, every spinning enemy) sample at the upscaled grid instead of nearest-neighbouring the 240-pixel staircase. (commit `dcde8415`)
- **F9 bug-report capture: PNG output + auto-capture on crash.** Replaces the BMP screenshot with PNG. SIGSEGV/SIGABRT/SIGBUS handlers (POSIX) and `SetUnhandledExceptionFilter` (Windows) snapshot the same bug-report directory automatically with `reason=crash:<sig>@<addr> ip=<rip>` so post-mortem reports include the fault site even if the user couldn't press F9. The handler now emits raw IPs and `/proc/self/maps` *before* any unsafe call so the report survives even when the crash came from the handler's own stack. (commits `cb4cc0ca`, `8fa93717`)
- **F8 → "All areas (raw, by index)" warp submenu.** Iterates every area slot with at least one mapped room, opens a per-area room list. Pages scroll if the list overflows. Spawn coord is geometric centre — re-warp if you land in a wall. (commit `628b8c49`)
- **F8 → ITEMS → "999 mysterious shells".** Companion to the existing 999-rupees / max-hearts / all-kinstones actions; needed for figurine-minigame repros. (commit `1599a004`)
- **F8 → WARP → repro shortcuts.** "Carlov figurine shop (#57 repro)", "MinishRafters Bakery (#58 repro)" — direct warps to the bug-report coordinates so repros don't need a save run. (commits `1599a004`, `89a575a1`)

### Build / CI

- **CI: install Linux audio dev libs.** Was failing to find ALSA/PulseAudio headers on the new runner. (commit `69c448d5`)
- **CI: upgrade meson via pip so Wayland 1.25.0 builds.** Distro meson was too old. (commit `d6252a06`)
- **Updater + Discord links repoint at this fork.** (commits `fdfb5aac`, `2be226ff`)

### Known issues (still open)

- **#9 Steam Deck packages**, **#11 boss asset rendering**, **#15 libfmt.so.12 on Fedora 43**, **#17 GLIBC_2.43 on Nobara/Fedora 43**, **#21 Link's house glitched doors**, **#26 fast-forward not working on Windows**, **#28 random door above staircase**, **#32 shop top textures**, **#37 Lon Lon Ranch shrink/door**, **#40 Hyrule Town door texture**, **#41 EU text extract**, **#44 map grey blocks**, **#47 Mt Crenel background texture**, **#54 boomerang dizzy stars sprite**, **#55 Palace of Winds idle softlock**, **#56 Lake Hylia entry crash**, **#58 bakery attic missing texture** (Windows-only, likely covered by #61 fix — needs reporter retest), **#63 West Hyrule Cave Moldorm sprite chunks**, **#64 Temple of Droplets big-key door crash** — open at the time of this build.

## 0.1.6-experimental — 2026-05-04

Bug-fix + tooling pass on top of 0.1.5. Five open tracker issues closed (cucco crash, chuchu freeze, two Melari's Mines bugs, two Cave-of-Flames bugs); F8 in-game debug menu + F5/F6 quicksave/quickload added for bug-repro work. Picked up matheo/master twice (fileselect / spear-moblin / cucco refactors + AVX2 build option).

### Fixed (issue tracker)

- **#46 Hyrule Town: cucco minigame crash on reward.** `CuccoMinigame_WinRupees` mimicked the GBA `add r0,r3,r0` quirk by casting `CuccoMinigameRupees` to `int`, adding the cucco-type index, and re-casting back to `u8*`. On 64-bit hosts that drops the upper bits of the rupee-table pointer and the dereference SIGSEGVs as soon as the reward sequence runs. Replaced with a normal bounds-checked indexed read. (commit `d629b93c`)
- **#45 Chuchu missing animation (frozen blob).** Animation .bin files extracted at exact ROM size drop the trailing `loop_back` byte (on GBA it was the first byte of the next animation in ROM). The runtime's `FrameZero` hits `AnimRangeHasBytes()=false` on the loop frame, returns silently, and the chuchu freezes on the last frame. `Port_LoadSpritePtrsFromAssets` now allocates a padded copy of every animation that ends on a loop frame and appends a synthetic loop_back byte (next animation's first byte when in-range, otherwise loop-to-start). (commit `68f6e79f`)
- **#36 Cave of Flames: moving lava platforms missing.** Two layered bugs. (a) The asset extractor emits `room_properties/offset_<hex>.bin` files that contain only the leading 4-byte rail pointer of multi-chunk `gUnk_additional_*` tables; the runtime iterates 16-byte LavaPlatform entries from a 4-byte buffer, runs into uninitialised heap, and gives up before any moving platform spawns. `RomBackedPointerForAssetFile()` now recognises that filename pattern and returns `gRomData + <hex>` so the consumer sees the GBA-original contiguous layout including all interleaved rail pointers and chunks. (commit `8a8c92d3`) (b) Cave-of-Flames boss room separately SIGSEGV'd in `sub_0802D650`: `gUnk_080CD7E4/810/828/848` are packed 4-byte GBA function-pointer tables in `data_const_stubs.c`; the C declarations of `void (*const [])(...)` make x86-64 stride 8 bytes per index, calling garbage. Added native shadow tables in `gleerok.c` with the actual decompiled C functions, dispatched via a `GLEEROK_FN(table, idx)` macro. Also guards a NULL `heap->entity1` deref in `sub_0802E7E4` that GBA hardware silently wrote to BIOS at addr 0. (commits `fa77240c`, `9d5f55a5`)
- **#42 Melari's Mines: double heart containers + green warp tile.** Symptom of the same bug class as #43 below — `LoadRoomEntityList(&gUnk_additional_8_MelarisMine_Main)` reads 64 zero bytes from the stub and parses them as four `kind=0` (GROUND_ITEM) entities at world (0,0); the renderer draws those as the spurious heart containers + warp tile. (commit `86f62351`)
- **#43 Melari's Mines: Melari and his crew missing.** `gUnk_additional_8/9_MelarisMine_Main` are zero-initialised stubs in `port_linked_stubs.c`; the asset extractor doesn't index them and `Port_InitDataStubs` had no entry to copy them in from `gRomData`. The state-change path that loads Melari + the two mountain-minish apprentices read zeros and spawned nothing. Enlarged the additional_8 stub to 128 bytes (the GBA table is 96 B, the previous 64-byte size truncated past Melari) and added both stubs to `Port_InitDataStubs` so they're populated from `gRomData[0xDD214..]` and `gRomData[0xDD274..]` on every boot. (commit `86f62351`)

Bonus: a NO_CAP-mode dispatch bug in `sub_08077D38` (player-item animation lookup) had no `default` case, so any item without a no-cap-specific animation (boomerang, gust jar, pacci cane, etc.) ended up with an uninitialised `anim` value of `0x7FFF` — Link rendered as `SPRITE_JARPORTAL` (the pot) while using those items. Added `anim = ptr->frameIndex` as the default. Discovered while building the F8 debug menu's "Unlock all items" feature.

### Tooling

- **F8 debug menu.** In-game overlay with arrow-key navigation. Pages: Items / progress (unlock equipment, max hearts, 999 rupees, all kinstones), Warp (14 destinations including individual dungeon entries with the right spawn coordinates from `gWallMasterScreenTransitions`), Heal to full. Refuses to warp when not in `TASK_GAME` (toast says so). Renders via `SDL_RenderDebugText` after the game frame, intercepts input while open. (commit `ed136db3`)
- **F5 quicksave / F6 quickload.** Snapshots a curated set of game-state regions (gEwram/gIwram/gVram/gIoMem + gSave/gPlayerEntity/gPlayerState/gMain/gRoomControls/gRoomTransition + the full `gEntities[72]`) into a heap buffer on F5, memcpys back on F6 — handy for iterating on bug repros without replaying through a sequence of inputs. Single in-process slot, lost on exit. (commit `d6ddf908`)

### Build / merges

- **Sync with matheo/master ×2.** Picked up matheo's `Port_UnpackRomDataPtr` helper (cleaner than our `Port_PackedRomEntry` wrappers — same correctness, no `#ifdef PC_PORT` blocks at call sites), spear-moblin / hurdy-gurdy / percy / npcUtils / script `gUnk_08001A7C` cleanups using it, the `DmaCopy32` portable wrapper for the kinstone menu, the `houseDoorExterior` `sizeof(...)` bounds check, the `cuccoMinigame` rupees rewrite (we'd just landed our own — kept matheo's), and the AVX2 build option. (commits `afa4b3e2`, `023adf1b`)
- **Reverted `feature/skip_opening_cutscene` (#48).** It broke audio sync — the m4a engine state isn't flushed when the cutscene is skipped, so the next BGM plays ahead of the visuals. Reverted in `a2b806b7`; comment posted on the PR explaining the desync and what would need to change to land it cleanly.

### Known issues (still open)

- **#9 Steam Deck packages**, **#11 boss asset rendering**, **#15 libfmt.so.12 on Fedora 43** (header-only fmt is in xmake.lua but the released tarball needs a rebuild on the CI runner), **#17 GLIBC_2.43 on Nobara/Fedora 43** (CI runner pin to ubuntu-22.04 not yet effective), **#21 Link's house glitched doors**, **#26 fast-forward not working on Windows**, **#27 tree stump scaling** (fixed in `c2d69075`, awaiting tarball), **#28 random door above staircase** (fixed in `829de678`, awaiting test), **#32 shop top textures**, **#37 Lon Lon Ranch shrink/door**, **#40 Hyrule Town door texture**, **#41 EU text extract**, **#44 map grey blocks**, **#47 Mt Crenel background texture** — still open at the time of this build.

## 0.1.5-experimental — 2026-05-04

Issue-tracker bug-fix pass on top of 0.1.4. Eight tracker entries closed — including the cloud-shadow "lines after castle" carry-forward (#25) and the tall-grass shoes overlay (#24) that had been deferred since 0.1.2 — plus a port-side dispatcher fix that unblocks any enemy whose death cascade lives in OnCollision/OnKnockback. Adds an F9 bug-report capture for playtesters and surfaces the port version in the window title.

### Fixed (issue tracker)

- **#5 Deepwood Shrine: Link backflips off solid walls.** `FindValueForKey` iterates a `KeyValuePair` list until `key == 0`. The GBA assembled each list as N entries plus a trailing `gUnk_..End` u16 sentinel placed immediately after; the C port has those as separate top-level definitions which the linker is free to reorder. When the End ends up elsewhere, scanning falls through into whatever array follows. Verified at runtime: walking up into a Deepwood wall returned uVar3=5 from the next array's `{42, 5}` entry. Inline a `{0, 0}` sentinel at the end of each KeyValuePair array so each list self-terminates. Same fix applied to `sTiles0..3` in player.c which had the same single-entry-plus-trailing-u16 shape. (commit `38d8290a`)
- **#22 BGM doesn't duck during item-get jingle.** GBA m4a's per-channel priority allocation let `SFX_ITEM_GET` (high priority on `MUSIC_PLAYER_1E`) starve `MUSIC_PLAYER_BGM` of channels, naturally muting BGM. The PC backend renders all players independently. Reuse the existing `volumeBgm` fade plumbing: when `SoundReq` starts a recognized ducking SFX, set `volumeBgmTarget = 0`; each frame `AudioMain` polls a new `Port_M4A_Backend_IsPlayerActive` helper to detect when the SFX player has reached `ply_fine` and restores `volumeBgmTarget = 0x100`. (commit `0ecf37b4`)
- **#24 Tall-grass / shallow-water shoes overlay missing.** GBA renders a small overlay sprite over Link's feet when on tall grass (act-tile 0x2F) or shallow water (act-tile 0x0F). Frame data lives at ROM 0x080B2B58 in 16 IWRAM-relative pointers (4 shadow rows × 4 anim frames). The shadow-table fix (#10) skipped the IWRAM overlay copy and never resolved this companion table — the `ProcessEntityForDraw` `z >= 0` branch had a TODO. Translate the IWRAM pointers (delta 0x050AC28C, USA) and render via `RenderSpritePieces` when an entity at `z >= 0` flags `spritePriority & 8` and stands on the right act-tile. (commit `0178cf37`)
- **#25 Cloud overlay broken into lines after exiting Hyrule Castle.** Hyrule Castle's `holeManager` parallax overwrites `SCREENBASE 30` (cloud tilemap) with castle data; on PC the asset cache short-circuits the area gfx-group reload that would restore it on GBA. Snapshot the live BG3 VRAM (CHARBASE 1 chardata + SCREENBASE 30 tilemap) the first time `CloudOverlayManager_OnEnterRoom` fires for `AREA_HYRULE_FIELD` with a working overlay, then restore on every subsequent Hyrule Field entry — side-steps the question of which gfx group actually owns the cloud data. (commit `14b184b6`)
- **#34 Mt Crenel mountaintop renders pink/cyan terrain.** `weatherChangeManager.c` cross-fades the mountaintop palette via `sub_08059894(gPalette_549, gPalette_549 + 0xD0, factor)`. On GBA the linker placed `gPalette_549..gPalette_574` sequentially in `gfxAndPalettes`, so the +0xD0 read walks into `gPalette_562`. The PC port stubbed `gPalette_549` as a single 32-byte buffer, so the offset read fell off the end into garbage. Allocate the full 416-color block and populate it from `gGlobalGfxAndPalettes` at offset 0x44A0 (same on USA and EU). (commit `1a5d2931`)
- **#35 AcroBandit stack drifts off-screen + survivors don't fall.** Three layered bugs: (a) `AcroBanditEntity.unk_70/72` aliased `Enemy.homeX/homeY` on GBA (both at offset 0x70/72) but the alias broke on PC because Entity grew 0x68 → 0x90 bytes; chain bandits ended up with `homeX = 0` and the stack drifted toward (0,0). Restored the alias by padding the struct on PC. (b) Hazards (pit/water/lava) delete a chain bandit without going through OnCollision's chain unwind, leaving children with stale `parent` pointing at zeroed memory; added a self-heal in Action4 that promotes a bandit to chain head when `parent->kind != 3`. (c) The original chain unwind only walks DOWN from the dying bandit, leaving ancestors stuck in Action4 when the player kills the bottom; walk UP too so the whole stack falls together. (commit `b5ee0144`)
- **#39 Cave of Flames map crash on B3.** `GetAreaRoomPropertyList` had only NULL-or-ROM-range branches; for never-visited dungeon-boss areas the slot held a stale 4-byte→8-byte widen value (non-NULL but not a real address) and the fallback `areaTable[room]` dereference SIGSEGV'd. Added a third bucket: if the pointer is non-NULL, not in ROM, and not in the canonical x86-64 user-space range (< 2^47), force a refresh and reclassify. (commit `6dd75555`)
- **GetNextFunction skipped OnCollision/OnKnockback when health=0.** The PC fix added for #20 (Peahat corpse stuck in OnGrabbed loop) short-circuited to OnDeath as soon as `health == 0`, which also blocked the AcroBandit chain unwind (#35) and the death-fall animation. Preserve OnCollision/OnKnockback dispatch when their conditions are met, fall through to OnDeath only when neither is active. (commit `ef1b8344`)

### Tooling

- **F9 bug-report capture for playtesters.** Pressing F9 in-game writes a timestamped `bugreport_YYYYMMDD_HHMMSS/` directory next to the binary containing `screenshot.bmp` (240x160 GBA framebuffer), `save.bin` (current EEPROM dump), and `state.txt` (area / room / coords / hp / ROM size). Attach the folder to a GitHub issue. (commit `52cbd7e2`)
- **Window title now shows port version.** "The Minish Cap 0.1.5-experimental - 60.0 FPS" instead of just "The Minish Cap - 60.0 FPS", so testers can include their version when filing issues without digging through the changelog. (commit `52cbd7e2`)

### Known issues (still open)

- **#9 Steam Deck packages**, **#11 boss asset rendering**, **#21 Link's house glitched doors**, **#27 tree stump scaling** (already fixed in `c2d69075`, awaiting tarball), **#28 random door above staircase** (already fixed in `829de678`), **#32 shop top textures**, **#36 CoF moving platform** (already fixed), **#37 Lon Lon Ranch closed door**, **#38 LikeLike crash** (already fixed), **#40 Hyrule Town door texture**, **#41 EU text extract** — still open at the time of this build.

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
