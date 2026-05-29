# Changelog

## 0.3.2-experimental — 2026-05-27

Re-tagged from the original 2026-05-26 cut to include the post-tag
work: the Project Picori UI rebrand (logo, theme, interactive
prelaunch with hash-validated ROM picker), four Windows-only fixes
that surfaced under Wine testing (#44 grey-blocks + cursor-bouncing,
plus universal collision-disable), and a cross-platform-parity audit
that closes several straggler `#ifdef __linux__`-only paths.

### Fixed (issue tracker)

- **#87 Great Fairies have a light/sphere stuck on their face when talking.** As a Great Fairy appears she spawns a descending "blessing light" (FORM9) whose `child` pointer is never set by the spawn site (`GreatFairy_FinalUpdate`). The #33 Mt-Crenel-fountain crash fix (`06ad49dbf`) worked around the resulting NULL-deref in `sub_080871F8` by aliasing `target->child = super` — but that made the light **home onto the fairy** (32px above = her face) and park there permanently, since the action that releases it waits on `animFlags & 4`, a flag never set for this form. On GBA the unset `child` is open-bus garbage, so the light drifts off-screen and is cleaned up — no visible light. Fix: drop the `child = super` alias and instead **delete the light when it has no valid target**, matching the GBA result (no light on her face) while keeping the #33 crash fixed. (`src/object/greatFairy.c`)
- **#139 ToGrimblade (Grimblade dojo entrance): lit flame braziers vanish after a dark-room round-trip.** The flaming braziers flanking the door are BG1 (foreground) tiles drawn over the BG2 bowl base; both BGs sit at the *same* priority (2), so on GBA hardware the lower-numbered BG (BG1 = flame) wins the tie and draws on top. Going down into the dark Grimblade dojo and back up (an intra-area `RELOAD_ALL` transition) leaves `BG3CNT` at the dojo's priority 0 (`0x1e0c`). BG3 is disabled so it never renders — but VirtuaPPU's `mode1.c` composite built its per-pixel draw order with an **unstable selection sort** over all four BGs: the disabled BG3 at priority 0 swapped forward and displaced BG1 *past* BG2 in the order, flipping the equal-priority tie-break so the BG2 bowl drew over the BG1 flame and the flame "vanished" (only the bowl base showed). A full room init reset `BG3CNT` and masked the bug, which is why exiting the area and returning restored the flames. Fix replaces the sort with a **stable insertion sort** so equal-priority BGs always keep their BG-index order (GBA-accurate), regardless of any other BG's priority — a disabled/low-priority BG can no longer reorder two same-priority layers. Port-side ViruaPPU patch `port/patches/viruappu-bg-priority-sort.patch`.
- **#54 follow-up: Boomerang — dizzy-stars sprite never leaves enemies.** `b9ce2fb5d` restored the alive-dispatch branch so `GenericConfused` ticks on stunned enemies, but the FX detach inside `GenericConfused` still never fired — the rationale conflated `Entity::child` (PC offset 0x68) with `Enemy::child` (PC offset 0x90); they're two different fields after pointer widening. `EnemyCreateFX` writes the FX through the `Enemy*` cast, so the FX pointer lives in `Enemy::child`; the detach gate was reading `entity->child` (Entity::child), the kind/id/type identity check never matched, and `FX_STARS` — which self-deletes only when its parent goes NULL — stayed glued to the enemy after stun ended. (commit `d86cde0ba`)
- **#55 "Palace of Winds Tower" (actually Wind Tribe Tower 2F): softlock removing ghost from Gregal.** `script_WindTribespeople6` is declared as a 2-byte BSS stub in `port/port_linked_stubs.c` (one of many such stubs to satisfy the linker — the real script bytes live in ROM). The call site at `src/npc/windTribespeople.c:79` used `&script_WindTribespeople6` which resolved to zero-filled memory, so when Tribesperson5 script-swapped to it after the gust-jar capture, her `ExecuteScript` hit the defensive `cmd == 0x0000` short-circuit every frame and never advanced. Gregal had already set sync flag 1 and parked on `WaitForSyncFlagAndClear 2`; the partner's `SetSyncFlag 2` never executed → softlock. Diagnosed live via GDB `inspect_cutscene` + the `[sync]` diagnostic stream. Fix routes the call through `PORT_SCRIPT(script_WindTribespeople6)` so the real bytes resolve via `Port_ResolveRomData`; ROM address `0x08014A80` confirmed against the upstream zeldaret/tmc USA map. (commit `1b6f1d4b8`)
- **#102 Veil Falls: Biggoron + sibling BG-manager EWRAM-bridge bugs.** The GBA decomp uses pointer arithmetic that crosses *between* two distinct EWRAM symbols (`gMapDataTopSpecial + 0x4000` resolves to `gUnk_02006F00` on GBA's flat 256 KB EWRAM layout). On PC the two arrays are separate host allocations, so the bridged offset lands in unrelated memory: at best silently corrupts the wrong buffer, at worst SIGSEGVs inside `DmaCopy16`. Confirmed cases: `bigGoron.c::sub_0806D110` + `sub_0806D164`, `horizontalMinishPathBackgroundManager.c::sub_08058004`, `minishRaftersBackgroundManager.c::sub_080582D0` + `sub_080582A0`. PC fix routes bridge arithmetic through the actual target symbol on `PC_PORT` and clamps / early-returns when scroll-derived offsets would overflow the source buffer. (commit `7893f1cb4`)
- **#103 Cloud Tops: BG texture broken after kinstone fusion.** `vaatiAppearingManager` armed `SetVBlankDMA` on entry and never called `DisableVBlankDMA` on exit. On GBA this was benign because the next subtask re-armed `SetVBlankDMA` and overwrote the old src/dest; on PC the leftover DMA kept firing into BG2's charBase register every HBlank, breaking the next room's BG rendering. Fix pairs every `SetVBlankDMA` with a `DisableVBlankDMA` on the manager's exit path, plus a `Subtask_FadeOut` catch-all that disables any leftover DMA and calls `AnimatedBackgroundManager_RestoreBgGfx` on every active manager so BG3 re-arms cleanly across menu→game transitions. (commits `4e28ea2c4`, `543a8f47a`, `ebee7e9a6` for the BG-restore plumbing)
- **#136 Palace of Winds: Gyorg boss NULL deref on entry.** The boss-room dispatcher (`gyorgBossObject.c::sub_080A1DCC`) unconditionally calls tail helpers *after* the action handler every frame. On frame 0 the action handler is `SetupStart`, which populates `heap->female / male1 / male2` but NOT `heap->mouth / heap->tail` — those get filled by `GyorgFemale_Setup` on the female's first tick. The helper then dereferences `heap->mouth` and NULL-crashes. Fix NULL-guards the heap fields plus the `tail->child->child->child` walk. (commit `b91a4faa6`)
- **#44 Windows-only: pause-menu world-map grey blocks + cursor bouncing.** Two independent Windows-side root causes:
  1. `port_resolve_addr`'s `#ifdef _WIN32` branch used `VirtualQuery` to short-circuit GBA→host remapping for any address in the GBA range that happened to be a committed VM page on the host — which is the case for every real GBA EWRAM address since the low address space is densely mapped by Wine / Windows system DLLs. The map screen's tilemap pointer (`gMapDataBottomSpecial` at 0x02006B00) came back as the raw GBA value instead of `gEwram[0x6B00]`, and BG3 read garbage at the top. (commit `d9b2abef7`)
  2. MinGW defaults to `-mms-bitfields` (MSVC bitfield ABI) which makes any `__attribute__((packed))` struct with bitfields larger than the GCC-default packing — verified empirically: the pause-menu's `gUnk_08128DE8_struct_2` (5+5+6 bits) is 2 bytes on Linux GCC but 3 bytes on MinGW default. That shifted the outer struct's `unk6` / `unk7` (the screen-space cursor coords) from offsets 6/7 to 8/9, so the player-position marker read its x/y from the next entry's bitfield bytes. Adding `-mno-ms-bitfields` globally for MinGW / Windows targets restores parity. (commit `a8f480012`)
