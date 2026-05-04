/**
 * @file cloudOverlayManager.c
 * @ingroup Managers
 *
 * @brief Cloud bg overlay for Hyrule Fields.
 */
#include "manager/cloudOverlayManager.h"
#include "area.h"
#include "screen.h"
#include "game.h"
#include "room.h"

void CloudOverlayManager_OnEnterRoom(CloudOverlayManager*);
void CloudOverlayManager_OnExitRoom(CloudOverlayManager*);

void CloudOverlayManager_Main(CloudOverlayManager* this) {
    static const u16 gUnk_0810865C[] = {
        0xf01, 0xe02, 0xe03, 0xe04, 0, 0,
    };
    if (this == NULL) {
        if (gArea.onEnter != CloudOverlayManager_OnEnterRoom) {
            CloudOverlayManager_OnEnterRoom(NULL);
        }
    } else {
        if (super->action == 0) {
            super->action = 1;
            super->flags |= ENT_PERSIST;
            super->timer = 0;
            super->subtimer = 8;
            this->field_0x20 = gUnk_0810865C[0];
            SetEntityPriority((Entity*)this, PRIO_PLAYER_EVENT);
            if (gArea.onEnter == NULL) {
                RegisterTransitionHandler(this, CloudOverlayManager_OnEnterRoom, CloudOverlayManager_OnExitRoom);
            } else {
                DeleteThisEntity();
            }
        } else {
            if ((gUnk_0810865C[super->timer] != 0) && (--super->subtimer == 0)) {
                super->subtimer = 4;
                if (gUnk_0810865C[++super->timer] != 0) {
                    this->field_0x20 = gUnk_0810865C[super->timer];
                    gScreen.controls.alphaBlend = this->field_0x20;
                }
            }
            gRoomControls.bg3OffsetX.WORD -= 0x2000;
            gRoomControls.bg3OffsetY.WORD -= 0x1000;
            gScreen.bg3.xOffset = gRoomControls.scroll_x + gRoomControls.bg3OffsetX.HALF.HI;
            gScreen.bg3.yOffset = gRoomControls.scroll_y + gRoomControls.bg3OffsetY.HALF.HI;
        }
    }
}

void CloudOverlayManager_OnEnterRoom(CloudOverlayManager* this) {
    gScreen.bg3.control = BGCNT_SCREENBASE(30) | BGCNT_PRIORITY(1) | BGCNT_CHARBASE(1);
    gScreen.lcd.displayControl |= DISPCNT_BG3_ON;
    gScreen.controls.layerFXControl =
        BLDCNT_TGT1_BG3 | BLDCNT_EFFECT_BLEND | BLDCNT_TGT2_BG1 | BLDCNT_TGT2_BG2 | BLDCNT_TGT2_OBJ | BLDCNT_TGT2_BD;
    gScreen.controls.alphaBlend = (this != NULL) ? this->field_0x20 : BLDALPHA_BLEND(0, 16);
    gScreen.bg3.xOffset = gRoomControls.scroll_x + gRoomControls.bg3OffsetX.HALF.HI;
    gScreen.bg3.yOffset = gRoomControls.scroll_y + gRoomControls.bg3OffsetY.HALF.HI;
    if (this != NULL) {
        CloudOverlayManager_Main(this);
    }
}

void CloudOverlayManager_OnExitRoom(CloudOverlayManager* this) {
    super->flags &= ~ENT_PERSIST;
    gScreen.lcd.displayControl &= ~DISPCNT_BG3_ON;
    gScreen.controls.layerFXControl = 0;
}
