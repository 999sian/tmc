#pragma once
/*
 * port_voxel.h — experimental 3D ("voxel") view of the room, SDL_GPU only.
 *
 * Phase 1: the bottom map layer is the ground plane, the top map layer floats
 * one tile above it, entity sprites stand up as camera-facing billboards at
 * their ground position (entity z lifts them), shadows lie flat, and HUD
 * sprites + BG0 (text boxes) draw as a flat 2D overlay. All pixels are decoded
 * on the GPU from the live VRAM / palette / OAM / room sub-tile maps, so
 * palette fades, tile animations and SetTile rewrites show up for free.
 *
 * Falls back to the normal 2D present outside room gameplay (title, menus,
 * subtasks, cutscenes that null the map layers).
 */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Per-OAM-slot anchor recorded by port_draw.c while it builds OAM, so the
 * voxel view knows which sprites belong to world entities. The build array
 * tracks gOAMControls; the latched copy is taken with the vblank OAM DMA
 * (Port_Voxel_LatchOamTags) and always matches gOamMem. */
enum {
    PORT_VOXEL_OAM_HUD = 0, /* default: anything not drawn for an entity */
    PORT_VOXEL_OAM_ENTITY,  /* billboard standing at groundY */
    PORT_VOXEL_OAM_DECAL,   /* shadow: flat on the ground */
};
typedef struct {
    uint8_t kind;
    uint8_t layer;   /* entity collisionLayer (2 = top layer, raised) */
    int16_t groundY; /* screen Y of the entity's feet (sprite y minus z) */
} PortVoxelOamTag;
extern PortVoxelOamTag gPortVoxelOamTagsBuild[128];
extern PortVoxelOamTag gPortVoxelOamTags[128];
void Port_Voxel_LatchOamTags(void);
/* Debug: write the next 3D frame (rendered offscreen, 960x540) to `path`. */
void Port_Voxel_RequestShot(const char* path);

#ifdef __cplusplus
}

struct SDL_GPUCommandBuffer;
struct SDL_GPUTexture;
/* Renders the 3D room view + 2D HUD into `swap` (clearing it). Returns false
 * when the voxel view is off or not applicable this frame; the caller then
 * presents the normal 2D frame. Must be called outside any render pass. */
bool Port_Voxel_Present(SDL_GPUCommandBuffer* cmd, SDL_GPUTexture* swap, int swapW, int swapH);

/* Phase 3: per-area tile shape overrides for the height heuristic, edited from
 * F8 and persisted to voxel_shapes.json next to config.json. */
enum { PORT_VOXEL_SHAPE_AUTO = 0, PORT_VOXEL_SHAPE_FLOOR, PORT_VOXEL_SHAPE_BLOCK, PORT_VOXEL_SHAPE_PROP };
struct PortVoxelTileAhead {
    bool valid;
    int area;
    int tileType; /* bottom-layer tile type of the tile Link faces */
    int shape;
};
PortVoxelTileAhead Port_Voxel_TileAhead(void);
void Port_Voxel_SetTileShape(int area, int tileType, int shape);
int Port_Voxel_AreaWallTiles(int area); /* front-wall rows per solid run, 1..4 */
void Port_Voxel_SetAreaWallTiles(int area, int tiles);
int Port_Voxel_CurrentArea(void); /* -1 outside room gameplay */
void Port_Voxel_Shutdown(void);
#endif
