#include "entity.h"
#include "physics.h"
#include "room.h"
#include "object.h"
#include "asm.h"
#include "effects.h"
#include "player.h"
#include "pauseMenu.h"
#include "definitions.h"
#include "vram.h"
#include "object/linkAnimation.h"
#include "object/itemOnGround.h"

extern const Hitbox* const gObjectHitboxes[];

extern const ObjectDefinition gObjectDefinitions[];

static Entity* CreateSpeechBubble(Entity*, u32, s32, s32);

const s8 gUnk_08126EE4[] = {
    0,   -32, //
    22,  -6,  //
    0,   12,  //
    -22, -6,
};
const s8 gUnk_08126EEC[] = {
    0,   -13, //
    13,  0,   //
    0,   16,  //
    -13, 0,
};

Entity* CreateLinkAnimation(Entity* parent, u32 type, u32 type2) {
    Entity* e = CreateAuxPlayerEntity();
    if (e != NULL) {
        LinkAnimationEntity* this = (LinkAnimationEntity*)e;
        e->id = LINK_ANIMATION;
        e->kind = OBJECT;
        e->type = type;
        e->type2 = type2;
        e->parent = parent;
        AppendEntityToList(e, 6);
        PrependEntityToList(e, 6);
        CopyPosition(&gPlayerEntity.base, e);
        gPriorityHandler.event_priority = 6;
        gPauseMenuOptions.disabled = 1;

        // store player state
        this->storeFlags = gPlayerEntity.base.flags;
        this->storeDrawFlags = gPlayerEntity.base.spriteSettings.draw;
        this->storeIFrames = gPlayerEntity.base.iframes;
        this->storeField7 = gPlayerState.field_0x7;
        this->storeKeepFacing = gPlayerState.keepFacing;
        this->storeFieldA = gPlayerState.field_0xa;
        this->storeField27 = gPlayerState.field_0x27[0];
        this->storeMobility = gPlayerState.mobility;
        this->storeStateFlags = gPlayerState.flags;
        this->store8A = gPlayerState.field_0x8a;

        // redundant, this is done by the LinkAnimation object
        gPlayerEntity.base.flags &= ~ENT_COLLIDE;
        gPlayerEntity.base.spriteSettings.draw = 0;
    }
    return e;
}

void ObjectInit(Entity* this) {
    const ObjectDefinition* definition = &gObjectDefinitions[this->id];
    if (LoadObjectSprite(this, this->type, definition) == 2) {
        UpdateSpriteForCollisionLayer(this);
    }
}

u32 LoadObjectSprite(Entity* this, s32 type, const ObjectDefinition* definition) {
    u16 tmp1;
    u8 tmp2;

    if ((this->flags & ENT_DID_INIT) != 0) {
        // Sprite already loaded
        return 1;
    }
    if ((definition->bitfield.type) == 0) {
        // Object has no sprite
        this->flags |= ENT_DID_INIT | definition->bitfield.flags << 2;
        return 1;
    }

    if ((u8)(((*((u8*)definition) & 0xff & 3) - 2)) < 2) {
        // Multiple forms
        definition = &definition->data.definition[type];
    }
    tmp1 = definition->bitfield.gfx;
    switch (definition->bitfield.gfx_type) {
        case 2:
            this->spriteVramOffset = definition->bitfield.gfx;
            break;
        case 1:
            if (!LoadSwapGFX(this, tmp1, 0)) {
                return 0;
            }
            break;
        default:
            if (!LoadFixedGFX(this, tmp1)) {
                return 0;
            }
            break;
    }
    tmp1 = definition->data.sprite.paletteIndex;
    LoadObjPalette(this, tmp1);
    tmp2 = 0xff;
    this->animIndex = tmp2;
    this->hurtType = 0x48;
    this->spriteIndex = definition->data.sprite.spriteIndex;
    this->spriteSettings.shadow = definition->data.sprite.shadow;
    this->spritePriority.b1 = definition->data.sprite.spritePriority;
    this->spriteSettings.draw = definition->data.sprite.draw;
    this->hitbox = (Hitbox*)gObjectHitboxes[definition->bitfield.hitbox];
    this->flags |= ENT_DID_INIT | definition->bitfield.flags << 2;

    return 2;
}