- **#44 (Windows-only, separate from #44 above): no enemy damages Link, Link can't damage enemies.** `src/collision.c::IsColliding` had a host-pointer range guard (`pa >= 0x100000000000`) added to catch half-pointer-write artifacts on Linux x86_64, but the lower bound (17.5 TB) excludes every valid Windows / Wine user-mode pointer — Wine puts entities at ~0x140_XXXXX (≈5 GB). The guard returned `FALSE` for every hitbox pair, silently disabling all combat collision. Split the check on `_WIN32`: Linux keeps the original strict range (still catches the documented half-pointer-write hazard); Windows uses a looser `>= 0x10000` lower bound (rules out NULL + the first 64 KB, which is the only platform-portable "obviously bogus" signal). (commit `b1685b5c3`)

### Quality of life

- **Project Picori UI rebrand.** Project codenamed "Project Picori" — README title updated, F8 / config menu re-themed with a deep-green Minish palette + 10 px card rounding, and a hand-drawn Ezlo-and-Link logo (`docs/picori-logo.png`) embedded in the binary via xmake's `utils.bin2c` rule. (commits `b9fa90b0e`, `d21d547ed`)
- **Interactive prelaunch screen replaces the timed splash.** `Port_PPU_Init` now runs before any ROM is loaded; the prelaunch ImGui card is the first interactive frame the user sees. Two states: "No ROM found" (big Select-ROM button, hash-explainer copy) or "ROM detected" (version + filename + Change-ROM + Play). Play kicks `Port_LoadRom` → asset extraction → audio init → `AgbMain`. Works on both SDL_Renderer and SDL_GPU backends (the latter via a new `Port_GPU_PresentPrelaunchFrame` that pairs PrepareDrawData + ImGui render in one pass). (commits `5a069c39d`, `03a2518ef`, plus `686a67cbe` / `c22f197e2` for the splash branding)
- **ROM picker validates by SHA-1 hash, not filename.** Drops the gamecode-only check (BZME / BZMP / BZMJ); any `.gba` whose SHA-1 matches one of the five known TMC dumps (USA / EU / JP retail + USA / JP demo) is accepted. Users can name their dump anything. Rejection dialog now prints the picked file's actual hash next to the expected list. (commit `f0497dc4b`)
- **xBRZ on the SDL_GPU backend.** The CPU-side xBRZ 4× upscaler used to only fire on the SDL_Renderer path; the SDL_GPU branch bypassed it entirely. GPU branch now runs `Port_Upscale_xBRZ_4x` and feeds the 960×640 buffer straight to `Port_GPU_PresentFrame` (mutually exclusive with internal-scale, same as the SDL_Renderer branch). F8 → "Filter" picker is no longer gated behind "GPU inactive". CRT filter stays SW-only — separate Stage 3 work. (commit `205616720`)
- **Internal render scale cap raised 4× → 10×.** The 4× cap was based on a stale comment about a fixed framebuffer size; the scaled buffer is actually `malloc`'d lazily and the affine-OAM overlay scales naturally with the `scale` parameter. 10× yields a 2400×1600 internal render (≈15 MB scratch + matching SDL_Texture). Verified live on both SDL_Renderer and SDL_GPU / Vulkan paths. (commit `7a12173c5`)

