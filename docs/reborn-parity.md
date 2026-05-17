# Minish Cap Reborn — parity status in tmc_pc

Cherry-pick port of features from
[Admentus64/The-Minish-Cap-Reborn](https://github.com/Admentus64/The-Minish-Cap-Reborn).

All implemented features are runtime-togglable via **F8 → Reborn** ribbon
tab. Each has its own `REBORN_FEAT_*` enum + checkbox.

## Implemented (9 / 16)

| Feature | Toggle | Default | Hook |
|---|---|---|---|
| Shells cap 9999 | `REBORN_FEAT_SHELLS_9999` | ON | `src/itemUtils.c::ModShells` |
| L + R → Pegasus Boots | `REBORN_FEAT_EQUIP_LR_BOOTS` | ON | `src/game.c::GameMain_Update` |
| L + SELECT → Ocarina | `REBORN_FEAT_EQUIP_LSELECT_OCARINA` | ON | same handler |
| Skip Ezlo hint on resume | `REBORN_FEAT_NO_EZLO_ON_RESUME` | ON | `src/game.c::GameMain_InitRoom` + just-resumed flag set by F6 quickload + file-select continue |
| Library reopen after Four Sword | `REBORN_FEAT_LIBRARY_REOPEN` | ON | `src/roomInit.c::sub_StateChange_HouseInteriors1_Library1F` |
| Hero Mode (2× damage) | `REBORN_FEAT_HERO_MODE` | OFF | `src/gameUtils.c::ModHealth` |
| Skip Ezlo tutorials | `REBORN_FEAT_SKIP_EZLO_TUTORIALS` | OFF | `src/manager/ezloHintManager.c::EzloHintManager_Action2` |
| Raise figurine min chance (3×) | `REBORN_FEAT_FIGURINE_MIN_RAISED` | ON | `src/object/figurineDevice.c::FigurineDevice_GetChanceBasedOffFigurineCount` |
| Rupee Like overhaul (shield steal + 10t cap) | `REBORN_FEAT_RUPEE_LIKE_OVERHAUL` | OFF | `src/enemy/rupeeLike.c::sub_0802953C` + state-reset on grab-start |

## Not implemented (7) — estimated scope per feature

### L + A/B → use secondary items
**Scope:** ~1–2 days. Reborn extends `Stats::equipped[2]` to
`equipped[4]` (adds `SLOT_LA`, `SLOT_LB`) and adds a `isSecondaryItems`
state flag toggled by holding L. Multiple call sites need updating
(every place that does `gSave.stats.equipped[SLOT_A or B]`). Save
format change requires migration logic so existing saves load
cleanly.

Touches:
- `include/save.h` Stats struct
- `src/playerUtils.c::UpdateActiveItems` (input dispatch)
- All `equipped[]` reads in `src/player.c`, `src/itemUtils.c`, etc.
- Save format version bump + migration

### SELECT-hold in item menu → equip secondary
**Scope:** ~1 day on top of L+A/B. Pause menu state-machine extension
to track "holding SELECT" and target the LA/LB slots on equip.

Touches:
- `src/menu/pauseMenu.c`

### Mirror Shield available earlier
**Scope:** ~half day investigation, ~1 day fix.
The gating in vanilla is an event-script condition (Big Goron only
gives Mirror Shield after a story flag). Reborn likely altered that
flag check. Need to read both `data/scripts/veilFalls/script_BigGoronMirrorShield.inc`
trees and identify the changed precondition.

Touches:
- `data/scripts/veilFalls/script_BigGoronMirrorShield.inc` (or .s)
- Possibly `src/npc/bigGoron.c`

### Joy Butterfly fusion earlier
**Scope:** ~half day investigation. The Joy Butterfly is a kinstone-
fusion reward. Reborn changed an event in the fusion data table or
script to make it available earlier. Need to identify which fusion.

Touches:
- `data/scripts/kinstoneFusion/*.inc`
- Possibly `src/kinstone.c`

### Oracle homemade charm
**Scope:** ~half day. Oracle script change — the "left behind" oracle
offers a charm. Need to find oracle dialogue script and modify the
conditional branch.

Touches:
- `data/scripts/sanctuary/script_Oracle*.inc`
- Possibly NPC dialogue logic

### Improved text boxes
**Scope:** ~1–2 days. Reborn rewrote several re-occurring text
strings. Need to diff each text bank (`text/*.txt` or similar) and
apply changes. Cosmetic improvements; substantial scope because
strings live in `data/text/` per-language.

### EU language restoration
**Scope:** ~1+ week. Reborn restored French/German/Italian/Spanish
text + the language-selection UI. Largest remaining feature.
Requires:
- Importing translated text archives from Reborn
- Bringing back the file-select language-pick screen
- Extracting + repacking the text banks per language
- Verifying char-set rendering

## How to test what's implemented

1. Build: `xmake build -y tmc_pc`
2. Launch tmc_pc
3. Press F8 → "Reborn" tab
4. Each feature is a checkbox with a `(?)` hover-tooltip

## License posture

Implemented features were ported with reference to Reborn's GPL-3.0
source. That makes our port a derivative work of GPL-3.0 code, which
means anyone redistributing tmc_pc binaries with these features
enabled is obligated to also distribute source under GPL-3.0
(consistent with the existing GPL-3.0 obligation from the embedded
randomizer CLI).

The pre-existing tmc_pc port code remains under its existing licence;
only the cherry-picked Reborn-derived blocks are GPL-3.0 themselves.
