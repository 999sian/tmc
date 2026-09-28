#!/usr/bin/env python3
"""Generate port/port_rom_stubs.c from the three retail ROMs.

The port's gUnk_* ROM-data symbols are filled at startup from the player's own
ROM. Their consumers were written against the USA layout, so for EU/JP each
symbol is located in that ROM (anchored on byte-identical neighbours) and the
words where it differs from USA are recorded as patches: ROM addresses always,
other fields only when the difference is small (table fields such as flag
ordinals). Larger non-address differences (graphics, text, bytes past the end
of the real object) stay region-native. The output contains offsets, addresses
and those small fields only - never ROM content.

    python3 tools/generate_rom_stubs.py --usa USA.gba --eu EU.gba --jp JP.gba
"""
import argparse
import hashlib
import re
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "port" / "port_rom_stubs.c"
INDEX = ROOT / "port" / "port_asset_index.c"
USA_SHA1 = "b4bd50e4131b027c334547b4524e2dbbd4227130"
# Non-address differences above this many bytes are left region-native.
SMALL_DIFF_LIMIT = 64
# Extra ROM tables consumed through their own buffers (USA offset, size).
EXTRA = [("gRomOverlaySizeData", 0x0B2BE8, 240), ("gUnk_080C9044", 0x0C9044, 8)]
EXTERN = {"gUnk_080C9044"}  # defined elsewhere; only filled here


def is_ptr(v):
    return 0x08000000 <= v < 0x0A000000


def word(b, k):
    return int.from_bytes(b[k:k + 4].ljust(4, b"\0"), "little")


def masked_diff(a, b):
    """Differing bytes outside words that are ROM addresses in both images."""
    n = 0
    for k in range(0, len(a), 4):
        wa, wb = a[k:k + 4], b[k:k + 4]
        if wa == wb:
            continue
        if len(wa) == 4 and is_ptr(word(a, k)) and is_ptr(word(b, k)):
            continue
        n += sum(x != y for x, y in zip(wa, wb))
    return n


def read_stubs():
    src = OUT.read_text()
    extra = {e[0] for e in EXTRA}
    found = [(n, int(o, 16), int(s)) for n, s, o in re.findall(r"\{ (\w+), (\d+)u, \{ 0x([0-9A-Fa-f]+)u", src)]
    found += [(n, int(o, 16), int(s)) for n, o, s in re.findall(r"\{ (\w+), 0x([0-9A-Fa-f]+)u, (\d+)u \}", src)]
    ents = [e for e in found if e[0] not in extra]
    if not ents:
        raise SystemExit("no stubs parsed from " + str(OUT))
    return sorted(ents + EXTRA, key=lambda e: e[1])


def ptr_mask(usa, o, n):
    """True for bytes of USA words (ROM-aligned) holding ROM addresses; they differ per region."""
    m = np.zeros(n, bool)
    for k in range((-o) % 4, n - 3, 4):
        if is_ptr(word(usa, o + k)):
            m[k:k + 4] = True
    return m


def mismatch(U, Rm, o, q, n, m):
    """Byte mismatches of USA [o, o+n) vs region [q, q+n), ignoring masked address bytes."""
    return (U[o:o + n] != Rm[q:q + n]) & ~m


def locate(usa, rom, ents, true_size):
    """USA offset -> offset of the same object in `rom`."""
    loc = {}
    for n, o, s in ents:
        b = usa[o:o + true_size[n]]
        if len(b) >= 16 and rom.count(b) == 1:
            q = rom.find(b)
            if abs(q - o) <= 0x40000:
                loc[n] = q
    anchors = sorted((o, loc[n] - o) for n, o, s in ents if n in loc)
    U = np.frombuffer(usa, np.uint8)
    ra = np.frombuffer(rom, np.uint8)
    for n, o, s in ents:
        if n in loc:
            continue
        prev = [sh for ao, sh in anchors if ao <= o]
        nxt = [sh for ao, sh in anchors if ao > o]
        near = prev[-1:] + nxt[:1]
        t = true_size[n]
        m = ptr_mask(usa, o, t)
        best = None
        for sh in range(min(near) - 64, max(near) + 65):
            d = int(mismatch(U, ra, o, o + sh, t, m).sum())
            key = (d, abs(sh - near[0]))
            if best is None or key < best[0]:
                best = (key, o + sh)
        loc[n] = best[1]
    return loc


ANCHOR = 24  # bytes per unique-match anchor window
SWITCH_COST = 12  # mismatched bytes a shift change must save to be taken


