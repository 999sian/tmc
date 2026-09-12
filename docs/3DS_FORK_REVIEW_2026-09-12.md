# 3DS fork comparison — 2026-09-12

Reviewed [EstebanPdN/zelda-tmc-3ds](https://github.com/EstebanPdN/zelda-tmc-3ds)
through [4669942 / v1.3-E17](https://github.com/EstebanPdN/zelda-tmc-3ds/commit/4669942141fdcd5996e062f9a31a1b50a10bbc67),
including recent fix history and issue reports. The fork was inspected in a
separate temporary checkout. No GitHub comments or issue changes were made.

## Fixes adapted into this PC port

### Retry NPC initialization when graphics allocation fails

Source: [3b05a85](https://github.com/EstebanPdN/zelda-tmc-3ds/commit/3b05a85eeba2f9fd053ec7ceb875040d3d25db9b).

`NPCInit` ignored failures from both graphics loaders, then published the NPC
as initialized. `NPCUpdate` could execute its scripts, animation, and draw
without valid graphics. Native builds now leave initialization incomplete and
retry on the next frame before dispatching the NPC. GBA behavior is preserved
behind `PC_PORT`.

The regression runs the real initializer and dispatcher with forced failures
and later successful allocations across USA/EU/JP. It failed before the change
and passes afterward, including palette/update/draw ordering and steady-state
behavior. This proves the allocation bug; it does not establish that every
Goron report in the fork has this same cause.

### Correct native graphics-slot compaction

Source: [396dc7b](https://github.com/EstebanPdN/zelda-tmc-3ds/commit/396dc7bcbe22280de236511e19c7a3570e99ca58).

- The final graphics slot was excluded from the occupied-slot search.
- Compaction remapped allocated entities belonging to suspended gameplay,
  even though that scene's graphics table was saved separately. It now follows
  the current entity lists and remaps only their associated extra sprites.
- EU native allocations now use the corrected compaction/retry/upload paths,
  avoiding false failures when sufficient space is fragmented.

The regression covers full and fragmented tables, final-slot relocation,
fixed and swap allocation retry, reserved slots, invalid bounds, OAM/extra
sprite relocation, and preservation of suspended scene state. It fails against
the baseline and passes with the changes for USA/EU/JP. Retail GBA allocator
behavior is preserved.

## Relevant fixes already present or not transferable

| Fork work | Finding here |
| --- | --- |
| Horizontal Minish-path buffer alias and scrolling bounds | Already fixed. The fork's layout/scroll regression passed against this port with adapted memory stubs. |
| Affine state restoration across subtasks | Already fixed. The adapted lifecycle test passed 256 cycles. |
| EU Vaati/Gyorg sprite conversion | Current regional sprite assignment tests cover these boundaries and pass. |
| Mushroom allocation, Gleerok fire hitboxes, Vaati defeat recovery | Equivalent fixes already present. |
| Latest lantern GPU command overflow | Specific to Citro3D/PICA200 command buffers; this port uses SDL rendering and does not have that buffer path. |
| Old 3DS audio scheduling, texture uploads, second-screen state | Hardware/platform-specific; not copied. |
| Legacy EU bomb inventory and wall-fusion recovery | Targeted old-save migrations; not applied without establishing affected PC save provenance. |
| EU/JP collected fusion markers | Fork changes retail completion-marker behavior; not included in these allocation fixes. |

The legacy wall-fusion state was reproducible in isolation: with 0x29 already
fused, saved progress 2 and offer 0x25 can skip the unfinished 0x2A reward when
0x25 is completed. This establishes the inconsistent-state consequence, not
that a current PC workflow creates it. The fork's exact-state repair remains a
candidate for a separate, backup-preserving migration if an affected save is
available.

Regional baseline dialogue-flag wrappers and fusion-text accessors also merit
future runtime comparison; they were not established as new failures in this
pass. Open fork reports about Vaati, bullets, and ChuChu were not treated as
proof that an applicable fix exists.

## Verification

- `xmake build -y tmc_pc`: passed.
- `python3 tools/allocation_regression_test.py`: passed, also with UBSan.
- `python3 tools/gfx_regression_test.py`: passed, also with ASan/UBSan.
- Existing regional, sprite-assignment, and fusion-cursor regressions: passed.
- Independent review found no actionable issue in the combined changes.

The existing allocation fixtures cannot all link with ASan enabled because
its global instrumentation retains unrelated engine sections for which the
isolated fixtures provide no stubs. The normal and UBSan runs pass; graphics
compaction's separate fixture supports both ASan and UBSan.

Actual USA/EU/JP ROM runs all passed:

- Opening cutscene through Smith interaction and live message activation,
  exercising scene suspension/restoration and NPC graphics in the game.
- Audio-enabled startup, frame-180 capture, and clean normal exit.

Logs and JSON results: `/tmp/tmc-3ds-fixes-final-_y5fs3ky/`. Runs used isolated
temporary directories, SDL dummy video/audio, and the software renderer.
No full playthrough, GPU-specific validation, or listening-based audio check
was performed.

The verified binary was copied to `dist/USA/tmc_pc`; it supports all three
regions despite the staging directory name. The staged copy's hash matches
the tested binary. `git diff --check` passed. No changes were pushed.
