#include "asm.h"
#include "collision.h"
#include "effects.h"
#include "entity.h"
#include "map.h"
#include "object.h"
#include "physics.h"
#include "player.h"
#include "port_gba_mem.h"
#include "room.h"
#include "sound.h"

#include <stdint.h>

extern const u8 gUnk_08007DF4[];
extern const KeyValuePair gUnk_080046A4[];
extern const u16 gUnk_080047F6[];
extern const KeyValuePair gMapActTileToSurfaceType[];
extern u32 CalcDistance(s32 x, s32 y);
extern u32 sub_0806F58C(Entity* a, Entity* b);
extern Entity* gCollidableList[];
extern u8 gCollidableCount;

u8* DoTileInteractionOffset(Entity* entity, u32 interaction, s32 xOffset, s32 yOffset);

extern u8 gUnk_02024048;
u16 gUnk_02021F20[8];

typedef struct {
    u16 flags;
    u8 objectId;
    u8 objectType;
    u16 _pad;
    u16 tileChange;
} TileInteractionDef;

static bool32 FindEntryForKeyInternal(u32 key, const KeyValuePair* list, u16* outValue) {
    if (list == NULL) {
        if (outValue != NULL) {
            *outValue = 0;
        }
        return 0;
    }

    for (; list->key != 0; list++) {
        if (list->key == key) {
            if (outValue != NULL) {
                *outValue = list->value;
            }
            return 1;
        }
    }

    if (outValue != NULL) {
        *outValue = 0;
    }
    return 0;
}

u32 sub_08000E44(s32 value) {
    if (value == 0) {
        return 0;
    }
    return (value < 0) ? (u32)-1 : 1;
}

u32 sub_08000E62(u32 value) {
    value = (value & 0x55555555U) + ((value >> 1) & 0x55555555U);
    value = (value & 0x33333333U) + ((value >> 2) & 0x33333333U);
    value = (value & 0x0F0F0F0FU) + ((value >> 4) & 0x0F0F0F0FU);
    value = (value & 0x00FF00FFU) + ((value >> 8) & 0x00FF00FFU);
    value = value + (value << 16);
    return value >> 16;
}

u32 FindValueForKey(u32 key, const KeyValuePair* keyValuePairList) {
    u16 value;
    return FindEntryForKeyInternal(key, keyValuePairList, &value) ? value : 0;
}

void EnqueueSFX(u32 sfx) {
    u8 count = gUnk_02024048;
    if (count < (u8)ARRAY_COUNT(gUnk_02021F20)) {
        gUnk_02024048 = count + 1;
        gUnk_02021F20[count] = (u16)sfx;
    }
}

void SoundReqClipped(Entity* entity, u32 sfx) {
    if (CheckOnScreen(entity)) {
        SoundReq(sfx);
    }
}

u32 sub_080040A2(Entity* entity) {
    u8 rawSpriteSettings = *(u8*)&entity->spriteSettings;
    if (rawSpriteSettings & 0x2) {
        return 1;
    }
    return CheckOnScreen(entity);
}

static u32 ReadCollisionBitAtPosition(Entity* entity, const u16* slopeTable, u32 worldX, u32 worldY) {
    u32 roomX = (u16)worldX - gRoomControls.origin_x;
    u32 roomY = (u16)worldY - gRoomControls.origin_y;
    u32 tilePos = ((roomX & 0x3F0) >> 4) + ((roomY & 0x3F0) << 2);
    u32 collision = GetCollisionDataAtTilePos(tilePos, entity->collisionLayer);

    if (collision < 0x10) {
        if ((roomY & 0x8) == 0) {
            collision >>= 2;
        }
        if ((roomX & 0x8) == 0) {
            collision >>= 1;
        }
        return collision & 1;
    }

    if (collision == 0xFF) {
        return 1;
    }

    if (slopeTable == NULL) {
        return 0;
    }

    u16 bits = slopeTable[collision - 0x10];

    u32 xf = roomX & 0xF;
    if (xf >= 4) {
        bits >>= 1;
        if (xf >= 8) {
            bits >>= 1;
            if (xf >= 12) {
                bits >>= 1;
            }
        }
    }

    u32 yf = roomY & 0xF;
    if (yf >= 4) {
        bits >>= 4;
        if (yf >= 8) {
            bits >>= 4;
            if (yf >= 12) {
                bits >>= 4;
            }
        }
    }

    return bits & 1;
}

u32 sub_080040D8(Entity* entity, const u16* slopeTable, s32 worldX, s32 worldY) {
    return ReadCollisionBitAtPosition(entity, slopeTable, (u32)worldX, (u32)worldY);
}

u32 sub_080040E2(Entity* entity, const u16* slopeTable) {
    return ReadCollisionBitAtPosition(entity, slopeTable, (u16)entity->x.HALF.HI, (u16)entity->y.HALF.HI);
}