def segments(usa, rom, o, s, coarse):
    """[(dst, src, len)] rebuilding USA stub [o, o+s) from `rom`.

    A stub can span several ROM objects, and EU/JP insert or drop entries
    inside tables, so the shift from USA to region varies along the stub.
    Candidate shifts come from unique anchor windows and the coarse location
    (each widened by whole 4-byte entries); a Viterbi pass then picks, per
    byte, the shift that best reproduces USA. USA ROM-address words count as
    matching wherever the region has a ROM address too."""
    lo, hi = max(0, o + coarse - 0x10000), o + coarse + s + 0x10000
    base = {coarse}
    for k in range(0, max(1, s - ANCHOR + 1), 8):
        w = usa[o + k:o + k + ANCHOR]
        if len(w) < ANCHOR or len(set(w)) < 8:
            continue
        q = rom.find(w, lo, hi)
        if q >= 0 and rom.find(w, q + 1, hi) < 0:
            base.add(q - o - k)
    S = np.array(sorted({b + 4 * d for b in base for d in range(-8, 9)}))
    U = np.frombuffer(usa, np.uint8)[o:o + s]
    Rm = np.frombuffer(rom, np.uint8)
    x = np.arange(s)
    m = ptr_mask(usa, o, s)
    msb = (x - ((x + o) % 4)) + 3  # stub-relative MSB byte of each byte's ROM-aligned word
    C = np.empty((len(S), s), np.int32)
    for j, sh in enumerate(S):
        idx = o + x + sh
        ok = (idx >= 0) & (idx < len(rom))
        reg = Rm[np.clip(idx, 0, len(rom) - 1)]
        top = Rm[np.clip(o + msb + sh, 0, len(rom) - 1)]
        C[j] = np.where(ok & m & ((top == 8) | (top == 9)), 0, (reg != U) | ~ok)
    dp = C[:, 0].copy()
    back = np.empty((len(S), s), np.int16)
    back[:, 0] = np.arange(len(S))
    stay_idx = np.arange(len(S), dtype=np.int16)
    for i in range(1, s):
        b = int(dp.argmin())
        sw = dp[b] + SWITCH_COST
        take = sw < dp
        back[:, i] = np.where(take, b, stay_idx)
        dp = np.where(take, sw, dp) + C[:, i]
    j = int(dp.argmin())
    path = np.empty(s, np.int64)
    for i in range(s - 1, -1, -1):
        path[i] = j
        j = back[j, i]
    out, start = [], 0
    for i in range(1, s + 1):
        if i == s or path[i] != path[start]:
            out.append((start, o + start + int(S[path[start]]), i - start))
            start = i
    return out