### Defensive / hardening (no specific issue)

- **#136 family — defensive `parent == NULL` guards across 12 boss-helper sites.** Class audit after the #136 fix found the same shape (helper derefs `this->parent->next == NULL` at function entry with no preceding NULL check) across Vaati / Mazaal / Gyorg families plus `rupeeLike.c` and `flame.c`. `#ifdef PC_PORT` early-returns mirror the existing `sub_080A1DCC` fix; GBA path is byte-identical. Patched: `vaatiWrathEye`, three `vaatiEyesMacro` functions, `vaatiProjectile`, `v3ElectricProjectile`, `mazaalHead`, two `mazaalMacro` functions, `mazaalObject`, `gyorgFemaleEye`, `gyorgFemaleMouth`, `gyorgTail`, `rupeeLike`, `flame`. (commit `cd7805da2`)
- **`IsColliding` host-pointer range guard for stale hitbox** (Linux path retained, Windows path loosened — see #44 above).
- **Auto-crash bug-report capture restored.** A Matheo-merge regression removed the `Port_BugReport_InstallCrashHandlers` call from the top of `main()`. F9 manual capture kept working but auto-on-crash bundles silently stopped generating. Re-added the install call. (commit `51dc6c9c1`)
- **Delayed-entity bitmap reset on PC.** `gArea.filler6` aliases `gUnk_020342F8` (the delayed-entity-load bitmap) on GBA but they're separate symbols on PC; `sub_0806F364` now clears both. (commit `b1c58e587`)

### Cross-platform parity audit

- **`ExecutableDirectory()` was Linux-only in `port_mods.cpp` + `port_randomizer.cpp`** — both fell through to `current_path()` on Windows / macOS, returning the cwd instead of the exe's directory. Mod loader and randomizer CLI lookup probed the wrong root and silently failed. Added Windows `GetModuleFileNameW` + macOS `_NSGetExecutablePath` branches matching the existing pattern in `port_asset_bootstrap.cpp::GetExecutableDirectory`. (commit `79a11cf68`)
- **#135 — TownMinish bookshelf NPC didn't offer Speak prompt.** Decomp typo in the `gUnk_additional_a_TownMinishHoles_LibraryBookshelf` EntityData table: `15` (=0x0F) instead of the default `0x4F` npc_raw pool. Replaced with the correct value so `sub_0804AF0C`'s case 0x40 (which calls `StartCutscene` and sets `ENT_SCRIPTED`) fires. (commit `ebee7e9a6`)

### Build / CI / docs

- **CI matrix now triggers on `sync-matheo-release`** (the working branch), in addition to `master` / `CI-Test`. Linux + Windows + macOS-arm64 all build on every push so any new Linux-only code gets flagged at PR time. (commit `4c823eae5`)
- **Windows F9 path-resolve fix** — replaced POSIX `realpath` with `_fullpath` under `#ifdef _WIN32`. MinGW lacks `realpath` and a whole-file `extern "C"` declaration of it failed at link time. (commit `5cff9ad6a`)
- **`build.py` always passes `--gpu_renderer=y`** so the shipping binary has the SDL_GPU backend + F8 → Shader Preset picker enabled. (commit `d3ff4c374`)
- **Windows CI: force LF on checkout + `git apply --ignore-whitespace`** so `port/patches/viruappu-*.patch` apply cleanly even when CRLF crept into the submodule. (commit `d82630f44`)
- **`docs/widescreen-phase2-design.md` kept after the Phase 2 revert** as an archive of what was tried, what worked, what broke, and why — so a future attempt doesn't re-walk the same dead-ends. (commit `7cd6afeab`)

## 0.3.1-experimental — 2026-05-24

Bug-fix release covering five issue-tracker reports + a packet of
quality-of-life features (renderer backend picker, auto-save,
quit-save confirm, F8 audio tab, profile management).

### Fixed (issue tracker)

- **#131 Deepwood Shrine: missing barrel textures after pause-menu close.** Closing the pause menu while standing inside a barrel left `RollingBarrelManager` un-reinitialised, so the barrel sprites' GFX slots were never re-claimed. `Subtask_FadeOut` → `RestoreGameTask` now re-runs `RollingBarrelManager_OnEnterRoom()` when the barrel-update path is the resumed task. Confirmed both Linux and Wine/Windows builds. (commit `2b07478d`)
- **#128 Hyrule Town: minish doors disappear after first walk-through; house signs disappear after scrolling off-screen.** Both bugs in the same family — a manager subclass declared an `unk_20` field meant to alias `EntityManager::field_0x20` (the per-instance "active door slots" bitmask).  On GBA the 4-byte Entity::zVelocity at offset 0x20 was reused as the manager's field; on PC the pointer-widened Entity shifts that slot, and the door's `parent->zVelocity & mask` read goes to actual zVelocity (always 0) so doors always delete themselves. Replaced the alias with a direct cast to the manager subclass, reading the real `field_0x20` / `bitfield` slot. Same pattern applied to `houseSign.c`. (commits `41c77124`, `f47e8470`)
- **#129 Hyrule Castle Garden: post-takeover knights stand still.** The non-scripted patrol guards' movement script lives in `gUnk_0810F6BC[type]`, a 6-entry packed-pointer table.  `Port_ReadPackedRomPtr` was rejecting it because the table sits in PC `.rodata` (`data_const_stubs.c` defines it as `const u8[920]` copied verbatim from ROM 0x10F6BC) rather than inside the `gRomData` mmap. The base-bounds gate said "outside gRomData, reject" and `sub_0806EE04` got `child=NULL`, so the per-tick `RunScript` found no script to execute and the guards never moved. Loosened the gate: still bounds-validate when base is in gRomData, but accept PC stubs and trust the caller. Movement script loads, guards patrol. (commit `2f2fdbc2`)
- **#101 Temple of Droplets: Scissors Beetle crash when defeated with detached mandibles (follow-up).** `8d5e0066f` already guarded `sub_08038C2C` but the underlying mandibles projectile has three more parent/child deref sites that crash in the same scenario.  Added NULL guards to `MandiblesProjectile` dispatch (line 48 `entity->confusedTime` after both child + parent fall through NULL), `sub_080AA270` (parent-anim-state read), and `OnCollision` default case (parent iframes/knockback writes).  Guarded with `#ifdef PC_PORT` so the GBA path is byte-identical. (commit `438e4bf1`)
- **#110 \[FATAL\] palette group N not found — startup abort on Arch Linux.** Title screen tries to load palette group 2 (Japanese title intro) when `gSaveHeader->language != 0`. When the user's setup didn't extract the non-English palette files — or has a tmc.sav from a non-English session — the abort kills the engine before file-select. Engine-level fallback: a missing palette group N now tries the canonical English-equivalent (2 → 1, 4 → 3) before aborting, with a one-shot warning.  Title colours may be slightly off in the rare case the user genuinely wanted a non-English palette, but the game boots. The `7a7fd0c1` asset-environment dump is still emitted if neither group resolves. (commit `b05039e3`)
- **#78 Wind Ruins wizards (follow-up).** After `8d5e0066f` fixed the divide-by-zero crash, JesterWizard / linkdedo reported the wizards "appear out of bounds" — the teleport do-while picked random positions in `[homeX, homeX + rangeX*8)` and accepted any walkable tile, including ones outside the room (OOB tiles return non-collision from asset-load fallback).  Now: reject candidates outside `gRoomControls.{origin_x..origin_x+width, origin_y..origin_y+height}` and cap iterations at 64 so the loop can't infinite-spin on unlucky rooms. (commit `924967d3`)

### Quality of life

- **F8 → renderer backend picker** (Auto / Software / GPU).  Software stays the default; users with broken SDL_GPU drivers (Wayland surface conflicts in particular) can pin Software at startup without env-var fiddling. (commit `9eca4567`)
- **F8 → Audio tab with per-category SFX mutes** — mute Link's footsteps, sword swings, item pickups, etc. independently while keeping BGM. (commit `510e4721`)
- **F8 → Profiles tab: rename + delete soft slots.** (commit `467faf91`)
- **Auto-save on area / room change** — eliminates "I forgot to save" loss on crash. Quick-save-style soft slot is rotated every transition. (commit `7247a18e`)
- **Quit-save confirm modal on window close.** Asks before discarding unsaved progress when you Alt-F4 / close the window. (commit `6c7e9972`)
- **GPU backend parity with Software backend** — aspect ratio, bg fill, soft-slot overlay, internal render scale all now work on the SDL_GPU path. (commits `91283475`, `7d97af48`)

### Defensive / hardening (no specific issue)

- **#131-class crash hardening: defensive `UnlinkEntity` guards against half-pointer prev/next.** Rupee LikeLike crash showed `prev = 0x555511113333` (decompressed 32-bit-half written into 64-bit field).  `UnlinkEntity` now NULL-tests `ent->prev` / `ent->next` before dereferencing them and logs once if the pointer fails the user-space-address range check. Quiet on the GBA path, defensive guard on PC. (commit `265c2a5f`)
- **gSpritePtrs runtime-extend beyond 329-entry compile-time cap.** Sprite-pointer indices > 329 used to fall off the end of `port_asset_index.c`'s lookup table; runtime now walks the ROM table directly past that index until it hits the 0-sentinel.  Lays groundwork for #127-class "sprite present but invisible". (commit `fcb5b57c`)

### Notes on the Vulkan RT experiment

The `port/vk_rt_experiment/` standalone Vulkan ray tracing demo gained
slices 4–13 this release (sprite plane, multi-bounce GI, point lights,
a-trous denoiser, water reflections, material variety, animated water,
volumetric fog, bloom + dither, perf pass).  It's still a separate
binary, not linked into `tmc_pc` — it consumes `/dev/shm/tmc_framebuffer`
when `tmc_pc` is started with `TMC_PUBLISH_FRAMEBUFFER=1`. No change to
normal end-user behaviour.

## 0.3.0-experimental — 2026-05-23

Boss-room crash sweep across Temple of Droplets, Discord Rich Presence
default-on with native Windows named-pipe support, and the post-v0.2.2.0
bundle of audio / VSync / portal / asset-loader follow-ups.

### Fixed (issue tracker)

- **#64 Temple of Droplets: big-key-door / Big Octorok boss room crashes.** Two confirmed SIGSEGVs along the path from the Entrance room (3) through the boss door into the BigOcto room (14). Same `#91`/`#97` family as today's frozen-octorok fix: a child entity dereferences a sibling pointer that hasn't been assigned yet. (a) `OctorokBoss_Init` (`src/enemy/octorokBoss.c:472`) ends with a direct `OctorokBoss_Action1(this)` call. The head (WHOLE) creates LEG_*/MOUTH/TAIL_END/TAIL children, but their own Inits run later in the entity-update loop in list order — so a leg or tail child's Action1 reads `heap->mouthObject->base.health` before MOUTH's Init has assigned `mouthObject` (line 455). GBA NULL-deref returned BIOS bytes; PC SIGSEGVs. Guarded four sites in Action1 (lines ~598, 623 for LEG_*, 636 for TAIL, 657 for TAIL_END). (b) `OctorokBossObject_Action1` case 4 line 251 derefs `helper->tailObjects[super->timer]` — the type-4 (smoke-attack) variant is spawned by `OctorokBoss_ExecuteAttackSmoke` without ever assigning `helper`, so it stays NULL. The line is a `x = x` self-assign (no observable effect even on GBA), so the PC-side guard simply skips it. Behaviour-preserving: NULL window is one Init frame before the sprite has even drawn; all reads converge to real values on the next tick. (commit `a7eeda1a`)
- **#100 Temple of Droplets: Blue Chuchus don't spawn after lever.** Resolved indirectly. The chuchu spawner is `TempleOfDropletsManager` Type 1 in room 16 (BigBlueChuchu), whose `localFlag` (`0x46`) is one of the fields the `#75` `src/room.c:122` rewrite already restores for **all** `id == 0x15` managers, not just the sunbeam Type 2. The bug was also gated by the boss-room frozen-octorok crash blocking the route through room 8 (Element); once that crash is gone, the room loads and the chuchus spawn as designed. No code change beyond `69c84f84`. (Verified live by F8-warping to area 0x60 room 0x10 and watching `[ToD-mgr] type=1 action=3` advance to 4 on lever-push.)

### Fixed (other)

- **Temple of Droplets Element room: `FrozenOctorok_Action1` SIGSEGV on entry.** First crash uncovered while reproducing #100. Same Init→Action1 pattern as the OctorokBoss fix above: leg children (types 1-4) of the head (type 0 / WHOLE-equivalent) run their Init→Action1 (line 150) before the mouth (type 5) has assigned `heap->mouthObject`. GBA NULL-deref → BIOS garbage ≠ 1 → else branch; PC → SIGSEGV at the offset_of_health load. Single PC-port-only NULL guard at `src/object/frozenOctorok.c:193`. Also applied the same guard to three defence-in-depth sites in `src/enemy/octorokBoss.c` (`Hit` at line 122 — `tailObjects[0]` camera-target; `Hit_SubAction6` at lines 303/306 — `legObjects[0]` death-FX + `mouthObject` death-kill); these sites only reach the deref under a valid boss state, but the cost of guarding is one NULL check and avoids re-hitting the same pattern from a different attack path. (commit `69c84f84`)

### Tooling / new features

- **Discord Rich Presence: default-on + native Windows support.** Rich Presence now ships enabled by default on Linux, macOS, and Windows. Adds a `TryConnectWindows()` path in `port/port_discord_rpc.c` that opens `\\?\pipe\discord-ipc-N` via `CreateFileA` (`N = 0..9`), with the existing JSON-RPC frame protocol reused unchanged — `WriteFile` replaces `send(MSG_NOSIGNAL)` on the Windows branch. Connection handle storage moved from `int sock` to `intptr_t handle` so the same field holds either a Unix fd or a `HANDLE`. F8 toggle still controls per-session enable/disable; `TMC_DISCORD_APP_ID` env var or the `discord_app_id.txt`-at-build-time path still gate whether the connection is even attempted, so users without Discord (or without a registered app ID) see no behaviour change.

### Notes

These NULL-guard fixes (`#91`/`#97` family) are intentionally not bit-identical to GBA: on hardware, the BIOS bytes at the NULL+offset address are deterministic but not always equal to the value our guard substitutes, so the chosen branch can differ for **one Init frame** before the sibling's Init runs. The affected fields (`unk_74`/`unk_76`/`radius`/`DeleteThisEntity` gating) all reset to correct values on the next tick, and the child sprite isn't drawn yet during that one frame, so the divergence is unobservable in-game. This matches the established "guard + clamp/early-return" pattern documented under *Critical concepts → NULL deref differs from GBA* in CLAUDE.md.

### Carries from post-v0.2.2.0 master

Issues already fixed in master between v0.2.2.0 and this tag — closed by re-release rather than further code changes:
**#42, #45, #74, #75, #91, #94 (auto safe-spawn), #99, #101, #106, #107, #117, #118, #119**.

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