void SnapToTile(Entity* entity) {
    entity->x.WORD = (entity->x.WORD & ~0x000FFFFF) + 0x00080000;
    entity->y.WORD = (entity->y.WORD & ~0x000FFFFF) + 0x00080000;
}

void sub_0800417E(Entity* entity, u32 collisions) {
    u32 direction = entity->direction;
    if (collisions & 0xEE00) {
        direction = 0x20 - direction;
    }
    if (collisions & 0x00EE) {
        direction = 0x10 - direction;
    }
    entity->direction = direction & 0x1F;
}

u32 sub_0800419C(Entity* a, Entity* b, u32 xRange, u32 yRange) {
    if (xRange != 0) {
        s32 tmp = (s32)a->x.HALF.HI - (s32)b->x.HALF.HI + (s32)xRange;
        if ((xRange << 1) < (u32)tmp) {
            return 0;
        }
    }

    if (yRange != 0) {
        s32 tmp = (s32)a->y.HALF.HI - (s32)b->y.HALF.HI + (s32)yRange;
        if ((yRange << 1) < (u32)tmp) {
            return 0;
        }
    }

    return 1;
}

u32 sub_080041DC(Entity* entity, u32 x, u32 y) {
    return CalcDistance((s32)entity->x.HALF.HI - (s32)x, (s32)entity->y.HALF.HI - (s32)y);
}

u32 sub_080041E8(s32 x1, s32 y1, s32 x2, s32 y2) {
    return CalcDistance(x1 - x2, y1 - y2);
}

static u32 CheckEntityPickup(Entity* source, Entity* target, u32 xRange, u32 yRange) {
    Hitbox* hb = (Hitbox*)port_resolve_addr((uintptr_t)target->hitbox);
    if (hb == NULL) {
        return 0;
    }

    u32 rangeX = xRange + (u8)hb->width;
    u32 rangeY = yRange + (u8)hb->height;

    s32 targetX = (s32)(u16)target->x.HALF.HI + hb->offset_x;
    if ((*(u8*)&target->spriteSettings & 0x4) != 0) {
        targetX = (s32)(u16)target->x.HALF.HI - hb->offset_x;
    }
    s32 targetY = (s32)(u16)target->y.HALF.HI + hb->offset_y;

    if ((source->collisionLayer & target->collisionLayer) != 3) {
        if (rangeX != 0) {
            u32 xDelta = (u32)((s32)(u16)source->x.HALF.HI - targetX + (s32)rangeX);
            if ((rangeX << 1) < xDelta) {
                return 0;
            }
        }
        if (rangeY != 0) {
            u32 yDelta = (u32)((s32)(u16)source->y.HALF.HI - targetY + (s32)rangeY);
            if ((rangeY << 1) < yDelta) {
                return 0;
            }
        }
    }

    return 1;
}

u32 sub_08003FDE(Entity* source, Entity* target, u32 xRange, u32 yRange) {
    u32 picked = CheckEntityPickup(source, target, xRange, yRange);
    if (picked) {
        sub_0806F58C(source, target);
    }
    return picked;
}

// Advance tilePos by one tile in the direction given by animationState,
// then look up the tile type at that position.
// tilePos is a BYTE offset into the mapData u16 array (i.e. element_index * 2).
// On GBA, sub_08004212 leaves the modified tilePos in r2 and the tile in r1.
// sub_08004202 returns the modified tilePos (r2) and writes the tile to *outTileType.
static u32 AdvanceTilePos(u32 animationState, u32 tilePos) {
    if ((animationState & 3) != 0) {
        tilePos += (animationState & 4) ? (u32)-2 : 2;
    }

    if ((animationState & 3) != 2) {
        tilePos += (((animationState + 1) & 4) != 0) ? 0x80 : (u32)-0x80;
    }

    return tilePos & 0x1FFF;
}

static u16 LookupTileType(Entity* entity, u32 tilePos) {
    MapLayer* layer = GetLayerByIndex(entity->collisionLayer);
    u16 tile = layer->mapData[tilePos >> 1]; // tilePos is byte offset, convert to element index
    if ((tile & 0x4000) == 0) {
        tile = layer->tileTypes[tile];
    }
    return tile;
}

u32 sub_08004212(Entity* entity, u32 animationState, u32 tilePos) {
    tilePos = AdvanceTilePos(animationState, tilePos);
    return LookupTileType(entity, tilePos);
}

u32 sub_08004202(Entity* entity, u8* outTileType, u32 tilePos) {
    tilePos = AdvanceTilePos(entity->animationState, tilePos);
    u32 tileType = LookupTileType(entity, tilePos);
    *(u32*)outTileType = tileType;
    return tilePos; // Return modified tilePos (matches GBA r2 behavior)
}

