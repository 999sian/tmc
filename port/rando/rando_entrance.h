/*
 * port/rando/rando_entrance.h — coupled dungeon-entrance shuffle (runtime).
 *
 * The GBA randomizer's "Dungeon Entrance Shuffle" patches ROM transition structs so
 * that each of the 8 shuffleable entrance doors leads to its assigned dungeon
 * and every way OUT of that dungeon (door exit, post-boss green warp,
 * element-get warp, Palace ledge hole) returns to the door it was entered
 * through. The native port performs the same remap at runtime instead of
 * patching data: the engine's transition choke points call these hooks after
 * filling gRoomTransition.player_status and the destination is rewritten in
 * place when (and only when) an active seed carries entrance assignments.
 *
 * No-op when the seed is inactive or no `Items.Entrance.*` assignment exists
 * (vanilla seeds / logic without entrance shuffle).
 */

#ifndef PORT_RANDO_ENTRANCE_H
#define PORT_RANDO_ENTRANCE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* DoExitTransition (src/scroll.c): every exit-list door, the Lake Hylia ToD
 * portal, the Wind Tribe whirlwind into PoW, the ToD waterspout exit and the
 * FoW/Crypt element-get warps all funnel through it. `cur_area` is
 * gRoomControls.area (the room being left). */
void Rando_Entrance_RemapExit(uint8_t cur_area, uint8_t* area, uint8_t* room, int16_t* x, int16_t* y, uint8_t* layer,
                              uint8_t* spawn_type, uint8_t* facing);

/* DoHoleTransition (src/manager/holeManager.c): covers the Palace of Winds
 * entrance-room ledge jump back to the Wind Tribe tower roof. */
void Rando_Entrance_RemapHole(uint8_t cur_area, uint8_t* area, uint8_t* room, uint8_t* layer, int16_t* x, int16_t* y);

/* WarpPoint_Action5 (src/object/warpPoint.c): post-boss green warp
 * (warp_type == 2) out of a dungeon. Blue/red intra-dungeon warps and the
 * overworld warp pads are left untouched (GBA-randomizer parity). */
void Rando_Entrance_RemapGreenWarp(uint8_t cur_area, uint32_t warp_type, uint8_t* area, uint8_t* room, int16_t* x,
                                   int16_t* y);

#ifdef __cplusplus
}
#endif

#endif /* PORT_RANDO_ENTRANCE_H */
