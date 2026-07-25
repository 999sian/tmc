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

/* Active kinstone-fusion map markers (the red checks the pause map shows):
 * writes up to maxPairs (x, y) world-map pixel pairs into outMapXY and
 * returns how many were written, applying the game's own rule to the save
 * bits passed in (fused && marker not retired — the same checks the pause
 * map's marker pass makes, kinstone ids 10..100). Positions are the marker
 * CENTERS, the fusion world events' overworld locations pushed through the
 * pause map's world->map transform. Returns 0 while map/table data isn't
 * ready. fusedKinstones/fusionUnmarked are the 13-byte snapshot arrays,
 * passed through verbatim from gSave.kinstones.
 *
 * Known gap, on the stale-not-cheating side: the game refreshes
 * fusionUnmarked from each event's completion flag only when the pause
 * menu opens (UpdateVisibleFusionMapMarkers, src/common.c) — that flag
 * state isn't in these two arrays, so a fusion reward claimed since the
 * last pause keeps its check until the game's own retire pass next runs.
 * Stale info the player already had, never an unearned reveal. */
int32_t Port_SecondScreenWorldMap_GetFusionMarkers(const uint8_t* fusedKinstones,
                                                   const uint8_t* fusionUnmarked,
                                                   int32_t* outMapXY, int32_t maxPairs);

/* Draws the map's red-check fusion marker sprite (decoded from ROM, the
 * exact art the pause map stamps — a 16x16 frame) at (x, y) top-left,
 * nearest-neighbor scaled: the stamp covers 16*scale pixels a side, so
 * callers center it on a marker pair with x = cx - 8*scale (same for y).
 * Returns 1 if drawn, 0 while the sprite isn't decodable yet — callers
 * simply skip markers that frame. */
int Port_SecondScreenWorldMap_DrawFusionCheck(uint32_t* pixels, int32_t bufW, int32_t bufH,
                                              int32_t stride, int32_t x, int32_t y, int32_t scale);

/* The map screen's own zoom grid: the game's map lets the player put the
 * cursor on a tile and open that tile's enlarged regional map. Resolves a
 * world-map pixel position to the tile under it — outRegion is the id to
 * hand DrawRegion, and (x0,y0)-(x1,y1) is the tile's rect in world-map
 * pixels so callers can outline it like the game's cursor brackets.
 * Returns 1 on a real tile, 0 off-grid or while data isn't ready. */
int Port_SecondScreenWorldMap_GetRegionAt(int32_t mapX, int32_t mapY, int32_t* outRegion,
                                          int32_t* outX0, int32_t* outY0, int32_t* outX1,
                                          int32_t* outY1);

/* Draws one region's enlarged map — the same artwork the game shows after
 * zooming into a tile — fitted into the destination rect, nearest-neighbor.
 * Returns 1 when drawn, 0 while that region's data isn't decodable (caller
 * stays on the world view). */
int Port_SecondScreenWorldMap_DrawRegion(uint32_t* pixels, int32_t bufW, int32_t bufH, int32_t stride,
                                         int32_t dstX, int32_t dstY, int32_t dstW, int32_t dstH,
                                         int32_t region);

/* Where the player marker sits inside a region's enlarged map, in that
 * drawn region's own pixel space (0..w, 0..h as passed to DrawRegion).
 * Returns 1 when the player is inside this region and the position is
 * known, 0 otherwise — callers just omit the marker then. `area`, `areaX`
 * and `areaY` are the same values LocatePlayer takes. */
int Port_SecondScreenWorldMap_LocateInRegion(int32_t region, uint8_t area, int32_t areaX, int32_t areaY,
                                             int32_t dstW, int32_t dstH, int32_t* outX, int32_t* outY);

#ifdef __cplusplus
}
#endif

#endif /* PORT_SECOND_SCREEN_WORLDMAP_H */
