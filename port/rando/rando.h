/*
 * port/rando/rando.h — public API for the in-process randomizer.
 *
 * Clean-room C++ rewrite of the C# minishmaker randomizer. Operates
 * by intercepting item-give code paths in src/ and substituting
 * shuffled item IDs at the moment of the give. No ROM bytes are
 * modified, no patch files are written, no save data is touched.
 *
 * The seed lives in process memory only — restarting tmc_pc resets
 * the randomizer to vanilla. The user re-rolls via F8 → Randomizer.
 *
 * See port/rando/README.md for the milestone plan.
 */

#ifndef PORT_RANDO_RANDO_H
#define PORT_RANDO_RANDO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    RANDO_OK = 0,
    RANDO_INACTIVE,    /* no seed rolled yet */
    RANDO_INTERNAL,
} RandoStatus;

/* Roll a new seed. Pass seed=0 to let the engine pick one; the chosen
 * seed is returned via *out_seed if non-null. Subsequent item-give
 * intercepts apply the new permutation. */
RandoStatus Rando_RollSeed(uint32_t seed, uint32_t* out_seed);

/* Disable the randomizer and revert to vanilla item assignments. */
void Rando_Reset(void);

/* True if a seed is currently active. */
bool Rando_IsActive(void);

/* Current seed (0 if inactive). */
uint32_t Rando_GetSeed(void);

/* Generic item-spawn override. Called from the centralized point in
 * src/playerItemUtils.c::GiveItemWithCutscene, which all item-entity
 * spawn paths flow through (chests, NPC gifts, cutscenes, drops, shop
 * gives, kinstone-fusion rewards…).
 *
 * *type and *subtype come in as the vanilla values; if the active
 * seed remaps the item, the function mutates the pointed values.
 * Returns true if anything was rewritten.
 *
 * For M1 only *type matters (pool-shuffle of 8 major items). M2+ may
 * also rewrite *subtype or require additional source context. */
bool Rando_OverrideItem(uint8_t* type, uint8_t* subtype);

/* Copy a UTF-8 spoiler describing the current permutation into `buf`
 * (NUL-terminated). Returns bytes written excluding the NUL. */
size_t Rando_GetSpoiler(char* buf, size_t buflen);

#ifdef __cplusplus
}
#endif

#endif /* PORT_RANDO_RANDO_H */
