# Minish Cap Reborn — parity status in tmc_pc

Cherry-pick port of features from
[Admentus64/The-Minish-Cap-Reborn](https://github.com/Admentus64/The-Minish-Cap-Reborn).

All runtime-toggleable features are at **F8 → Reborn** ribbon tab.

## Implemented (13 / 15)

| # | Feature | Toggle | Default | Hook |
|---|---|---|---|---|
| 1 | Shells cap 9999 | `REBORN_FEAT_SHELLS_9999` | ON | `src/itemUtils.c::ModShells` |
| 2 | L + R → Pegasus Boots | `REBORN_FEAT_EQUIP_LR_BOOTS` | ON | `src/game.c::GameMain_Update` |
| 3 | L + SELECT → Ocarina | `REBORN_FEAT_EQUIP_LSELECT_OCARINA` | ON | same handler |
| 4 | L + A/B → secondary item | `REBORN_FEAT_SECONDARY_LAB` | ON | `src/playerUtils.c::UpdateActiveItems`. Backed by SLOT_LA / SLOT_LB overlaid on `Stats::filler14[0..1]` (no save format change). |
| 5 | SELECT-hold in item menu → equip secondary | `REBORN_FEAT_SELECT_HOLD_EQUIP` | ON | `src/menu/pauseMenu.c` |
| 6 | Skip Ezlo hint on resume | `REBORN_FEAT_NO_EZLO_ON_RESUME` | ON | `src/game.c::GameMain_InitRoom` + just-resumed flag |
| 7 | Library reopen after Four Sword | `REBORN_FEAT_LIBRARY_REOPEN` | ON | `src/roomInit.c` |
| 8 | Hero Mode (2× damage) | `REBORN_FEAT_HERO_MODE` | OFF | `src/gameUtils.c::ModHealth`. Surfaced via F8 toggle instead of Reborn's file-select-L. |
| 9 | Skip Ezlo tutorials | `REBORN_FEAT_SKIP_EZLO_TUTORIALS` | OFF | `src/manager/ezloHintManager.c` |
| 10 | Raise figurine min chance (3×) | `REBORN_FEAT_FIGURINE_MIN_RAISED` | ON | `src/object/figurineDevice.c` |
| 11 | Rupee Like overhaul | `REBORN_FEAT_RUPEE_LIKE_OVERHAUL` | OFF | `src/enemy/rupeeLike.c` |
| 12 | Mirror Shield earlier | (no toggle — additive) | always | `data/scripts/veilFalls/script_BigGoron3.inc` — flag swap GAMECLEAR → EZERO_1ST |
| 13 | Oracle left-behind also offers charm | (no toggle — additive) | always | `data/scripts/hyruleTown/script_{Nayru,Farore,Din}Alone.inc` — branch into MovedIn charm paths |

## Not implemented (2)

### Joy Butterfly fusion earlier
**Status:** *Couldn't locate the change.* Diffed `src/kinstone.c`,
`data/scripts/kinstoneFusion/`, and `data/scripts/sanctuary/` — no
clear fusion-content diff found. Possibly buried in a fusion table
that lives outside the script tree. Would need targeted investigation.

### Improved text boxes
**Status:** Cosmetic; large scope. Reborn rewrote multiple recurring
text strings — those live in compiled text banks (`data/strings.s`
indirects into per-language `.bin` files). Diffing the asm shows only
build-system simplification, not content. A proper port would replace
specific entries in the text bank; not attempted.

### EU language restoration (excluded from the count)
Reborn re-introduced French / German / Italian / Spanish text + the
file-select language picker. That's a separate localization project
(~1+ week of data work). Out of scope.

## Notes

### License posture
Features 4, 5, 10, 11, 12, 13 were ported with reference to Reborn's
GPL-3.0 source. Those specific blocks are derivative works of GPL-3.0
code. Features 1, 2, 3, 6, 7, 8, 9 were implemented clean-room from
the upstream README before source authorization. Anyone redistributing
tmc_pc binaries with these features is bound by GPL-3.0 (consistent
with the existing GPL-3.0 obligation from the embedded randomizer CLI).

### Save format
No save format change. SLOT_LA / SLOT_LB storage overlays onto
`Stats::filler14[0..1]` which was always 0 in vanilla saves — loading
a legacy save just gives you no items bound to the secondary slots
until you set them via SELECT-hold equip.

### Differences from upstream Reborn
- **Hero Mode** is toggleable mid-run via F8 instead of being chosen
  on file select. Functionally equivalent; the file-select-L trigger
  in Reborn relies on a save-format flag we didn't need to add.
- **Skip Ezlo tutorials** is a runtime gate on
  `EzloHintManager_Action2` rather than removing the hint entries
  from `data/map/entity_headers.s`. Same observable behavior;
  reversible at runtime.
- **L + A/B secondary items** uses `Stats::filler14[0..1]` instead of
  extending `Stats::equipped` to 4 entries. Avoids save-format change.

## How to test

1. Build: `xmake build -y tmc_pc`
2. Launch tmc_pc
3. F8 → "Reborn" tab — each feature is a checkbox with `(?)` tooltip
4. **Mirror Shield**: enter Veil Falls + interact with Big Goron
   right after meeting Ezlo (instead of needing GAMECLEAR)
5. **Oracle charm**: with only one oracle moved into the house, talk
   to the others "left behind" — they'll now offer the charm
6. **L + A/B**: in the pause menu, hold SELECT while pressing A/B on
   an item to bind it to LA/LB. In-game hold L + tap A or B to use.
