#ifndef BEANSTALKSUBTASK_H
#define BEANSTALKSUBTASK_H

#include "global.h"
#include "map.h"

/*
tiles 0x4000 and above create an entry here
*/
#define MAX_SPECIAL_TILES 0x100
typedef struct {
    u16 tilePosAndLayer; // (layer << 12) | position
    u16 tileIndex;
} SpecialTileEntry;
extern SpecialTileEntry gTilesForSpecialTiles[MAX_SPECIAL_TILES];

typedef struct {
    u16 collision;
    u16 tileIndex;
} struct_080B44D0;

void LoadMapData(MapDataDefinition* dataDefinition);

/**
 * Renders a tileMap with 16x16 tiles into a subTileMap with 8x8 tiles.
 *
 * Takes into account the special tile indicess >= 0x4000 using GetTileSetIndexForSpecialTile.
 */
void RenderMapLayerToSubTileMap(u16* tileMap, MapLayer* mapLayer);

extern void sub_0801AFE4(void);
extern void SetBGDefaults(void);

typedef struct {
    s16 tileIndex;
    s16 tilePosOffset;
} TileData;

/**
 * @brief Sets multiple tiles at once
 *
 * @param tileData [u16 tileIndex, s16 positionOffset], ends with 0xffff
 * @param basePosition the position the offsets in tileData are based on
 * @param layer the tile layer
 */
extern void SetMultipleTiles(const TileData* tileData, u32 basePosition, u32 layer);

#endif // BEANSTALKSUBTASK_H
