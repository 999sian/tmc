/**
 * @file object70.c
 * @ingroup Objects
 *
 * @brief Object70 object
 */
#include "asm.h"
#include "object.h"
#include "effects.h"
#include "player.h"

void Object70_Init(Entity*);
void Object70_Action1(Entity*);

void Object70(Entity* this) {
    static void (*const Object70_Actions[])(Entity*) = {
        Object70_Init,
        Object70_Action1,
    };
    Object70_Actions[this->action](this);
}

void Object70_Init(Entity* this) {
    this->action = 1;
    this->spriteSettings.draw = 1;
    this->frameIndex = this->type + 0xb;
    if (this->type != 0) {
        SnapToTile(this);
#ifdef PC_PORT
        /* Issue #84: on GBA flipY=3 (OAM priority 3, lowest) hides Link
         * behind the muddy-water BG so Object70's splash sprite can
         * show only his head. On the PC port Object70's splash sprite
         * doesn't render, so flipY=3 leaves Link fully invisible.
         * Use flipY=2 (normal priority) until the splash renders. */
        gPlayerEntity.base.spriteOrientation.flipY = 2;
#else
        gPlayerEntity.base.spriteOrientation.flipY = 3;
#endif
        if ((gPlayerEntity.base.spritePriority.b0) != 7) {
            this->spritePriority.b0 = gPlayerEntity.base.spritePriority.b0 + 1;
        } else {
            this->spritePriority.b0 = 7;
        }
    }
}

void Object70_Action1(Entity* this) {

    if (this->type == 0) {
        if (gPlayerEntity.base.z.WORD != 0 || (gPlayerState.dash_state & 0x40) != 0 ||
            gPlayerState.floor_type != SURFACE_SWAMP ||
            (gPlayerEntity.base.action != PLAYER_NORMAL && gPlayerEntity.base.action != PLAYER_ROLL &&
             gPlayerEntity.base.action != PLAYER_JUMP)) {
            if (gPlayerEntity.base.z.WORD == 0) {
                CreateFx(&gPlayerEntity.base, FX_GREEN_SPLASH, 0);
            }

            gPlayerEntity.base.spriteOrientation.flipY = 2;
            DeleteThisEntity();
        }
        this->x = gPlayerEntity.base.x;
        this->y = gPlayerEntity.base.y;
        if (gPlayerState.jump_status == 0) {
#ifdef PC_PORT
            /* Issue #84 — see Object70_Init for rationale. */
            gPlayerEntity.base.spriteOrientation.flipY = 2;
#else
            gPlayerEntity.base.spriteOrientation.flipY = 3;
#endif
            if (gPlayerEntity.base.spritePriority.b0 != 7) {
                this->spritePriority.b0 = gPlayerEntity.base.spritePriority.b0 + 1;
            } else {
                this->spritePriority.b0 = 7;
            }
        }
        return;
    }

    if (gPlayerEntity.base.action != PLAYER_USEENTRANCE) {
        if (this->collisionLayer == 1) {
            gPlayerEntity.base.spriteOrientation.flipY = 2;
        } else {
            gPlayerEntity.base.spriteOrientation.flipY = 1;
        }
        DeleteThisEntity();
    }
}