def patches_for(usa_b, reg_b, bounds):
    """(offset, usa_word, nbytes) for every differing word that should be restored.

    `bounds` splits the stub into ROM objects; each object's non-address
    differences are restored only when small (table fields, not content)."""
    small = set()
    for b0, b1 in zip(bounds, bounds[1:]):
        if masked_diff(usa_b[b0:b1], reg_b[b0:b1]) <= SMALL_DIFF_LIMIT:
            small.update(range(b0, b1))
    out = []
    for k in range(0, len(usa_b), 4):
        wa, wb = usa_b[k:k + 4], reg_b[k:k + 4]
        if wa == wb:
            continue
        ptr = len(wa) == 4 and is_ptr(word(usa_b, k)) and is_ptr(word(reg_b, k))
        if ptr or k in small:
            out.append((k, word(usa_b, k), len(wa)))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--usa", required=True)
    ap.add_argument("--eu", required=True)
    ap.add_argument("--jp", required=True)
    a = ap.parse_args()
    usa = Path(a.usa).read_bytes()
    if hashlib.sha1(usa).hexdigest() != USA_SHA1:
        raise SystemExit("--usa is not the expected USA ROM")
    roms = {1: Path(a.eu).read_bytes(), 2: Path(a.jp).read_bytes()}

    ents = read_stubs()
    idx = {}
    for o, s in re.findall(r"\{ 0x([0-9A-F]+), 0x([0-9A-F]+), \"", INDEX.read_text()):
        o, s = int(o, 16), int(s, 16)
        idx[o] = max(idx.get(o, 0), s)
    true_size = {n: min(s, idx.get(o, s)) for n, o, s in ents}

    segs, patches = [], []
    for r, rom in roms.items():
        loc = locate(usa, rom, ents, true_size)
        native = []
        for i, (n, o, s) in enumerate(ents):
            parts = segments(usa, rom, o, s, loc[n] - o)
            segs += [(i, r, d, sr, ln) for d, sr, ln in parts]
            reg_b = b"".join(rom[sr:sr + ln] for d, sr, ln in parts)
            usa_b = usa[o:o + s]
            bounds = sorted({0, s} | {k - o for k in idx if o < k < o + s})
            patches += [(i, r, k, v, ln) for k, v, ln in patches_for(usa_b, reg_b, bounds)]
            if masked_diff(usa_b[:true_size[n]], reg_b[:true_size[n]]) > SMALL_DIFF_LIMIT:
                native.append(n)
        count = len([p for p in patches if p[1] == r])
        print(f"region {r}: {count} word patches; region-native bytes kept in: {' '.join(native)}")

    lines = [
        "/* AUTO-GENERATED by tools/generate_rom_stubs.py - DO NOT EDIT.",
        " *",
        " * GBA ROM data symbols, filled from the player's ROM by Port_InitRomStubs().",
        " * Consumers expect the USA layout: EU/JP stubs are assembled from segments",
        " * of that ROM, then patches restore USA values (ROM addresses, small table",
        " * fields). Contains no ROM content. */",
        "",
        '#include "port_rom_stubs.h"',
        '#include "port_config.h"',
        "#include <string.h>",
        "",
    ]
    for n, o, s in ents:
        lines.append(f"extern u8 {n}[];" if n in EXTERN else f"u8 {n}[{s}] __attribute__((aligned(4)));")
    lines += ["", "typedef struct {", "    u8* dst;", "    u32 rom_offset; /* USA */", "    u32 size;",
              "} RomStubEntry;", "", "static const RomStubEntry sRomStubs[] = {"]
    for n, o, s in ents:
        lines.append(f"    {{ {n}, 0x{o:06X}u, {s}u }},")
    lines += ["};", "", "/* EU/JP: copy ROM [src, src+len) to stub byte `at`. */", "typedef struct {", "    u16 stub;",
              "    u8 region; /* 1 = EU, 2 = JP */", "    u32 at;", "    u32 src;", "    u32 len;", "} RomStubSegment;",
              "", "static const RomStubSegment sRomStubSegments[] = {"]
    for i, r, d, sr, ln in segs:
        lines.append(f"    {{ {i}, {r}, 0x{d:X}u, 0x{sr:06X}u, {ln}u }},")
    lines += ["};", "", "typedef struct {", "    u16 stub;", "    u8 region;", "    u8 len;",
              "    u32 at;", "    u32 value;", "} RomStubPatch;", "", "static const RomStubPatch sRomStubPatches[] = {"]
    for i, r, k, v, ln in patches:
        lines.append(f"    {{ {i}, {r}, {ln}, 0x{k:X}u, 0x{v:08X}u }},")
    lines += [
        "};",
        "",
        "#define COUNT(a) (sizeof(a) / sizeof((a)[0]))",
        "",
        "static void CopyRom(u8* dst, const u8* rom, u32 romSize, u32 src, u32 len) {",
        "    if (src <= romSize && len <= romSize - src)",
        "        memcpy(dst, rom + src, len);",
        "    else",
        "        memset(dst, 0, len);",
        "}",
        "",
        "void Port_InitRomStubs(const u8* romData, u32 romSize) {",
        "    const u32 r = gRomRegion == ROM_REGION_EU ? 1 : gRomRegion == ROM_REGION_JP ? 2 : 0;",
        "    if (r == 0) {",
        "        for (size_t i = 0; i < COUNT(sRomStubs); i++)",
        "            CopyRom(sRomStubs[i].dst, romData, romSize, sRomStubs[i].rom_offset, sRomStubs[i].size);",
        "        return;",
        "    }",
        "    for (size_t i = 0; i < COUNT(sRomStubSegments); i++) {",
        "        const RomStubSegment* s = &sRomStubSegments[i];",
        "        if (s->region == r)",
        "            CopyRom(sRomStubs[s->stub].dst + s->at, romData, romSize, s->src, s->len);",
        "    }",
        "    for (size_t i = 0; i < COUNT(sRomStubPatches); i++) {",
        "        const RomStubPatch* p = &sRomStubPatches[i];",
        "        if (p->region != r)",
        "            continue;",
        "        for (u32 b = 0; b < p->len; b++)",
        "            sRomStubs[p->stub].dst[p->at + b] = (u8)(p->value >> (8 * b));",
        "    }",
        "}",
        "",
    ]
    OUT.write_text("\n".join(lines))
    print(f"wrote {OUT} ({len(ents)} stubs, {len(segs)} segments, {len(patches)} patches)")


if __name__ == "__main__":
    main()
