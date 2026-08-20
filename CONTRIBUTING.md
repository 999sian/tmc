# Contributing

Thanks for your interest — a few honest ground rules first.

## What kind of project this is

Alek's Ultimate NX Edition is a **personal project**. Contributions and forks
are welcome, but the maintainer does not promise timely reviews, active issue
triage, or long-term responsiveness. If a pull request or issue sits for a
while, it is not personal — see the maintenance expectations in the
[README](README.md).

If you want to move faster than this repository does: **fork it**. That is a
fully encouraged outcome, not a fallback.

## Licensing ground rules

This tree stands on upstream projects with their own licenses (GPL-3.0
lineage plus differently-licensed libraries — see
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and
[CREDITS.md](CREDITS.md)). Any contribution or fork must respect those
upstream licenses and preserve their notices. Do not submit code you are not
allowed to license compatibly. Never submit ROMs, ROM-derived assets, or
copyrighted Nintendo content.

## Bug reports

Good bug reports are genuinely valued. Please include:

- **Hardware context**: Switch model, firmware, CFW version (e.g. Atmosphère
  x.y.z), application vs applet mode, handheld vs docked.
- **Port context**: version (CONFIG → SYSTEM → VERSION), display mode
  (NORMAL / DUAL / FLIP), relevant settings.
- **Reproduction steps**: where in the game, what you did, what you expected,
  what happened. A photo or capture helps enormously.
- Whether it reproduces after a restart, and whether it happens in NORMAL as
  well as DUAL/FLIP.

## Pull requests

- Keep changes focused; one topic per PR.
- Say what hardware testing you did (or that you could not test on hardware —
  that is fine to state, but say it).
- Match the existing code style of the files you touch.
- Do not bundle unrelated reformatting.

## Feature requests

Welcome, but read the maintenance expectations first: this project follows
the maintainer's own play priorities. A feature request that comes with a
fork implementing it will always be more persuasive than a wish.
