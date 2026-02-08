/**
 * @file bladeTrap.c
 * @ingroup Enemies
 *
 * @brief Blade Trap enemy
 */
#include "object.h"
#include "sound.h"
#include "room.h"
#include "physics.h"

typedef struct {
    /*0x00*/ Entity base;
    /*0x68*/ u8 unused1[12];
    /*0x74*/ u16 unk_74;
} BladeTrapEntity;

#define DIR_NOT_MOVING_CHECK 0x80

void BladeTrap(BladeTrapEntity* this) {
    if (super->action == 0) {
        super->action = 1;
        super->child = GetCurrentRoomProperty(super->type);
        UpdateRailMovement(super, (u16**)&super->child, &this->unk_74);
    }
    if (!(super->direction & DIR_NOT_MOVING_CHECK)) {
        LinearMoveUpdate(super);
    }

    if (!(--this->unk_74)) {
        if (!(super->direction & DIR_NOT_MOVING_CHECK)) {
            EnqueueSFX(SFX_METAL_CLINK);
        }
        UpdateRailMovement(super, (u16**)&super->child, &this->unk_74);
    }
}
