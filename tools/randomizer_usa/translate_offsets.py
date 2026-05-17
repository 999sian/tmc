#!/usr/bin/env python3
"""
EU→USA offset translator for randomizer event patches.

Parses an EventAssembler .event file, finds every `ORG $hex` directive,
and emits a translated copy where each EU offset has been replaced by
the corresponding USA offset.

Strategy (in priority order):

  1. Known-pair table: hand-curated EU→USA offsets we've verified
     (see KNOWN_PAIRS below). Pull-requestable as confidence grows.

  2. Decomp symbol-map matching: if both build/USA/tmc.map and a
     build/EU/tmc.map are available (zeldaret-tmc decomp), look up the
     EU symbol covering the offset, then find that symbol's USA address.
     This is the bulk-translation engine but requires an EU decomp build.

  3. Byte-pattern matching: read 32 bytes from the EU ROM at the EU
     offset, search for the same 32-byte sequence in the USA ROM. Use
     the match position as the USA offset. Fast, but only reliable for
     bytes inside fixed code/data — text/script regions are often
     rearranged.

  4. Anchor-delta interpolation: if the EU offset is within a small
     window of a known anchor, assume the same EU→USA delta applies.
     Useful for offsets that cluster around the HeaderTable anchors
     (Chunk0, Chunk1, Chunk2, etc. are very close together) but not
     for arbitrary distant offsets — the delta is NOT a smooth
     function across the whole ROM (we've observed 0x8A4, 0x8AC, 0x8B8,
     0xAB0, 0xAC8, 0xAF8 in different ranges).

  5. Unresolved: emit the EU offset unchanged with a `// UNRESOLVED`
     comment so the patch is auditable.

The patches still won't apply cleanly until ALL ORGs in ALL patches
resolve to USA — this script is the per-file building block; the
randomizer-wide migration is tracked in tools/randomizer_usa/README.md.
"""

import argparse
import os
import pathlib
import re
import sys


# Known EU→USA offset pairs (curated from
# RandomizerCore/Core/Constants.cs::HeaderTable, which already lists
# both regions for the 14 high-level header fields). Format: EU → USA.
KNOWN_PAIRS = {
    0x11D95C: 0x11E214,   # MapHeaderBase
    0x0D4828: 0x0D50FC,   # AreaMetadataBase
    0x5A23D0: 0x5A2E80,   # TileOffset
    0x0FED88: 0x0FF850,   # PaletteSetTableLoc
    0x107AEC: 0x108398,   # Chunk0TableLoc
    0x1077AC: 0x108050,   # Area1Chunk0TableLoc
    0x107B02: 0x1083AE,   # Chunk1TableLoc
    0x107B18: 0x1083C4,   # Chunk2TableLoc
    0x107B5C: 0x108408,   # SwapBase
    0x107940: 0x1081E4,   # PaletteChangeBase
    0x107800: 0x1080A4,   # Area1SwapBase
    0x101BC8: 0x10246C,   # GlobalTileSetTableLoc
    0x323FEC: 0x324AE4,   # GfxSourceBase
    0x1027F8: 0x10309C,   # GlobalMetaTileSetTableLoc
    0x1070E4: 0x107988,   # GlobalTileDataTableLoc
}


ORG_RE = re.compile(r"\bORG\s*(\$([0-9A-Fa-f]+)|0x([0-9A-Fa-f]+)|([0-9]+))\b")

# POIN $hex — EventAssembler pointer-write directive. The literal is a
# ROM address that gets written into the ROM as a 32-bit pointer (with
# the GBA 0x08000000 base added at assembly time). These addresses
# also need EU→USA translation. Only translate `$hex >= 0x1000` to
# avoid touching small literals.
POIN_RE = re.compile(r"\bPOIN\s*\$([0-9A-Fa-f]{4,})\b")


