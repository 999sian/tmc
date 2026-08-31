# Alek's Ultimate NX Edition v1.3.1

Hotfix. Two players reported Cave of Flames lava platforms still missing after
v1.3.0 — one on B2, one in a room where the Gust Jar and the Cane of Pacci
stopped working as well. Both were right, and both are fixed here.

**If you play Cave of Flames, update.** Nothing else changed.

## What was still wrong

v1.3.0 fixed one truncated data structure. It turned out to be one of five.

The asset index is generated from the decompilation's `.incbin` directives, so
a symbol whose data is interleaved with pointer words gets split across several
index entries — and the entry at the symbol's own address only covers its first
fragment. v1.3.0 handled the shape where the symbol *opens* with a pointer:

```asm
gUnk_additional_8_CaveOfFlames_Rollobite:: @ 080E09DC
	.4byte gUnk_080E09FC
	.incbin "...Rollobite.bin"
```

It did not handle the shape where the pointers are interleaved further in,
which is what B2's platforms use:

```asm
gUnk_additional_8_CaveOfFlames_BossDoor:: @ 080E15C4
	.incbin "...BossDoor.bin"        @ 32 bytes
	.4byte gUnk_080E1674
	.incbin "...BossDoor_1.bin"
	...                              @ eight more times
gUnk_080E1674:: @ 080E1674           @ the symbol is really 176 bytes
```

Ten platforms recorded as two.

The rule was wrong, not just the table. A symbol's true extent is **the
distance to the next symbol** — nothing to do with which fragment comes first.
Under that rule there are **five** affected symbols across the ROM, not two.
Two of them are outside Cave of Flames entirely, in Hyrule Town and near Lon
Lon Ranch.

## Why items stopped working

This is the part worth explaining, because it looked like an unrelated bug.

Every entity comes from one shared pool of 72 slots. When the spawner ran off
the end of its truncated buffer it created platform after platform out of
unrelated memory until it happened to hit a stop byte. That consumed the pool —
so the real platform never appeared, and then the Gust Jar and the Cane of
Pacci had no slot left to spawn their effects into either.

One defect, three symptoms, and the items were the clue that identified it.

v1.3.0 capped that runaway at 64 entries, which stopped the memory reads but
was still nearly the whole 72-slot pool — it would not have saved your items.
The cap is now derived from the pool size itself (9 of 72) so it cannot drift
into the failure it exists to prevent again.

## Still no need to touch your assets

As in v1.3.0, the fix reaches you in the binary: the game falls back to the ROM
whenever a room property is shorter than the structure it holds, and notes it
in `/switch/tmc/assetfix.log`. You do not need to delete or regenerate
anything.

A build-time check now re-derives the whole table from the decompilation and
fails the build if a sixth symbol of this shape ever appears.

## Thanks

To the two reporters on GBAtemp. The second report — the one that mentioned
items breaking in the same room — is what turned a guess into a diagnosis.

## Verification

    SHA-256  35397c8836022de5df087a7d458dd4760e82ac6a46e72d7c3a112d432320f6d4
             tmc_aleks_ultimate_nx_v1.3.1.nro
    Size     14202547 bytes