Entity* CreateObject(Object id, u32 type, u32 type2) {
    Entity* entity = GetEmptyEntity();
    if (entity != NULL) {
        entity->kind = OBJECT;
        entity->id = id;
        entity->type = type;
        entity->type2 = type2;
        AppendEntityToList(entity, 6);
    }
    return entity;
}

Entity* CreateObjectWithParent(Entity* parent, Object id, u32 type, u32 type2) {
    Entity* entity = CreateObject(id, type, type2);
    if (entity != NULL) {
        entity->parent = parent;
        CopyPosition(parent, entity);
    }

    return entity;
}

Entity* CreateFx(Entity* parentEnt, Effect type, u32 type2) {
    return CreateObjectWithParent(parentEnt, SPECIAL_FX, type, type2);
}

void CreateDeathFx(Entity* parent) {
    CreateFx(parent, FX_DEATH, 0);
}

void CreateDeathFxAt(s32 xOff, s32 yOff, u32 layer) {
    Entity* ent;

    ent = CreateObject(SPECIAL_FX, FX_DEATH, 0);
    if (ent != NULL) {
        ent->x.HALF.HI = gRoomControls.origin_x + xOff;
        ent->y.HALF.HI = gRoomControls.origin_y + yOff;
        ent->collisionLayer = layer;
    }
}

void CreateDashFx(Entity* parent) {
    CreateFx(parent, FX_DASH, 0);
}

void CreateExplosionBrokenFx(Entity* parent) {
    CreateFx(parent, FX_6, 0);
}

void CreateWaterSplashFx(Entity* parent) {
    CreateFx(parent, FX_WATER_SPLASH, 0);
}

Entity* CreateGroundItem(Entity* parent, u32 item, u32 subvalue) {
    Entity* ent;

    ent = CreateObjectWithParent(parent, GROUND_ITEM, item, subvalue);
    if (ent != NULL) {
        ent->timer = 5;
    }
    return ent;
}

Entity* CreateGroundItemWithFlags(Entity* parent, u32 item, u32 subvalue, u32 flags) {
    Entity* ent;

    ent = CreateObjectWithParent(parent, GROUND_ITEM, item, subvalue);
    if (ent != NULL) {
        ItemOnGroundEntity* this = (ItemOnGroundEntity*)ent;
        ent->timer = 5;
        this->flag = flags;
    }
    return ent;
}

Entity* CreateRippleFx(Entity* parent) {
    Entity* ent;

    ent = CreateFx(parent, FX_RIPPLE, 0);
    if (ent != NULL) {
        ent->spritePriority.b0 = 7;
    }
    return ent;
}

void CreateRippleFxRandom(Entity* parent, s32 range) {
    s32 sVar1, sVar2;
    Entity* ent;
    u32 uVar3;

    ent = CreateRippleFx(parent);
    if (ent != NULL) {
        uVar3 = Random();
        sVar1 = (int)uVar3 % (++range);
        uVar3 >>= 8;
        if ((uVar3 & 1) != 0) {
            sVar1 = -sVar1;
        }
        ent->x.HALF.HI += sVar1;
        uVar3 >>= 8;
        sVar2 = (int)uVar3 % range;
        uVar3 >>= 8;
        if ((uVar3 & 1) != 0) {
            sVar2 = -sVar2;
        }
        ent->y.HALF.HI += sVar2;
    }
}

Entity* CreateLargeRippleFx(Entity* parent) {
    Entity* ent = CreateFx(parent, FX_RIPPLE_LARGE, 0);
    if (ent != NULL) {
        ent->spritePriority.b0 = 7;
    }
    return ent;
}

void CreateLargeRippleFxRandom(Entity* parent, s32 minDistance, s32 maxDistance) {
    Entity* entity;
    s32 radius;
    s32 angle;

    entity = CreateLargeRippleFx(parent);
    if (entity != NULL) {
        angle = Random();
        radius = (angle % (maxDistance - minDistance + 1)) + minDistance;
        angle = angle >> 0x10 & 0xff;
        entity->x.WORD += FixedDiv(FixedMul(gSineTable[angle], radius << 8), 0x100) << 8;
        entity->y.WORD -= FixedDiv(FixedMul(gSineTable[angle + 0x40], radius << 8), 0x100) << 8;
    }
}