static void CreateHazardFX(Entity* entity, u32 fxType) {
    Entity* fx = CreateObject(SPECIAL_FX, fxType, 0);
    if (fx != NULL) {
        fx->x.HALF.HI = entity->x.HALF.HI;
        fx->y.HALF.HI = entity->y.HALF.HI;
        fx->z.HALF.HI = entity->z.HALF.HI;
        if (entity->kind == ENEMY) {
            fx->type2 = 1;
        }
    }
    DeleteEntity(entity);
}

void CreateDrownFX(Entity* entity) {
    CreateHazardFX(entity, FX_WATER_SPLASH);
}

void CreateLavaDrownFX(Entity* entity) {
    CreateHazardFX(entity, FX_LAVA_SPLASH);
}

void CreateSwampDrownFX(Entity* entity) {
    CreateHazardFX(entity, FX_GREEN_SPLASH);
}

void CreatePitFallFX(Entity* entity) {
    CreateHazardFX(entity, FX_FALL_DOWN);
}

void UpdateCollision(Entity* entity) {
    if ((entity->flags & ENT_COLLIDE) == 0) {
        return;
    }
    if (gCollidableCount < MAX_ENTITIES) {
        gCollidableList[gCollidableCount] = entity;
        gCollidableCount++;
    }
}

s32 DoItemTileInteraction(Entity* entity, u32 interaction, ItemBehavior* behavior) {
    const s8* offs = (const s8*)&gUnk_08007DF4[entity->animationState & 6];
    u8* entry = (u8*)DoTileInteractionOffset(entity, interaction, offs[0], offs[1]);

    if (entry != NULL) {
        u8* rawBehavior = (u8*)behavior;
        rawBehavior[3] = entry[2];
        rawBehavior[7] = entry[3];
        rawBehavior[8] = entry[5];
        return 1;
    }

    return 0;
}

u8* DoTileInteractionOffset(Entity* entity, u32 interaction, s32 xOffset, s32 yOffset) {
    u32 x = (u16)entity->x.HALF.HI + xOffset;
    u32 y = (u16)entity->y.HALF.HI + yOffset;
    return (u8*)DoTileInteraction(entity, interaction, x, y);
}

u32* DoTileInteractionHere(Entity* entity, u32 interaction) {
    return (u32*)DoTileInteraction(entity, interaction, (u16)entity->x.HALF.HI, (u16)entity->y.HALF.HI);
}

u16* DoTileInteraction(Entity* entity, u32 interaction, u32 worldX, u32 worldY) {
    if (gRoomControls.reload_flags == 1) {
        return NULL;
    }

    u32 tileType = GetTileTypeAtWorldCoords((s32)worldX, (s32)worldY, entity->collisionLayer);
    u16 entryIndex;
    if (!FindEntryForKeyInternal(tileType, gUnk_080046A4, &entryIndex)) {
        return NULL;
    }

    TileInteractionDef* entry = (TileInteractionDef*)((u8*)gUnk_080047F6 + ((u32)entryIndex << 3));
    if ((((u32)entry->flags >> interaction) & 1U) == 0) {
        return NULL;
    }

    u8 objectId = entry->objectId;
    u8 objectType = entry->objectType;

    if (objectId != 0xFF && interaction != 6 && interaction != 0xE && interaction != 0xA && interaction != 0xB &&
        !(interaction == 0xD && objectId == SPECIAL_FX && objectType == FX_GRASS_CUT)) {
        u32 objectType2 = (objectId == SPECIAL_FX) ? 0x80 : 0;
        Entity* created = CreateObject((Object)objectId, objectType, objectType2);
        if (created != NULL) {
            if (objectId != 0) {
                created->x.HALF.HI = ((u16)worldX & ~0xF) + 8;
                created->y.HALF.HI = ((u16)worldY & ~0xF) + 8;
            } else {
                created->x.HALF.HI = entity->x.HALF.HI;
                created->y.HALF.HI = entity->y.HALF.HI;
                created->z.HALF.HI = entity->z.HALF.HI;
            }

            created->parent = entity;
            created->collisionLayer = entity->collisionLayer;
            UpdateSpriteForCollisionLayer(created);
        }
    }

    u32 tileX = ((u16)worldX - gRoomControls.origin_x) >> 4;
    u32 tileY = ((u16)worldY - gRoomControls.origin_y) >> 4;
    u32 tilePos = tileX + (tileY << 6);
    u32 layer = entity->collisionLayer;
    u16 tileChange = entry->tileChange;

    if (tileChange & 0x4000) {
        if (tileChange == 0xFFFF) {
            RestorePrevTileEntity(tilePos, layer);
        } else {
            GetLayerByIndex(layer)->mapData[tilePos] = tileChange;
        }
    } else {
        sub_0807B7D8(tileChange, tilePos, layer);
    }

    return (u16*)entry;
}

