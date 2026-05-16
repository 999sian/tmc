/**
 * @file wizzrobeWind.c
 * @ingroup Enemies
 *
 * @brief Wizzrobe Wind enemy
 */
#include "enemy/wizzrobe.h"

#include "collision.h"
#include "enemy.h"
#include "projectile.h"
#include "object.h"
#include "asm.h"
#include "sound.h"
#include "effects.h"
#include "room.h"
#include "physics.h"
#include "player.h"
#include "tiles.h"

extern void (*const WizzrobeWind_Functions[])(WizzrobeEntity*);
extern void (*const WizzrobeWind_Actions[])(WizzrobeEntity*);

void sub_0802F888(WizzrobeEntity*);
void sub_0802FA48(WizzrobeEntity*);
bool32 sub_0802FA88(WizzrobeEntity*);
void sub_0802F9C8(WizzrobeEntity*);
void sub_0802F8E4(WizzrobeEntity*);

void WizzrobeWind(WizzrobeEntity* this) {
    WizzrobeWind_Functions[GetNextFunction(super)](this);
    EnemySetFXOffset(super, 0, 1, -0x10);
}

void WizzrobeWind_OnTick(WizzrobeEntity* this) {
    WizzrobeWind_Actions[super->action](this);
}

void WizzrobeWind_OnCollision(WizzrobeEntity* this) {
    if (super->confusedTime != 0) {
        EnemyCreateFX(super, FX_STARS);
    }
    EnemyFunctionHandlerAfterCollision(super, WizzrobeWind_Functions);
    if (super->contactFlags == (CONTACT_NOW | 0x7)) {
        Entity* obj = CreateObject(FLAME, 3, 0);
        if (obj != NULL) {
            obj->spritePriority.b0 = 3;
            obj->spriteOffsetY = -4;
            obj->parent = super;
        }
    }
    if (super->health == 0) {
        SetTile(this->tileIndex, this->tilePos, super->collisionLayer);
    }
}

void WizzrobeWind_Init(WizzrobeEntity* this) {
    Entity* projectile;

    if (super->type2 != 0) {
        super->action = 3;
        super->speed = 0xc0;
        super->flags |= 0x80;
        this->targetIndex = 0;
        super->child = (Entity*)GetCurrentRoomProperty(super->timer);
        sub_0802FA48(this);
        sub_0802FA88(this);
    } else {
        sub_0804A720(super);
        super->action = 1;
        this->timer2 = 0xff;
        this->timer1 = 0x28;
        super->timer = 40;
        super->subtimer = 96;
        sub_0802F888(this);
    }
    projectile = EnemyCreateProjectile(super, WIND_PROJECTILE, 0);
    if (projectile != NULL) {
        super->parent = projectile;
        projectile->parent = super;
        projectile->direction = super->direction;
    }
    InitializeAnimation(super, super->direction >> 3);
}

void WizzrobeWind_Action1(WizzrobeEntity* this) {
    u8 tmp;
    Entity* parent;
    switch (this->timer2) {
        case 0xff:
            if (--super->subtimer == 0) {
                this->timer2 = 0;
            }
            break;
        case 0:
            if (--super->timer == 0) {
                this->timer2++;
                super->timer = 16;
                super->flags |= 0x80;
            }
            break;
        case 1:
            if (--super->timer == 0) {
                super->action = 2;
                this->timer2 = 0;
                super->timer = 40;
                tmp = super->direction >> 3;
                parent = super->parent;
                parent->timer = 1;
                parent->spriteSettings.draw = 1;
                InitializeAnimation(super, tmp | 4);
            }
            break;
    }
    sub_0802F9C8(this);
}

