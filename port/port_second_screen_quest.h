#ifndef PORT_SECOND_SCREEN_QUEST_H
#define PORT_SECOND_SCREEN_QUEST_H

/*
 * Quest-status screen for the second screen: the pause menu's own quest
 * screen (elements, kinstones, figurines, dungeon items) rebuilt from ROM
 * at runtime so the panel's tab matches what pressing START shows, rather
 * than paraphrasing it in invented rows.
 *
 * Same contract as the other art modules (see port_second_screen_worldmap.h):
 * ROM-constant data only — every live value arrives through the snapshot the
 * caller hands in, and any not-ready path returns 0 so the compositor can
 * fall back and retry next frame.
 */

#include <stdint.h>

#include "port_second_screen_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Draws the quest-status screen fitted into the destination rect of an
 * RGBA8888 buffer (stride in pixels), scaled nearest-neighbor. `tick` drives
 * whatever the real screen animates. Returns 1 when the authentic screen was
 * drawn, 0 while its ROM data isn't decodable yet. */
int Port_SecondScreenQuest_Draw(uint32_t* pixels, int32_t bufW, int32_t bufH, int32_t stride,
                                int32_t dstX, int32_t dstY, int32_t dstW, int32_t dstH,
                                const SecondScreenSnapshot* snap, uint32_t tick);

#ifdef __cplusplus
}
#endif

#endif /* PORT_SECOND_SCREEN_QUEST_H */