def parse_map(map_path):
    """Parse an ldscript-style .map file into {symbol: rom_offset}."""
    sym_to_addr = {}
    addr_to_sym = {}
    if not pathlib.Path(map_path).is_file():
        return sym_to_addr, addr_to_sym
    sym_re = re.compile(r"\s+0x([0-9A-Fa-f]+)\s+([A-Za-z_][A-Za-z0-9_]*)$")
    with open(map_path, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            m = sym_re.search(line)
            if not m:
                continue
            addr = int(m.group(1), 16)
            sym = m.group(2)
            if addr < 0x08000000 or addr >= 0x0A000000:
                continue
            rom_off = addr - 0x08000000
            sym_to_addr[sym] = rom_off
            addr_to_sym.setdefault(rom_off, sym)
    return sym_to_addr, addr_to_sym


def find_byte_pattern(haystack, needle, start=0):
    """Naive substring search; returns -1 if not found."""
    return haystack.find(needle, start)


def nearest_anchor(eu_offset):
    """Return (anchor_eu, delta) of the KNOWN_PAIRS entry closest to eu_offset,
    or (None, None) if KNOWN_PAIRS is empty."""
    if not KNOWN_PAIRS:
        return None, None
    best = min(KNOWN_PAIRS.keys(), key=lambda a: abs(a - eu_offset))
    return best, KNOWN_PAIRS[best] - best


# Confidence windows around known anchors for interpolation.
# Below ~0x2000 bytes from an anchor: HIGH confidence (we've observed
# that addresses inside the same data table generally share the same
# delta). Beyond that the delta is observed to drift in 0x4-0x250 byte
# steps, so it's only useful as a guess to bisect later.
ANCHOR_TIGHT_WINDOW   = 0x2000
ANCHOR_LOOSE_WINDOW   = 0x20000


def translate_offset(
    eu_offset,
    eu_map_sym_to_addr=None,
    eu_map_addr_to_sym=None,
    usa_map_sym_to_addr=None,
    eu_rom=None,
    usa_rom=None,
    probe_bytes=32,
    allow_interp=True,
):
    """Return (usa_offset_or_None, source_tag)."""
    # 1. Known pair
    if eu_offset in KNOWN_PAIRS:
        return KNOWN_PAIRS[eu_offset], "known"

    # 2. Symbol-map matching: find the EU symbol whose address is the
    # largest one <= eu_offset; record its delta; look up the same
    # symbol in the USA map and apply the delta.
    if eu_map_addr_to_sym and usa_map_sym_to_addr:
        addrs = sorted(a for a in eu_map_addr_to_sym.keys() if a <= eu_offset)
        if addrs:
            base = addrs[-1]
            sym = eu_map_addr_to_sym[base]
            if sym in usa_map_sym_to_addr:
                delta = eu_offset - base
                return usa_map_sym_to_addr[sym] + delta, f"symmap:{sym}+0x{delta:x}"

    # 3. Byte-pattern probe (only when both ROMs are loaded)
    if eu_rom and usa_rom and eu_offset + probe_bytes <= len(eu_rom):
        needle = eu_rom[eu_offset:eu_offset + probe_bytes]
        pos = find_byte_pattern(usa_rom, needle)
        if pos >= 0 and find_byte_pattern(usa_rom, needle, pos + 1) < 0:
            return pos, f"pattern:{probe_bytes}"

    # 4. Anchor-delta interpolation (last-resort heuristic).
    if allow_interp:
        anchor, delta = nearest_anchor(eu_offset)
        if anchor is not None:
            dist = abs(eu_offset - anchor)
            if dist <= ANCHOR_TIGHT_WINDOW:
                return eu_offset + delta, f"interp:tight(±0x{dist:x} from $0x{anchor:X})"
            if dist <= ANCHOR_LOOSE_WINDOW:
                return eu_offset + delta, f"interp:loose(±0x{dist:x} from $0x{anchor:X})"

    return None, "unresolved"


def translate_event_file(in_path, out_path, **lookup_args):
    text = pathlib.Path(in_path).read_text(encoding="utf-8", errors="replace")
    stats = {"hit": 0, "miss": 0, "by_source": {}}

    def repl_org(m):
        if m.group(2) is not None:
            eu_off = int(m.group(2), 16)
            raw = f"${m.group(2)}"
        elif m.group(3) is not None:
            eu_off = int(m.group(3), 16)
            raw = f"0x{m.group(3)}"
        else:
            eu_off = int(m.group(4), 10)
            raw = m.group(4)

        usa_off, source = translate_offset(eu_off, **lookup_args)
        stats["by_source"][source] = stats["by_source"].get(source, 0) + 1
        if usa_off is None:
            stats["miss"] += 1
            return f"ORG {raw}  // UNRESOLVED for USA"
        stats["hit"] += 1
        return f"ORG ${usa_off:X}  // EU {raw} → USA via {source}"

    def repl_poin(m):
        eu_off = int(m.group(1), 16)
        usa_off, source = translate_offset(eu_off, **lookup_args)
        stats["by_source"][f"POIN/{source}"] = stats["by_source"].get(f"POIN/{source}", 0) + 1
        if usa_off is None:
            stats["miss"] += 1
            return f"POIN ${m.group(1)}  /* UNRESOLVED for USA */"
        stats["hit"] += 1
        return f"POIN ${usa_off:X}  /* EU ${m.group(1)} → USA via {source} */"

    out_text = ORG_RE.sub(repl_org, text)
    out_text = POIN_RE.sub(repl_poin, out_text)
    pathlib.Path(out_path).parent.mkdir(parents=True, exist_ok=True)
    pathlib.Path(out_path).write_text(out_text, encoding="utf-8")
    return stats


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("input_event")
    ap.add_argument("output_event")
    ap.add_argument("--eu-rom",  help="path to EU baserom.gba (optional, enables byte-pattern fallback)")
    ap.add_argument("--usa-rom", help="path to USA baserom.gba (optional, enables byte-pattern fallback)")
    ap.add_argument("--eu-map",  help="path to EU decomp tmc.map (optional, enables symbol matching)")
    ap.add_argument("--usa-map", help="path to USA decomp tmc.map (optional, enables symbol matching)")
    args = ap.parse_args()

    kwargs = {}
    if args.eu_map and args.usa_map:
        eu_s2a, eu_a2s = parse_map(args.eu_map)
        usa_s2a, _ = parse_map(args.usa_map)
        if eu_s2a and usa_s2a:
            kwargs["eu_map_sym_to_addr"] = eu_s2a
            kwargs["eu_map_addr_to_sym"] = eu_a2s
            kwargs["usa_map_sym_to_addr"] = usa_s2a
            print(f"loaded EU map ({len(eu_s2a)} syms), USA map ({len(usa_s2a)} syms)", file=sys.stderr)
    if args.eu_rom and args.usa_rom:
        if not pathlib.Path(args.eu_rom).is_file() or not pathlib.Path(args.usa_rom).is_file():
            print("warning: one or both ROMs missing; byte-pattern fallback disabled", file=sys.stderr)
        else:
            kwargs["eu_rom"]  = pathlib.Path(args.eu_rom).read_bytes()
            kwargs["usa_rom"] = pathlib.Path(args.usa_rom).read_bytes()
            print(f"loaded EU ROM ({len(kwargs['eu_rom'])} B), USA ROM ({len(kwargs['usa_rom'])} B)", file=sys.stderr)

    stats = translate_event_file(args.input_event, args.output_event, **kwargs)
    print(f"\n{args.input_event} → {args.output_event}")
    print(f"  hit:  {stats['hit']}")
    print(f"  miss: {stats['miss']}")
    for k, v in stats["by_source"].items():
        print(f"    {k}: {v}")


if __name__ == "__main__":
    main()