void WizzrobeWind_Action2(WizzrobeEntity* this) {
    switch (this->timer2) {
        case 0:
            switch (--super->timer) {
                case 0:
                    this->timer2++;
                    super->timer = 56;
                    super->subtimer = 0;
                    super->parent->spriteSettings.draw = 0;
                    break;
                case 8:
                    if (EntityInRectRadius(super, &gPlayerEntity.base, 0xa0, 0xa0) && CheckOnScreen(super)) {
                        Entity* projectile = EnemyCreateProjectile(super, WIND_PROJECTILE, 1);
                        if (projectile != NULL) {
                            projectile->direction = super->direction & 0x18;
                        }
                    }
                    break;
            }
            break;
        case 1:
            if (--super->timer == 0) {
                this->timer2++;
                this->timer1 = 0x28;
                super->timer = 40;
                super->subtimer = 0;
                super->flags &= ~0x80;
                EnqueueSFX(SFX_156);
                SetTile(this->tileIndex, this->tilePos, super->collisionLayer);
                InitializeAnimation(super, super->direction >> 3);
            }
            break;
        case 2:
            if (--super->timer == 0) {
                this->timer2++;
                super->timer = (Random() & 0x3f) + 32;
                super->spriteSettings.draw = 0;
            }
            break;
        case 3:
            if (--super->timer == 0) {
                super->action = 1;
                this->timer2 = 0;
                this->timer1 = 0x28;
                super->timer = 40;
                EnqueueSFX(SFX_156);
                sub_0802F8E4(this);
                InitializeAnimation(super, super->direction >> 3);
            }
            break;
    }
    sub_0802F9C8(this);
}

void WizzrobeWind_Action3(WizzrobeEntity* this) {
    Entity* parent;
    sub_0802FA88(this);
    sub_0802F9C8(this);
    parent = super->parent;
    if (this->timer1 == 0) {

        switch (this->timer2) {
            case 0:
                this->timer2 = 1;
                super->timer = 64;
                break;
            case 1:
                if (--super->timer != 0) {
                    return;
                }
                this->timer2++;
                super->timer = 40;
                parent->timer = 1;
                parent->spriteSettings.draw = 1;
                InitializeAnimation(super, super->animationState >> 1 | 4);
                break;
            case 2:
                if (--super->timer == 0) {
                    this->timer2++;
                    super->timer = (Random() & 0x1f) + 48;
                    parent->spriteSettings.draw = 0;
                    InitializeAnimation(super, super->animationState >> 1);
                } else if (super->timer == 8) {
                    parent = EnemyCreateProjectile(super, WIND_PROJECTILE, 1);
                    if (parent != NULL) {
                        parent->direction = super->direction & 0x18;
                    }
                }
        }
    } else {
        if (this->timer2 != 0) {
            this->timer2 = 0;
            parent->spriteSettings.draw = 0;
        }
    }
}

void sub_0802F888(WizzrobeEntity* this) {
    super->direction = (sub_08049F84(super, 3) + 4) & 0x18;
    this->tilePos = COORD_TO_TILE(super);
    this->tileIndex = GetTileIndex(this->tilePos, super->collisionLayer);
    SetTile(SPECIAL_TILE_113, this->tilePos, super->collisionLayer);
}

void sub_0802F8E4(WizzrobeEntity* this) {
    u16 uVar1;
    s32 iVar4;
    u32 tilePos;
    u32 uVar7;
    u32 uVar8;

    bool32 loopCondition;
    u32 rand;

#ifdef PC_PORT
    /* On GBA, WizzrobeEntity::unk_6e/unk_6f overlay Enemy::rangeX/rangeY (offset 0x6e/0x6f),
     * and unk_70/unk_72 overlay Enemy::homeX/homeY (offset 0x70/0x72). On 64-bit PC the
     * Entity base is 0x28 bytes wider (8-byte pointers), so the GBA-style offsets land
     * inside the child-pointer area instead — `unk_6e << 3` becomes `(byte 6 of an x86-64
     * canonical pointer, always 0) << 3 == 0`, and the `% 0` aborts with #DE (#78).
     * Read the real Enemy fields. */
    Enemy* em = (Enemy*)super;
    u8 rangeX = em->rangeX;
    u8 rangeY = em->rangeY;
    u16 homeX = (u16)em->homeX;
    u16 homeY = (u16)em->homeY;
#else
    u8 rangeX = this->unk_6e;
    u8 rangeY = this->unk_6f;
    u16 homeX = this->unk_70;
    u16 homeY = this->unk_72;
#endif

    if (super->type2 == 0) {
        /* Defensive: a zero range box would still divide-by-zero. The original game
         * relies on roomInit / EnemyInit to populate range from the enemy definition,
         * so this should not happen in normal flow — but bail rather than fault. */
        if (rangeX == 0 || rangeY == 0) {
            sub_0802F888(this);
            return;
        }
        loopCondition = TRUE;
        do {
            rand = Random();
            uVar1 = homeX;
            iVar4 = ((s32)rand & 0x7ff0) % (rangeX << 3);
            uVar8 = (uVar1 + iVar4) | 8;
            rand >>= 0x10;
            uVar1 = homeY;
            iVar4 = ((s32)(rand)&0x7ff0) % (rangeY << 3);
            uVar7 = (uVar1 + iVar4) | 8;
            tilePos = TILE(uVar8, uVar7);
            if ((GetCollisionDataAtTilePos(tilePos, super->collisionLayer) == 0) &&
                (GetTileIndex(tilePos, super->collisionLayer) != SPECIAL_TILE_113)) {
                super->x.HALF.HI = (s16)uVar8;
                super->y.HALF.HI = (s16)uVar7;
                if (sub_08049FA0(super) != 0) {
                    loopCondition = FALSE;
                }
            }
        } while (loopCondition);
        sub_0802F888(this);
    }
}

