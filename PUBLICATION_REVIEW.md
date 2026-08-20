# Publication review — human checklist before anything goes public

Staging prepared from the development worktree; last updated 2026-08-20 after
the final technical checkpoint. **Nothing has been pushed, committed, tagged
or released.**

**TECHNICALLY READY: YES** — the binary is built, audited and complete.
**PUBLICATION READY: NO** — one licensing gate remains (see below). This is a
legal/provenance question only a human can settle; it does not indicate any
defect in the build.

---

## ✅ Resolved since the previous review

**X/Y shortcut inversion — FIXED.** `kDefaults` bound `soft_x` to SDL `WEST`
(physically Y) and `soft_y` to `NORTH` (physically X). Corrected to
`soft_x` = NORTH, `soft_y` = WEST, matching the A/B entries, the rebind picker
(`kPadChoices`) and the Ezlo shortcut, all of which already used the Nintendo
layout. A narrow one-time migration repairs an old `config.json` (details in
the final report / source comments). Removed from Known Issues; noted in the
CHANGELOG. **Still needs a hardware button-press confirmation** — see the
checklist.

---

## ⛔ REMAINING GATE — source publication / license provenance

The **binary** in `release/` contains no ROM and no user data and is safe on
those grounds. The open question is whether publishing it obliges you to
publish corresponding source, and under what terms.

What release preparation could actually verify from files and upstream repos:

| Component | Finding |
|---|---|
| Project Picori (999sian/tmc) | **GPL-3.0** (stated by upstream). This tree's port layer descends from it. |
| samyost1/tmc-android | **GPL-3.0** (LICENSE present upstream). Second-screen lineage. |
| EstebanPdN/zelda-tmc-3ds | **GPL-3.0** (stated by upstream). Reference. |
| HayatoG/tmc (the direct base fork, `origin`) | **No LICENSE file, no GitHub-displayed license.** Its README licenses only `libs/agbplay_core` (LGPL-3.0) and explicitly says the rest is *not* relicensed by that. Whole-repo terms are undefined. |
| ViruaPPU, VirtuaAPU (vendored submodules) | Upstream is **MatheoVignaud/VirtuaPPU** and **/VirtuaAPU** — **no LICENSE file and no GitHub-displayed license on either.** Note Project Picori is itself a fork of `MatheoVignaud/tmc`. |
| agbplay_core | LGPL-3.0 (text in `LICENSES/`). |
| rcheevos | MIT (text in `LICENSES/`). |

**The practical position.** If the GPL-3.0 upstreams (Picori / tmc-android)
contributed code that is present in this tree — which the lineage strongly
indicates — then distributing the compiled binary carries a GPL-3.0
obligation to make the corresponding source available under GPL-3.0. That
obligation is *not* discharged by keeping `source/` withheld.

At the same time, two components in the tree (the HayatoG base and the
Virtua* submodules) have **no stated license at all**, which means nobody has
granted redistribution rights for them. Publishing them anyway would be
guessing, and this project does not guess about other people's code.

These two facts pull in opposite directions, which is exactly why this is a
human decision and why `source/` is still not staged.

**Options, in order of safety:**

1. **Ask upstream.** Open a short issue on `HayatoG/tmc` and
   `MatheoVignaud/VirtuaPPU` / `VirtuaAPU` asking them to state a license.
   This is cheap, respectful, and resolves the question properly. Best option.
2. **Publish source with per-origin notices** once (1) answers: your own
   Switch-specific work under GPL-3.0 (compatible with the lineage), upstream
   files under their stated terms, submodules referenced by URL/commit rather
   than vendored, with `THIRD_PARTY_NOTICES.md` preserved.
3. **Hold the binary release** until (1) resolves. Choose this if you want
   zero risk.
4. Publishing the binary while withholding source indefinitely is the option
   this document recommends **against**, given the GPL-3.0 ancestry.

Do not apply a blanket license over third-party code, and do not remove any
upstream notice, whichever option you take.

---

## Items intentionally excluded from this staging

- **`source/`** — pending the gate above.
- User data: `config.json`, `tmc.sav`, `autosave.bin`, `tmc.softslots`,
  `ra_token`, `ra_badges/`, crash logs, debug dumps, `rom_data/` page dumps.
- The ROM and every ROM-derived artifact (the `assets/` cache).
- The debug **ELF** — kept privately in `artifacts/` for symbolizing crash
  reports; users do not need it and it is not part of the release.

## Optional polish (explicitly NOT blockers)

- All supplied captures include the on-screen **FPS overlay**. Fine to ship;
  retake with SHOW FPS off only if you want cleaner marketing shots (filenames
  are stable, docs reference them by name).
- No **CONFIG-menu screenshot** exists in the supplied set (the `07_config`
  role is unfilled). Optional.

---

## Checklist

Checked = objectively verified by source/build audit. Unchecked = needs a
human or real hardware.

### Verified by audit
- [x] Final NRO built from the current worktree, RELEASE=1, REGION=USA
- [x] X/Y default binding corrected in source and present in the built object
- [x] A/B, ZL/ZR, Ezlo, Quick Display bindings unchanged
- [x] Release binary is quiet (no `startup.log` / `clock.log` / `tmc.log` /
      `perf.log` / boot-profile strings)
- [x] No ROM bundled (13.18 MiB binary; a 16 MiB ROM cannot fit) and no
      generated ROM asset cache embedded
- [x] No RA token, no credentials, no IP addresses, no developer filesystem
      paths, no save data in the binary
- [x] Correct icon embedded byte-for-byte
- [x] Version string V0.1.0 present
- [x] Feature strings present: Action Hint, Autosave, Load Autosave, Return to
      Title, RA, Story Guide, all five UI languages, NORMAL/DUAL/FLIP
- [x] No true-widescreen code accidentally introduced
- [x] `SHA256SUMS.txt` generated for the released NRO
- [x] No private data anywhere in this staging directory
- [x] No copyrighted ROM content anywhere in this staging directory

### Confirmed by the maintainer
- [x] Deepwood barrel rendering fix — confirmed corrected on real hardware

### Needs the maintainer
- [ ] README English reviewed
- [ ] README Spanish reviewed
- [ ] Screenshots reviewed
- [ ] Credits reviewed (roles match your understanding of the lineage)
- [ ] **Licensing gate decided (see above)**
- [ ] ROM instructions verified against the shipped binary
- [ ] Final NRO hardware tested (see the 15-step checklist in the report)
- [ ] Physical X fires the X shortcut; physical Y fires the Y shortcut
- [ ] ZL / ZR shortcuts still work
- [ ] NORMAL tested
- [ ] DUAL tested
- [ ] FLIP 270 tested
- [ ] Action Hint OFF / CONTEXTUAL / ON tested
- [ ] Version row shows V0.1.0
- [ ] RetroAchievements tested (status or one unlock)
- [ ] Autosave + Load Autosave tested
- [ ] Return to Title tested
- [ ] Title screen verified after the mode2 fix
- [ ] Startup still feels fast (Fast Boot preserved)

## Notes for the release itself

- Suggested tag `v0.1.0`; title “Alek's Ultimate NX Edition v0.1.0”.
- Release body can be assembled from `CHANGELOG.md` plus the README install
  short-version.
- **No push / release until the licensing gate is settled and the hardware
  boxes are checked by a human.**
