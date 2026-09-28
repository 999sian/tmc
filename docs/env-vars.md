# Environment variables

Every variable read via `getenv`/`SDL_getenv` in `port/` and `src/` (91 total).
Boolean switches treat unset, empty or `0` as off unless noted.

Variables marked **(harness)** are read only by the `port/port_repro_*.c`
harnesses. Those files are compiled only when the xmake option
`repro_harness` resolves on: `default` (the default) turns them on in debug
builds and off in release; `xmake f --repro_harness=y` or `=n` forces it.
`build.py` always passes `=y` because CI's PPU parity gate uses `TMC_PERFCAP`.
Without the harness, these variables do nothing.

Warp arguments use the `area,room,x,y,layer` form (C integer literals, so
`0x` hex works).

## Startup, ROM and assets

| Variable | File(s) | Purpose |
|---|---|---|
| `TMC_BASEROM` | port/port_rom.c | Path to the ROM; checked before the exe-dir/cwd probe list. |
| `TMC_AUTOPLAY` | port/port_main.c, port/port_update_check.c | Skip the prelaunch menu and the update check (headless/CI runs). |
| `TMC_NO_UPDATE_CHECK` | port/port_update_check.c | Skip the network update check. |
| `TMC_AUTOLOAD` | src/fileselect.c | `0`-`2`: auto-select that file-select slot and start it. |
| `TMC_ANDROID_RUNTIME_DIR` | port/port_asset_bootstrap.cpp | Android only: override the runtime asset directory. |
| `TMC_ASSET_JOBS` | port/port_asset_log.cpp | Worker count (1-64) for asset extraction. |
| `TMC_MODS` | port/port_mods.cpp | Explicit active mod set; unset lets the loader scan `mods/`. |
| `TMC_RANDO_LOGIC` | port/rando/rando_logic.cpp | Path to the randomizer logic file, tried before the defaults. |
| `TMC_RANDO_FILE_MENU` | port/rando/rando_file_menu.c | `0` hides the randomizer file-select overlay (on by default). |
| `TMC_DISCORD_APP_ID` | port/port_discord_rpc.c | Discord Rich Presence app ID; overrides the compiled-in default. |
| `TMC_SAVE_RETAIL_LAYOUT` | port/port_save.c | `1`: never migrate flags; unstamped saves keep the retail layout (vetoes migration). |
| `TMC_SAVE_MIGRATE_LEGACY_FLAGS` | port/port_save.c | `1`: force old-PC-layout flag migration on every unstamped slot (default auto-detects). |

## Video and rendering

| Variable | File(s) | Purpose |
|---|---|---|
| `TMC_GPU_FILTER` | port/port_gpu_renderer.cpp | GPU present filter: `lcd_grid`, `scanline`, `handheld`, `vignette`, ... |
| `TMC_GLSLP_PRESET` | port/port_gpu_renderer.cpp, port/port_main.c | Load a libretro `.glslp` preset; overrides the configured shader preset. |
| `TMC_COLOR_CORRECTION` | port/port_ppu.cpp | Force GBA colour correction on/off, overriding config. |
| `TMC_LCD_PERSISTENCE` | port/port_ppu.cpp | Force the LCD ghosting effect on/off, overriding config. |
| `TMC_LCD_PERSISTENCE_RHO` | port/port_ppu.cpp | LCD persistence strength, `0 <= rho < 1`. |
| `TMC_RENDER_THREADS` | port/port_ppu.cpp | OpenMP render thread count (default: cores - 1). |
| `TMC_WS_VIEW_WIDTH` | port/port_linked_stubs.c | Override the widescreen view width. |
| `TMC_WS_TRACE` | port/port_linked_stubs.c | Log per-room widescreen width decisions. |
| `TMC_PUBLISH_FRAMEBUFFER` | port/port_shm_framebuffer.c | Publish the GBA framebuffer to shared memory. |
| `TMC_DUMP_ICON` | port/port_icon.cpp | Write the ROM-extracted window icon to this BMP path. |
| `OMP_NUM_THREADS` | port/port_ppu.cpp | Standard OpenMP; when set, the port keeps its value instead of picking a count. |
| `OMP_WAIT_POLICY` | port/port_ppu.cpp | Standard OpenMP; defaulted to `passive` when unset. |
| `KMP_BLOCKTIME` | port/port_ppu.cpp | Standard LLVM OpenMP; block time forced to 0 when unset (clang builds). |
| `SDL_VIDEODRIVER` | port/port_main.c | Standard SDL; the port reads it to report and pick the video backend. |
| `DISPLAY` | port/port_main.c | Logged at startup; used to choose X11 vs Wayland. |
| `WAYLAND_DISPLAY` | port/port_main.c | Logged at startup; used to choose X11 vs Wayland. |
| `XDG_SESSION_TYPE` | port/port_main.c | Logged at startup for video diagnostics. |