void sub_0802F9C8(WizzrobeEntity* this) {
    if (super->subtimer == 0) {
        if (this->timer1 != 0) {
            if ((--this->timer1 & 1) != 0) {
                super->spriteSettings.draw = 0;
            } else {
                super->spriteSettings.draw = 1;
            }
            if ((super->type2 != 0) && (LinearMoveUpdate(super), this->timer1 == 0)) {
                super->flags |= 0x80;
            }
        } else {
            if (super->type2 != 0) {
                ProcessMovement0(super);
                if (super->collisions != COL_NONE) {
                    super->flags &= ~0x80;
                    this->timer1 = 0x28;
                }
            }
        }
        GetNextFrame(super);
    }
}

void sub_0802FA48(WizzrobeEntity* this) {
    u16* target;
    u16* child;

    child = (u16*)super->child;
    target = &child[this->targetIndex * 2];
    if (target[0] == 0xffff) {
        this->targetIndex = 0;
        target = child;
    }
    this->targetX = gRoomControls.origin_x + target[0];
    this->targetY = gRoomControls.origin_y + target[1];
}

bool32 sub_0802FA88(WizzrobeEntity* this) {
    u32 direction;
    bool32 result = FALSE;
    if (EntityWithinDistance(super, this->targetX, this->targetY, 2)) {
        this->targetIndex++;
        sub_0802FA48(this);
        direction = CalculateDirectionTo(super->x.HALF.HI, super->y.HALF.HI, this->targetX, this->targetY);
        super->direction = direction;
        super->animationState = ((direction + 4) & 0x18) >> 2;
        if (((super->parent)->spriteSettings.draw & 3) != 0) {
            InitializeAnimation(super, direction >> 3 | 4);
        } else {
            InitializeAnimation(super, direction >> 3);
        }
        result = TRUE;
    } else {
        direction = CalculateDirectionTo(super->x.HALF.HI, super->y.HALF.HI, this->targetX, this->targetY);
        sub_08004596(super, direction);
        direction = ((super->direction + 4) & 0x18) >> 2;
        if (direction != super->animationState) {
            super->animationState = direction;
            InitializeAnimation(super, direction >> 1);
        }
    }
    return result;
}

void (*const WizzrobeWind_Functions[])(WizzrobeEntity*) = {
    WizzrobeWind_OnTick,
    WizzrobeWind_OnCollision,
    (void (*)(WizzrobeEntity*))GenericKnockback,
    (void (*)(WizzrobeEntity*))GenericDeath,
    (void (*)(WizzrobeEntity*))GenericConfused,
    WizzrobeWind_OnTick,
};
void (*const WizzrobeWind_Actions[])(WizzrobeEntity*) = {
    WizzrobeWind_Init,
    WizzrobeWind_Action1,
    WizzrobeWind_Action2,
    WizzrobeWind_Action3,
};