void CreateSparkleFx(Entity* parent) {
    Entity* sparkle;
    u32 rand;
    s32 y;
    s32 x;

    sparkle = CreateObject(SPECIAL_FX, FX_SPARKLE4, 0);
    if (sparkle != NULL) {
        rand = Random();
        x = rand & 0xf;
        if ((rand & 0x80) != 0) {
            x = -x;
        }
        y = rand >> 8 & 0xf;
        if (4 < y) {
            y = -y;
        }
        PositionRelative(parent, sparkle, x << 0x10, y << 0x10);
        SortEntityAbove(parent, sparkle);
        sparkle->spriteOffsetX = parent->spriteOffsetX;
        sparkle->spriteOffsetY = parent->spriteOffsetY;
    }
}

void SyncPlayerToPlatform(Entity* this, bool32 param_2) {
    s16 oldValue1, oldValue2;
    s32 newValue1, newValue2, newValue3, newValue4;
    s16 diff;

    if ((this->direction & 0x80) != 0)
        return;

    switch (this->direction >> 3) {
        case 0:
            oldValue1 = this->y.HALF_U.HI;
            LinearMoveUpdate(this);
            newValue1 = this->y.HALF_U.HI;
            diff = ((oldValue1 - newValue1));
            if ((diff != 0) && (param_2 != 0)) {
                sub_080044AE(&gPlayerEntity.base, diff << 8, 0);
            }
            break;
        case 1:
            oldValue2 = this->x.HALF_U.HI;
            LinearMoveUpdate(this);
            newValue2 = this->x.HALF_U.HI;
            diff = (newValue2 - oldValue2);
            if ((diff != 0) && (param_2 != 0)) {
                sub_080044AE(&gPlayerEntity.base, diff << 8, 8);
            }
            break;
        case 2:
            oldValue2 = this->y.HALF_U.HI;
            LinearMoveUpdate(this);
            newValue3 = this->y.HALF_U.HI;
            diff = (newValue3 - oldValue2);
            if ((diff != 0) && (param_2 != 0)) {
                sub_080044AE(&gPlayerEntity.base, diff << 8, 0x10);
            }
            break;
        case 3:
            oldValue1 = this->x.HALF_U.HI;
            LinearMoveUpdate(this);
            newValue4 = this->x.HALF_U.HI;
            diff = ((oldValue1 - newValue4));
            if ((diff != 0) && (param_2 != 0)) {
                sub_080044AE(&gPlayerEntity.base, diff << 8, 0x18);
            }
            break;
    }
}

void UpdateRailMovement(Entity* this, u16** param_2, u16* param_3) {
    u16* ptr = *param_2;
    if (ptr != NULL) {
        if (*ptr == 0xfe) {
            ptr = ptr + (u32)ptr[1] * -3;
        }
        if (*ptr == 0xff) {
            this->direction = DIR_NONE;
            this->speed = 0;
        } else {
            this->direction = (u8)*ptr;
            this->speed = ptr[2];
        }
        *param_3 = ptr[1];
        *param_2 = ptr + 3;
    }
}

Entity* CreateSpeechBubbleExclamationMark(Entity* parent, s32 offsetX, s32 offsetY) {
    return CreateSpeechBubble(parent, 0, offsetX, offsetY);
}

Entity* CreateSpeechBubbleQuestionMark(Entity* parent, s32 offsetX, s32 offsetY) {
    return CreateSpeechBubble(parent, 1, offsetX, offsetY);
}

Entity* CreateSpeechBubbleSleep(Entity* parent, s32 offsetX, s32 offsetY) {
    return CreateSpeechBubble(parent, 2, offsetX, offsetY);
}

static Entity* CreateSpeechBubble(Entity* parent, u32 type2, s32 xOffset, s32 yOffset) {
    Entity* obj = CreateObject(THOUGHT_BUBBLE, 0, type2);
    if (obj != NULL) {
        CopyPosition(parent, obj);
        SortEntityAbove(parent, obj);
        obj->parent = parent;
        obj->spriteOffsetX = xOffset;
        obj->spriteOffsetY = yOffset;
        SetEntityPriority(obj, parent->updatePriority);
    }
    return obj;
}
