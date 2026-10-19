# Repository and open PR review — 20 October 2026

Reviewed all 11 open PRs against their exact recorded heads and scanned high-risk runtime code at master `bd06a6391`, including the local HD dialogue/menu/capture changes. This is a focused review, not an exhaustive audit. No source fixes, merges, settings changes or GitHub comments were made. Reproduction files remain in `/tmp/tmc-pr-review/`; all fault injection used scratch files, never player saves.

## Release 0.9.6 remediation

The findings below are the historical review. All ten have been addressed on the release integration branch:

- R1: atomic checked disk replacement; failed saves propagate to Save & Quit, menus and F-key feedback.
- R2: exact graphics replacement sizes and complete destination allocation bounds.
- R3: sidecar override counts validated independently of logic mode.
- PR1: symlink-safe extraction cleanup.
- PR2: CMake links the embedded converter implementations.
- PR3: versioned runtime cache invalidation and extractor-aware staging fingerprints.
- PR4/PR5: strict tour input validation, capture errors and bounded waits.
- PR6/PR7: upper-layer fade height and full deferred-shadow coordinates.

All 11 reviewed PR heads are integrated. Follow-up review also covered F5 error feedback and the desktop archive payload. Desktop archives and Android startup now stage the public shape/logic defaults. Focused regression tests cover the fixes, including disk-failure injection and ASan/UBSan bounds checks. Full USA packaging, debug/randomizer tests and the two-scene PPU parity gate passed locally. Platform CI and presentation checks are recorded in the release PR. Regional playthroughs and Android device QA remain outside this review.

## Highest-priority runtime findings

### R1 — P1: failed quicksave overwrite destroys the previous disk snapshot

[port_quicksave.c:392](/home/sian/Documents/tmc/port/port_quicksave.c:392), [flush/result handling:410](/home/sian/Documents/tmc/port/port_quicksave.c:410).

Opening the slot with `wb` immediately truncates its previous good snapshot. The writer ignores `fclose` failure, so a final buffered write failure can return success with an incomplete file. The in-memory copy survives until exit, masking the disk loss. This affects manual saves, autosaves and Save & Quit.

Reproduced against the production module with a file-size limit one byte below the existing file size: `writer_return=1`, old valid size 411652, new size 411651, `reload_after_restart=0`. The loader rejects the resulting short payload. Use a sibling temporary file, check write/flush/sync/close, then atomically replace; propagate persistence failure to the UI. Reproducer: `/tmp/tmc-pr-review/quicksave_diskfull.c`.

### R2 — P1: an oversized graphics replacement overwrites native memory

[port_asset_loader.cpp:1509](/home/sian/Documents/tmc/port/port_asset_loader.cpp:1509).

The graphics descriptor's expected size is discarded, and the loader copies the complete replacement file into its fixed destination. A malformed raw graphics mod can overwrite other game data and escape emulated memory. Real group 10 resource `gfx/gfx_358fe0_56x48_4bpp_uncompressed.bin` expects 1280 bytes at `0x02001A40`; a 256 KiB replacement is accepted and crosses the end of `gEwram`.

ASan reproduced a global-buffer-overflow, write size 262144, at line1509 using the production loader and real descriptor destination. Retain/validate resource size and independently validate the full destination span, including remapped map buffers; reject invalid data and use existing fallback behavior. Reproducer: `/tmp/tmc-pr-review/gfx_overflow.cpp`.

### R3 — P1: corrupt randomizer sidecar bypasses bounds validation

[rando_save.c:254](/home/sian/Documents/tmc/port/rando/rando_save.c:254), [unchecked loop:272](/home/sian/Documents/tmc/port/rando/rando_save.c:272).

`logic_override_count` is bounded only when `logic_mode` is true, but string termination iterates that count for every active slot. A correctly sized v8 sidecar with `active=1`, `count=1`, `logic_mode=0`, `logic_override_count=65535` passes validation and writes past the override array and eventually the entire global sidecar. This is a corrupt/imported save-file trigger, not a normal seed-generation failure.

UBSan reports index320 out of bounds; ASan independently reports a global-buffer-overflow write at line273, 41 bytes beyond `sSidecar`. Validate this count independently of mode before either loop, or skip irrelevant fields only after validating the record. Reproducer: `/tmp/tmc-pr-review/rando-corrupt-repro.c`, which includes the existing production sidecar test fixture. The ordinary sidecar tests pass and miss this case.

