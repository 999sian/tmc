#ifndef PORT_SECOND_SCREEN_WORLDMAP_H
#define PORT_SECOND_SCREEN_WORLDMAP_H

/*
 * Hyrule world-map artwork for the second screen, decoded at runtime from
 * the game's own map-screen graphics (ROM/asset layer) — never shipped as
 * baked pixels, same policy as port_second_screen_render.c's item icons.
 *
 * Render-thread safe by construction: the image is built once (lazily) into
 * a private buffer and only published when complete; after that it is
 * immutable. Implementations read only ROM/asset data (port_rom accessors,
 * static decomp tables) — never live engine state (gSave / gArea /
 * gPaletteBuffer); anything save- or room-dependent reaches the caller
 * through SecondScreenSnapshot instead.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Returns the decoded world-map image as RGBA8888 (row-major, w x h), or
 * NULL while ROM/asset data isn't ready yet. Callers fall back to schematic
 * rendering and simply retry next frame — the call is cheap once decoded. */
const uint32_t* Port_SecondScreenWorldMap_GetImage(int32_t* outW, int32_t* outH);

/* Maps an area + area-local player position (pixels) to world-map image
 * pixel coordinates. Returns 1 on success, 0 when the area has no world-map
 * location (interiors, dungeons) — callers keep the last successful fix,
 * zelda3-android's frozen-doorway-marker behavior. */
int Port_SecondScreenWorldMap_LocatePlayer(uint8_t area, int32_t areaX, int32_t areaY,
                                           int32_t* outMapX, int32_t* outMapY);

/* World-map pixel position of a windcrest warp point (WindcrestID bit
 * index, matching gSave.windcrests' upper-byte flags — the caller decides
 * from the snapshot which ones are unlocked and how to draw them). Returns
 * 1 and fills the position when the id is a real windcrest, 0 past the end
 * or while map data isn't ready. Positions come from the same static
 * tables the fast-travel screen (src/subtask/subtaskFastTravel.c) uses. */
int Port_SecondScreenWorldMap_GetWindcrestPin(int32_t windcrestId, int32_t* outMapX, int32_t* outMapY);

#ifdef __cplusplus
}
#endif

#endif /* PORT_SECOND_SCREEN_WORLDMAP_H */
