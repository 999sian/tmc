/*
 * port/rando/rando_newfile.h — upstream-parity new-file save state data.
 *
 * Named-flag translations of the GBA randomizer's new-game byte tables
 * (see rando_newfile.c for provenance and region notes). Consumed by
 * rando_runtime.c at new-file commit; engine-independent so the offline
 * rando_logic_test can byte-verify the tables without linking the engine.
 */

#ifndef PORT_RANDO_RANDO_NEWFILE_H
#define PORT_RANDO_RANDO_NEWFILE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Composed gSave.flags bit ids (FLAG_BANK_x + per-bank flag), apply with
 * WriteBit(gSave.flags, id). Region-correct by construction. */
const uint16_t* Rando_NewFile_BaselineFlags(size_t* count);
const uint16_t* Rando_NewFile_WorldOpenFlags(size_t* count);

/* openWorld `visit` entry: gSave.areaVisitFlags[0] |= mask (4-byte copy
 * 0xFE 0xFF 0x7F 0x7F onto a zeroed file upstream). */
#define RANDO_NEWFILE_VISIT_MASK 0x7F7FFFFEu

/* Unconditional `map` entry: reveal the world map. gSave.windcrests'
 * lower bits ("used for other things", include/save.h) |= mask. */
#define RANDO_NEWFILE_MAP_REVEAL_MASK 0x0001FFFFu

/* Unconditional `elements` entry: gSave.map_hints |= mask (suppresses the
 * element map-hint pings, subtask MapHint). */
#define RANDO_NEWFILE_MAP_HINTS_MASK 0x001Eu

/* Unconditional `worldmap` entry: initial world-map cursor over Link's
 * house (gSave.saved_status.overworld_map_x/y). */
#define RANDO_NEWFILE_WORLDMAP_X 0x860u
#define RANDO_NEWFILE_WORLDMAP_Y 0xB3Eu

/* `figurines` entry (figurine bit array OR pattern). */
#define RANDO_NEWFILE_FIGURINE_BYTES 18u
extern const uint8_t kRandoNewFileFigurines[RANDO_NEWFILE_FIGURINE_BYTES];

#ifdef __cplusplus
}
#endif

#endif /* PORT_RANDO_RANDO_NEWFILE_H */
