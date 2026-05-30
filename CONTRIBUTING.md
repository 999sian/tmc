# Contributing to Project Picori

Thanks for helping with the Minish Cap PC port! This fork turns the
[zeldaret/tmc](https://github.com/zeldaret/tmc) decompilation into a native PC
game (SDL3 + a software PPU + the agbplay audio engine). The contributions we
most want: **bug fixes** for crashes and rendering/gameplay glitches,
build/packaging improvements, and accessibility / quality-of-life features.

If you want to *decompile* still-undecompiled GBA functions (matching ARM
assembly byte-for-byte), that work belongs upstream at zeldaret/tmc — see
[Decompiling new functions](#decompiling-new-functions) at the bottom.

## Getting set up

- Read the [README](README.md) for the build (`python3 build.py`, or the faster
  `xmake` dev cycle) and [INSTALL.md](INSTALL.md) for build options.
- You need your own ROM at the repo root — it is **not** shipped. USA
  `baserom.gba`, SHA1 `b4bd50e4131b027c334547b4524e2dbbd4227130`.
- There is **no automated test suite.** Verification is interactive play, the
  **F8** warp/debug menu, capturing repros with **F9**, and comparing against
  GBA reference behaviour.

## Source layout

| Path | What it is |
|------|------------|
| `src/` | C decompilation of the GBA game. **Edit carefully** — many functions still pattern-match the ARM original and use `super` (= `&this->base`). |
| `port/` | PC-only glue: main loop, ROM mmap, asset loader, PPU/audio/input bridges, quicksave, bug-report, debug menu. Most port work happens here and in guarded `src/` edits. |
| `libs/ViruaPPU` | Software GBA PPU renderer (submodule). Don't edit directly — see [Renderer changes](#renderer-changes-viruappu). |
| `libs/agbplay_core` | GBA M4A audio engine (LGPL-3.0 submodule). |
| `asm/` | Undecompiled functions, linked alongside `src/` via `linker.ld`. |
| `data/`, `assets/` | GBA data (`.s`) and the extracted runtime asset trees. |

## The one thing to internalise: GBA → PC differences

The port compiles GBA game logic natively for x86-64 — it is **not** an
emulator. Most bugs come from a handful of hardware-behaviour differences.
**When you fix one, guard it behind `#ifdef PC_PORT`** so the code still matches
the GBA original, and comment it referencing the bug.

- **Pointers are 8 bytes, not 4.** Any struct with embedded pointers shifts
  layout on PC. Keep both layouts in sync with the
  `PORT_STATIC_ASSERT_OFFSET/SIZE` macros (`include/global.h`). Enemy
  subclasses that wrap `Entity base` sometimes need an extra 4 bytes of
  `#ifdef PC_PORT` padding (the "#98" pattern — template in
  `src/enemy/eyegore.c`).
- **NULL / out-of-range reads crash here.** On GBA, reading address `0` or an
  OOB table index returned harmless garbage; on PC it SIGSEGVs. Guard the
  deref / clamp the index and early-return — don't "fix" the underlying logic.
  Canonical examples: `src/npc/cat.c` (#91),
  `src/projectile/darkNutSwordSlash.c` (#97).
- **Never dereference a raw GBA address.** ROM/EWRAM/IWRAM pointers
  (`0x08…/0x02…/0x03…`) must be routed through `Port_ResolveRomData`,
  `Port_ReadU16/U32`, or `gEwram[]`/`gIwram[]` (`port/port_rom.*`).

The repo's `CLAUDE.md` has the full catalogue of these bug classes with
`file:line` examples and fix templates — it is the best reference when you hit
a crash, even if you don't use the assistant it's named for.

## Fixing a bug

1. **Reproduce it.** Bug reports come as a `bugreport_*` folder (screenshot,
   `save.bin`, `state.txt`, and on crashes `backtrace.txt` + `maps.txt`). Drop
   `save.bin` next to `tmc_pc` as `tmc.sav` to load the exact state. Press **F9**
   anytime to capture your own bundle.
2. **Locate a crash.** On Linux, resolve `backtrace.txt`'s addresses with
   `addr2line -e build/pc/tmc_pc -fpi <addr − base>` (build the *same* commit),
   then inspect the faulting instruction with
   `objdump -d build/pc/tmc_pc --disassemble=<Function>`.
3. **Fix defensively**, behind `#ifdef PC_PORT`, with a comment naming the bug
   and why the GBA tolerated it.
4. **Build and play-test** the affected area — the F8 warp menu makes this fast.
5. **Commit one fix per commit**, message style `fix(port): <short description>`
   (reference an issue number when there is one).

## Renderer changes (ViruaPPU)

`libs/ViruaPPU` is a submodule, so **direct edits to its source are lost** on
`xmake clean` or a fresh checkout. Port-side PPU changes live as patches in
`port/patches/viruappu-*.patch`, applied (idempotently) at configure time. See
`CLAUDE.md` → *Authoring a new ViruaPPU patch* for the index-staging extraction
recipe and the "test with a **full** `git -C libs/ViruaPPU reset --hard HEAD`,
never a partial checkout" caveat.

## Submitting changes

- Keep PRs focused — one logical change per PR where practical.
- Match the existing code style and the `#ifdef PC_PORT` convention; don't
  reformat unrelated lines.
- Say **how you tested** (which area / repro), since there is no CI test gate.
- For larger work, open an issue first so we can coordinate.

## Decompiling new functions

Matching still-undecompiled ARM functions byte-for-byte happens in the upstream
[zeldaret/tmc](https://github.com/zeldaret/tmc) project — its build produces the
GBA ROM and verifies the SHA1 match, which this PC fork (built for x86-64) can't
do. Upstream's CONTRIBUTING guide walks through a full `asm/*.s` → `src/*.c`
example. When you bring a newly decompiled function into this port, keep the
`#ifdef PC_PORT` / `PORT_STATIC_ASSERT` layout guards intact so it works under
8-byte pointers.
