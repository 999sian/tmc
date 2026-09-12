# USA / EU / JP verification — 2026-09-12

Checked the current multi-region Linux binary against local retail ROMs whose
SHA1 hashes match `tmc.sha1`, `tmc_eu.sha1`, and `tmc_jp.sha1`. Runtime checks
used isolated temporary working directories, SDL dummy video/audio drivers,
and the software renderer. Existing user saves/configuration were not used.

| Check | USA | EU | JP |
| --- | --- | --- | --- |
| ROM identification and region-specific save filename | Pass | Pass | Pass |
| Audio-enabled startup, frame-180 capture, normal exit | Pass | Pass | Pass |
| Hyrule Town room capture, area 0x07 / room 0 | Pass | Pass | Pass |
| Live tile properties, collision lookup, lily-pad rails | Pass | Pass | Pass |
| Live fusion data, boss reward lists, HUD frame pointers | Pass | Pass | Pass |
| Original forge NPC interaction through real input/message path | Pass | Pass | Pass |
| Dampé interaction, after correcting test button pulses | Pass | Pass | Pass |
| ChuChu arena and live boss entities, with intro already seen | Pass | Pass | Pass |
| 26 extra entity lists checked against actual ROM data | Pass | Pass | Pass |
| Independent audio metadata comparison: 507 song labels | Pass | Pass | Pass |
| Formerly crashing early debug warp, with readiness fix | Pass | Pass | Pass |

Town and boss captures were visually inspected. The boss check asserted area
0x49 / room 0 and live enemy entities with ChuChu ID 0x13 before capturing.
It intentionally skipped the first-entry cutscene using local flag 0x48;
it does not verify the whole fight or boss death.

## Evidence and repeatable checks

- Runtime logs, captures, GDB boss probe, and JSON results:
  `/tmp/tmc-three-region-arbqw5z3/`.
- Longer USA/EU forge interaction logs:
  `/tmp/tmc-forge-long-1k_41a4s/{USA,EU}/run.log`.
- Final Dampé interaction logs:
  `/tmp/tmc-dampe-final-9rkvx3_i/{USA,EU,JP}/run.log`.
- Build: `xmake build -y tmc_pc` passed.
- `python3 tools/region_regression_test.py` passed.
- `python3 tools/sprite_regression_test.py` passed.
- `python3 tools/tests/regional_entity_lists.py` passed with synthetic inputs
  and again with `--usa`, `--eu`, and `--jp` pointing to verified retail ROMs.
- Existing `tools/tests/regional_runtime.gdb` passed against all three ROMs.
- Source review found runtime equivalents for the inspected regional compile
  guards and confirmed EU/JP bypass USA-only asset overrides.

For a normal startup/exit check, run the absolute binary path from a temporary
directory containing links to the staged `assets` directory and `sounds.json`:

```sh
TMC_BASEROM=/absolute/path/to/region.gba TMC_AUTOPLAY=1 \
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
TMC_CAPTURE_FRAME=180 TMC_CAPTURE_OUT=/tmp/region.png \
/absolute/path/to/build/pc/tmc_pc
```

Use `TMC_REPRO_NPC_TALK=1` instead of the capture variables for the forge
interaction check. Allow at least 120 seconds: the first 55-second deadline
was too short for USA/EU. Both passed at frame 4210 (~70 seconds); JP passed
at frame 2890. No game change was needed for that timing difference.

An additional Dampé check initially failed for USA/JP while EU passed. The
test harness stamped R every frame, producing only one GBA `newKeys` edge
before Link reached talking range. It now releases R between presses, as its
original comment intended. Game interaction logic was unchanged. The actual
retest passed at frames 460 (USA), 428 (EU), and 350 (JP). Reproduce with
`TMC_REPRO_NPC_TALK=1` and
`TMC_REPRO_NPC_TALK_WARP=0x22,0x12,0x78,0x70,1`.

## Debug-warp fix found during checking

An early warp could call `DoExitTransition` with no camera target. Coordinates
above 0x3ff mean preserve the camera position, so this dereferenced NULL.
The invalid early call reproduced a SIGSEGV in all three regions.

`Port_DebugAction_Warp` now returns zero until the camera is initialized,
letting callers retry. The existing debug-action test now checks refusal
before camera setup and successful retry afterward. It fails against the
baseline and passes with the fix:

```sh
xmake build -y debug_actions_test
./build/pc/debug_actions_test
```

The original crashing runtime tuple was also repeated unchanged after the fix
in all three regions: warps initially returned zero, retried successfully once
the camera existed, then captured area 0x49 / room 0 and exited zero. Logs are
under each region's `warp-guard` directory in the evidence directory above.

The initially suggested boss coordinates were world coordinates, not the
room-local coordinates expected by the warp API. Correct boss testing used
the curated local spawn (0x80, 0xe0), a valid intro-seen flag, and assertions
on the resulting room and entities. The tooling crash is not evidence of a
normal ChuChu gameplay crash.

The final rebuilt binary was copied to `dist/USA/tmc_pc` (the multi-region
binary despite the staging directory name). Fresh audio-enabled frame-180
startup/capture/exit checks passed with all three ROMs; evidence is under
each region's `final-exit` directory. `git diff --check` passed. No changes
were pushed.

## Coverage limits

These are targeted checks, not complete playthroughs. Combat damage,
first-entry boss cutscenes, boss deaths, every dialogue, GPU output, and
Windows/MSVC or separate single-region builds are not certified by this pass.
Audio was rendered through SDL's dummy device; sound quality was not assessed
by listening. Earlier unresolved GitHub reports remain separately tracked in
`BUG_SWARM_2026-09-12.md`.