## Audio

| Variable | File(s) | Purpose |
|---|---|---|
| `TMC_AUDIO_FRAMES` | port/port_audio.c | SDL audio device buffer size in sample frames. |
| `TMC_AUDIO_TRACE` | port/port_audio.c | Log audio callback timing and underruns. |

## Timing, profiling and logging

| Variable | File(s) | Purpose |
|---|---|---|
| `TMC_VERBOSE` | port/port_debug_verbose.c | Enable per-frame stderr logging. |
| `TMC_VERBOSE_GBA_VA` | port/port_main.c | Log the reserved GBA address window. |
| `TMC_PROFILE` | port/port_bios.c, port/port_repro_perfcap.c | Enable the frame profiler; also keeps perfcap from exiting after its dump. |
| `TMC_PACE_LOG` | port/port_bios.c | Print one pacing line (fps/tps) per second. |
| `TMC_LEGACY_PACING` | port/port_bios.c | Use the legacy one-render-per-tick loop. |
| `TMC_A11Y_DEBUG` | port/port_a11y_cues.c | Log accessibility cue decisions. |
| `TMC_ROLL_MACRO_DEBUG` | port/port_roll_attack_macro.c | `1`: log roll-attack macro state. |
| `TMC_RANDO_DEBUG` | port/rando/rando_keymap.c, port/rando/rando_logic.cpp | Log randomizer keymap misses and placement details. |
| `TMC_PAL_TRACE` | src/common.c, port/port_repro_perfcap.c | Log palette-group loads (and palette state at perfcap dump). |
| `TMC_TRACE_BEAM` | src/manager/templeOfDropletsManager.c | Log Temple of Droplets sunbeam manager state every 120 frames. |
| `TMC_TRACE_INTRO` | src/script.c | Log script enable/disable-player-control commands. |
| `TMC_TAKEOVER_WD` | src/cutscene.c | Enable the cutscene takeover watchdog fallback. |
| `XDG_RUNTIME_DIR` | port/port_discord_rpc.c | Standard; searched for the Discord IPC socket. |
| `TMPDIR` | port/port_discord_rpc.c, port/port_glslp_parser.cpp | Standard temp dir; Discord socket search and `.glslp` shader temp files. |

## Headless capture and scripted input

| Variable | File(s) | Purpose |
|---|---|---|
| `TMC_CAPTURE_FRAME` | port/port_bios.c | Frame number at which to save a PNG and exit (needs `TMC_CAPTURE_OUT`). |
| `TMC_CAPTURE_OUT` | port/port_bios.c | Output PNG path for `TMC_CAPTURE_FRAME`. |
| `TMC_TEST_INPUT` | port/port_bios.c | Scripted input: `tick:button[,tick:button...]`, e.g. `600:start,720:r`. |
| `TMC_REPRO_QUICKSAVE_ROUNDTRIP` | port/port_quicksave.c, port/port_repro_npc_talk.c | Quicksave/load round-trip check; boots into a room via the NPC-talk harness **(harness)**. |
| `TMC_REPRO_QUICKSAVE_FRAMES` | port/port_quicksave.c | Frames to replay for the quicksave round-trip. |

## Repro harnesses