u32 CheckNEastTile(Entity* entity) {
    u32 actTile = GetActTileRelativeToEntity(entity, 0, 0);
    if (actTile & 0x4000) {
        return 0;
    }

    u16 surfaceType;
    if (!FindEntryForKeyInternal(actTile, gMapActTileToSurfaceType, &surfaceType)) {
        return 0;
    }

    return (surfaceType == 1) ? 1 : 0;
}

u32 PlayerCheckNEastTile(void) {
    return CheckNEastTile(&gPlayerEntity.base);
}

static const s8 sVelocities1[] = { 0, -3, 3, -3, 3, 0, 3, 3, 0, 3, -3, 3, -3, 0, -3, -3 };
static const s8 sIceVelocities[] = { 0, -10, 10, -10, 10, 0, 10, 10, 0, 10, -10, 10, -10, 0, -10, -10 };
static const s8 sVelocities3[] = { 0, 6, -6, 0, 0, -6, 6, 0 };

static void ClampPlayerVelocityAxis(s16* axis) {
    if (*axis > 0x180) {
        *axis = 0x180;
    } else if (*axis < -0x180) {
        *axis = -0x180;
    }
}

static void AddPlayerVelocity(s32 x, s32 y) {
    s16 vx = (s16)gPlayerState.vel_x;
    s16 vy = (s16)gPlayerState.vel_y;
    vx = (s16)(vx + x);
    vy = (s16)(vy + y);
    ClampPlayerVelocityAxis(&vx);
    ClampPlayerVelocityAxis(&vy);
    gPlayerState.vel_x = (u16)vx;
    gPlayerState.vel_y = (u16)vy;
}

static void DampPlayerVelocityAxis(s16* axis) {
    if (*axis >= 0) {
        *axis -= 3;
        if (*axis < 0) {
            *axis = 0;
        }
    } else {
        *axis += 3;
        if (*axis > 0) {
            *axis = 0;
        }
    }
}

static void ApplyIceVelocityCore(Entity* entity, u32 direction, bool32 setDirectionFromState) {
    if (setDirectionFromState) {
        if (gPlayerState.field_0x7 != 0 || gPlayerState.field_0xa != 0) {
            return;
        }
        entity->direction = (u8)direction;
        if (direction & 0x80) {
            goto apply_movement;
        }
    }

    if (gPlayerState.heldObject == 1 || gPlayerState.heldObject == 2) {
        ResetPlayerVelocity();
        return;
    }

    if (gPlayerState.jump_status != 0) {
        u32 animStateEven = ((u32)entity->animationState >> 1) << 1;
        u32 diff = (((direction >> 2) - animStateEven) + 2) & 7;
        if (diff > 4) {
            const s8* vel = &sVelocities3[animStateEven];
            AddPlayerVelocity(vel[0], vel[1]);
        } else {
            const s8* vel = &sVelocities1[(direction >> 2) << 1];
            AddPlayerVelocity(vel[0], vel[1]);
        }
    } else {
        const s8* vel = &sIceVelocities[(direction >> 2) << 1];
        AddPlayerVelocity(vel[0], vel[1]);
    }

apply_movement:;
    s16 velX = (s16)gPlayerState.vel_x;
    if (velX != 0) {
        u32 moveDir = 8;
        s32 moveSpeed = velX;
        if (moveSpeed < 0) {
            moveDir = 0x18;
            moveSpeed = -moveSpeed;
        }
        LinearMoveDirectionOLD(entity, (u32)moveSpeed, moveDir);
        sub_0807A5B8(moveDir);
    }

    s16 velY = (s16)gPlayerState.vel_y;
    if (velY != 0) {
        u32 moveDir = 0x10;
        s32 moveSpeed = velY;
        if (moveSpeed < 0) {
            moveDir = 0;
            moveSpeed = -moveSpeed;
        }
        LinearMoveDirectionOLD(entity, (u32)moveSpeed, moveDir);
        sub_0807A5B8(moveDir);
    }

    if (gPlayerState.jump_status == 0) {
        s16 dampX = (s16)gPlayerState.vel_x;
        s16 dampY = (s16)gPlayerState.vel_y;
        DampPlayerVelocityAxis(&dampX);
        DampPlayerVelocityAxis(&dampY);
        gPlayerState.vel_x = (u16)dampX;
        gPlayerState.vel_y = (u16)dampY;
    }
}

void sub_08008926(Entity* entity) {
    ApplyIceVelocityCore(entity, gPlayerState.direction, 1);
}

void UpdateIcePlayerVelocity(Entity* entity) {
    u32 direction = ((u32)entity->animationState >> 1) << 3;
    ApplyIceVelocityCore(entity, direction, 0);
}