## Findings introduced by open PRs

### PR1 — P1, PR #217: Cleanup follows an editable-root symlink and deletes target files

[Exact-head source](https://github.com/999sian/tmc/blob/570124599a1ffb687a93fd2298ff81da26e11d77/tools/src/assets_extractor/assets_extractor_api.cpp#L461). The new recursive iterator/removal pre-pass follows a directory symlink supplied as its root. In standalone runtime-only cleanup, an assets_src/usa link to an external directory causes files in that target to be deleted. Previously remove_all(root) removed only the symlink. An isolated C++ harness copying the pre-pass confirmed old sentinel survives=1, new sentinel survives=0. Check symlink_status before traversing and preserve the old nontraversing behavior. Full asset extraction was not run.

### PR2 — P2, PR #216: CMake tools target cannot resolve embedded converter entry points

[Exact-head source](https://github.com/999sian/tmc/blob/e9c28c67646dad025925dd4f303e98f478d5b820/tools/src/asset_processor/util.cpp#L107). check_call now references gbagfx_main and aif2pcm_main, but only xmake links their renamed C implementations. The unchanged CMake asset_processor target has no provider for those symbols; the converters remain separate executables. Update the CMake graph or retain an external-process fallback. Exact-head build definitions were traced; CMake linking was not executed.

### PR3 — P2, PR #218: Preserved regional caches bypass extractor fixes

[Exact-head source](https://github.com/999sian/tmc/blob/874aaed66d3efed717e9dfd1dab2e9191b135f26/build.py#L722). The new staging code restores regional caches even when the build asset fingerprint changed. Runtime validation checks ROM size/mtime, pack mode and builder version3, not that fingerprint. Existing loose caches affected by the duplicate-write bug fixed in #215 can survive a full rebuild and remain accepted. A standalone fixture using the actual runtime validator accepted the preserved v3 cache. Invalidate regional caches on extractor identity changes; at minimum refresh the builder version with output fixes. Combined-PR gameplay was not run.

### PR4 — P2, PR #210: Negative tour resume index reads before plan arrays

[Exact-head source](https://github.com/999sian/tmc/blob/e70144327a1ea55092780685e2fee813615c8f07/port/port_repro_roomcap.c#L234). TMC_ROOMCAP_TOUR_START=-1 is accepted through atoi; only ti>=tn is checked before indexing ta/tr/tx/ty/tl. Validate the full integer and 0<=ti<=tn before indexing. Confirmed through exact-head control flow, without launching the tour.

### PR5 — P2, PR #210: Tour can report success with no captures

[Exact-head source](https://github.com/999sian/tmc/blob/e70144327a1ea55092780685e2fee813615c8f07/port/port_repro_roomcap.c#L340). An unreadable plan becomes an empty successful tour; a PNG failure is logged but the view counter still advances, and completion exits0. A nonexistent output directory therefore produces successful automation with missing artifacts. Fail nonzero on plan/read/write errors and count only successful captures. Source trace, not runtime reproduction.

### PR6 — P2, PR #210: Upper-layer wall fading uses the wrong ground elevation

[Exact-head source](https://github.com/999sian/tmc/blob/e70144327a1ea55092780685e2fee813615c8f07/port/port_voxel.cpp#L1239). GatherFadeActors uses max(0,-z), while the sprite pass raises collision-layer2 by16. An actor standing on upper ground is sent at feetY0 rather than16, so the shader floor-exclusion test can dither the platform as an occluder. The layer offset and ray calculation were traced; GPU visuals were not reproduced. Derive fade coordinates from the same ground/layer transform as sprites.

### PR7 — P2, PR #219: New visible sprite range exceeds deferred-shadow coordinate storage

[Exact-head source](https://github.com/999sian/tmc/blob/0b33c36404918a04de1bcad743368e892d4b4639/port/port_draw.c#L473). Widened culling retains X520 in a supported 384-wide, pitch30 view, but deferred shadows still encode signed10-bit X in a shifted s16. X520 decodes to -504 and is culled while the body remains visible. Camera math places feet(520,-160) at NDC(0.973,0.633), inside the view. Carry full coordinates through deferred shadows. Verified by source and coordinate arithmetic; no GPU screenshot reproduction.

## Per-PR recommendation

“No new defect found” is a review result, not a guarantee or a completed runtime gate.

| PR | Recommendation | Reason |
|---|---|---|
| [#207](https://github.com/999sian/tmc/pull/207) | Merge first | Fixes the existing debug_actions_test link failure; patched standalone test passes locally and recorded CI is green. |
| [#206](https://github.com/999sian/tmc/pull/206) | Rebase after #207 and rerun CI | No introduced defect found. Recorded Linux failure is missing gMapTop/gMapBottom in the test, not launcher logic. |
| [#212](https://github.com/999sian/tmc/pull/212) | Merge after normal platform checks | No introduced defect found; #218 contains this exact commit. |
| [#214](https://github.com/999sian/tmc/pull/214) | Merge after normal platform checks | Binary mode is appropriate for binary dungeon maps. |
| [#215](https://github.com/999sian/tmc/pull/215) | Merge with a runtime cache refresh | Largest-entry duplicate selection matches pak behavior; ensure old regional output is regenerated, especially with #218. |
| [#213](https://github.com/999sian/tmc/pull/213) | Merge after rendering checks | No actionable defect found; #219 contains this commit. |
| [#216](https://github.com/999sian/tmc/pull/216) | Request changes | CMake integration missing (PR2). |
| [#217](https://github.com/999sian/tmc/pull/217) | Request changes | Reproduced symlink-target deletion (PR1). |
| [#218](https://github.com/999sian/tmc/pull/218) | Request changes; merge after #212 | Regional cache invalidation missing (PR3). |
| [#210](https://github.com/999sian/tmc/pull/210) | Request changes | Tour input/error handling and layer elevation (PR4–PR6). |
| [#219](https://github.com/999sian/tmc/pull/219) | Request changes; rebase after #213 | Deferred-shadow overflow (PR7). |

At review time, nine newer PRs (#210 and #212–#219) had no recorded status checks. #207's recorded matrix is green. #206's Linux link failure was traced from its CI log and independently addressed by applying #207's three-line addition to a scratch test copy: `DEBUG-ACTIONS REGRESSION OK`.

Merge order: #207 before rerunning #206; #212 before rebasing #218; #213 before rebasing #219. Do not treat the repeated patches in stacked PRs as independent implementations. #214/#215 are independent correctness changes. Resolve the findings before merging #216/#217/#218/#210/#219.

## Verification and limits

Passed locally during this review: persistence (USA/EU/JP and quicksave/entity ownership), regional data/audio/collision, widescreen engine and scrolling, NPC context, enemy home, allocation, sprites, graphics slots, fuser, randomizer sidecar regression tests, and the scratch #207 debug action test. `git diff --check` was clean before adding this report. Failure reproductions were rerun in scratch directories: both ASan overflows, the quicksave short-write case, the cleanup sentinel case and the cache acceptance fixture.

Reviewed exact PR diffs, build graphs and relevant callers; runtime scan covered save/profile/sidecar, mod/asset/address boundaries, ROM/config and recent presentation changes. No additional confirmed defect was found in the inspected HD/menu integration. Current full-game binaries/configuration were not rebuilt or changed for the review.

Not checked: Windows execution, full asset conversion, CMake link execution, complete GPU/3D runtime, full regional playthroughs, or every source file. The 3D findings are source/math findings. Full performance claims in PR descriptions were not benchmarked. Screenshots, saves and ROM data were not committed or uploaded.

The initially considered #210 global-timeout issue was rejected: that timeout exists only in local scene changes, not the exact PR head. If those local changes are later combined with tour mode, reconcile the global timeout during integration.

## Recorded PR heads

- #206: `252c4b36813510fe5a96d7391724fd8c7b1cb460`
- #207: `03c0f64db4973c8ba7db215f56db4ab39bc97ad9`
- #210: `e70144327a1ea55092780685e2fee813615c8f07`
- #212: `db6bf478410ac523432921e35a51c847f1f5999f`
- #213: `e4135be8beafeef508328ebafe8cb29e5f93715a`
- #214: `ed746657cd493dc38faf37de78af6c49b9a270ff`
- #215: `84c63912091ed8678a20d0e333d736467e7394c5`
- #216: `e9c28c67646dad025925dd4f303e98f478d5b820`
- #217: `570124599a1ffb687a93fd2298ff81da26e11d77`
- #218: `874aaed66d3efed717e9dfd1dab2e9191b135f26`
- #219: `0b33c36404918a04de1bcad743368e892d4b4639`
