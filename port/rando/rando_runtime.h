/*
 * port/rando/rando_runtime.h — MinishMaker `!eventdefine` runtime features.
 *
 * The logic engine (rando_logic) parses `!eventdefine` entries; this module
 * makes the game honor them natively:
 *  - Rando_Runtime_OnNewFile(): one-shot grants applied when a NEW rando
 *    file is committed (start inventory, wind crests, dungeon portals,
 *    instant text). Mutates gSave only; the caller persists the save.
 *  - Rando_Runtime_Refresh(): recomputes cached per-seed runtime state
 *    (damage multiplier, low-health-beep / music mutes). Called whenever a
 *    seed becomes active; the getters also self-refresh when the active
 *    seed changes, so missing a call site is safe.
 */

#ifndef PORT_RANDO_RANDO_RUNTIME_H
#define PORT_RANDO_RANDO_RUNTIME_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Apply new-file grants to gSave. No-op unless Rando_IsActive(). */
void Rando_Runtime_OnNewFile(void);

/* Recompute cached eventdefine-driven runtime state for the active seed. */
void Rando_Runtime_Refresh(void);

/* Incoming-damage multiplier: `dmgMulti` (2/3/4) or `heroMode` (x2);
 * dmgMulti wins when both are defined. 1 when no seed is active. */
int Rando_Runtime_DamageMultiplier(void);

/* `lowHealthBeep - 0` => suppress SFX_LOW_HEALTH (consulted by
 * port_audio_mute.cpp::Port_AudioMute_ShouldSuppress). */
bool Rando_Runtime_MuteLowHealthBeep(void);

/* `no_music` => suppress BGM song requests (same consult point). */
bool Rando_Runtime_MuteMusic(void);

/* Query a chest's localFlag by room property 3 (tile entities list).
 * Returns 0xFF if not found. */
unsigned Rando_GetChestLocalFlag(unsigned area, unsigned room, unsigned chestIndex);
unsigned Rando_GetDungeonKeyCount(unsigned dungeon_idx);
bool Rando_GetDungeonHasBigKey(unsigned dungeon_idx);

#ifdef __cplusplus
}
#endif

#endif /* PORT_RANDO_RANDO_RUNTIME_H */