| Variable | File(s) | Purpose |
|---|---|---|
| `TMC_REPRO_NPC_TALK` | port/port_repro_npc_talk.c | **(harness)** Boot into a room, walk to the nearest NPC and talk; PASS = message box. |
| `TMC_REPRO_NPC_TALK_WARP` | port/port_repro_npc_talk.c | **(harness)** Target room for the NPC-talk harness. |
| `TMC_REPRO_WORLD_UNLOCK` | port/port_repro_npc_talk.c | **(harness)** Set story-progress flags so a warped room behaves like mid-game. |
| `TMC_REPRO_KEEP_SAVE` | port/port_repro_npc_talk.c | **(harness)** Use the first real on-disk save instead of a synthesized one. |
| `TMC_REPRO_MASH_A` | port/port_repro_npc_talk.c | **(harness)** Press A every 40 frames. |
| `TMC_REPRO_MASH_B` | port/port_repro_npc_talk.c | **(harness)** Also press B while mashing (sword). |
| `TMC_REPRO_HOLD_DIR` | port/port_repro_npc_talk.c | **(harness)** Hold a direction (`u`/`d`/`l`/`r`) every frame. |
| `TMC_REPRO_NOCLIP` | port/port_repro_npc_talk.c | **(harness)** Enable noclip after the warp. |
| `TMC_REPRO_FUSE` | port/port_repro_npc_talk.c | **(harness)** Run the live kinstone fusion for this kinstone ID. |
| `TMC_VOXEL_TOUR` | port/port_repro_npc_talk.c | **(harness)** Warp through every warpable room (3D view QA). |
| `TMC_VOXEL_TOUR_START` | port/port_repro_npc_talk.c | **(harness)** First room index for the tour. |
| `TMC_VOXEL_TOUR_COUNT` | port/port_repro_npc_talk.c | **(harness)** Number of rooms to tour. |
| `TMC_VOXEL_TOUR_DIR` | port/port_repro_npc_talk.c | **(harness)** Directory for per-room 3D and 2D PNGs. |
| `TMC_VOXEL_TOUR_DUMP` | port/port_repro_npc_talk.c | **(harness)** Also dump the special map data per room. |
| `TMC_REPRO_ITEMGET` | port/port_repro_itemget.c | **(harness)** Boot into the forge and trigger an item-get cutscene for profiling. |
| `TMC_REPRO_ITEMGET_ITEM` | port/port_repro_itemget.c | **(harness)** Item ID to award. |
| `TMC_REPRO_ITEMGET_DUMP` | port/port_repro_itemget.c | **(harness)** Dump path for message state during the cutscene. |
| `TMC_REPRO_ROLL_MACRO` | port/port_repro_roll_macro.c | **(harness)** End-to-end test of the roll-attack macro. |
| `TMC_REPRO_ROLL_MACRO_WARP` | port/port_repro_roll_macro.c | **(harness)** Target room for the roll-macro test. |
| `TMC_REPRO_A11Y` | port/port_repro_a11y.c | **(harness)** Accessibility surroundings-scan self-test. |
| `TMC_REPRO_RANDO` | port/port_repro_rando.c | **(harness)** End-to-end randomizer check (file-select overlay, generation, item overrides). |
| `TMC_REPRO_RANDO_MISSING_SIDECAR` | port/port_repro_rando.c | **(harness)** Randomizer check variant with the seed sidecar missing. |
| `TMC_PERFCAP` | port/port_repro_perfcap.c, port/port_bios.c, port/port_ppu.cpp | **(harness)** Drive into gameplay and dump a PPU snapshot for the render microbench; also forces legacy pacing. |
| `TMC_PERFCAP_AT_FRAME` | port/port_repro_perfcap.c | **(harness)** Frame at which to dump. |
| `TMC_PERFCAP_WARP` | port/port_repro_perfcap.c | **(harness)** Target room for perfcap. |
| `TMC_PERFCAP_DUMP` | port/port_repro_perfcap.c | **(harness)** Snapshot path (default `/tmp/tmc_ppu_snapshot.bin`). |
| `TMC_ROOMCAP` | port/port_repro_roomcap.c, port/port_bios.c | **(harness)** Warp to a room and save its framebuffer; also forces legacy pacing. |
| `TMC_ROOMCAP_WARP` | port/port_repro_roomcap.c | **(harness)** Target room for roomcap. |
| `TMC_ROOMCAP_SETTLE` | port/port_repro_roomcap.c | **(harness)** Frames to wait after the warp before capturing (default 300). |
| `TMC_ROOMCAP_OUT` | port/port_repro_roomcap.c | **(harness)** Output PNG path (default `roomcap.png`). |
| `TMC_ROOMCAP_SAVE` | port/port_repro_roomcap.c | **(harness)** Write a quicksave at the warped spot and exit. |
| `TMC_ROOMCAP_MSG` | port/port_repro_roomcap.c | **(harness)** Open message `idx[,posY]` before capturing. |
| `TMC_ROOMCAP_MSG_ADVANCE` | port/port_repro_roomcap.c | **(harness)** Tap A to page through that message, stopping at a choice. |
| `TMC_ROOMCAP_FILESELECT` | port/port_repro_roomcap.c | **(harness)** Capture the file-select screen instead of a room. |
| `TMC_ROOMCAP_FSEL_SERIES` | port/port_repro_roomcap.c | **(harness)** Dump file-select OAM every 4 frames, 12 samples. |
| `TMC_ROOMCAP_FSEL_ANIMTRACE` | port/port_repro_roomcap.c | **(harness)** Count file-select animation calls over a few frames. |
| `TMC_ROOMCAP_DUMP` | port/port_repro_roomcap.c | **(harness)** Base path for file-select OAM dumps. |
| `TMC_ROOMCAP_COF_PROBE` | port/port_repro_roomcap.c | **(harness)** Scripted flag probe after the warp. |
| `TMC_ROOMCAP_LEVER_PROBE` | port/port_repro_roomcap.c | **(harness)** Scripted lever/flag probe after the warp. |
| `TMC_ROOMCAP_PAN_PROBE` | port/port_repro_roomcap.c | **(harness)** Camera-pan probe for widescreen room borders. |
| `TMC_ROOMCAP_TOWN_PROBE` | port/port_repro_roomcap.c | **(harness)** Hyrule Town flag probe stage (integer). |
