#include "global.h"
#include "area.h"
#include "room.h"
#include "player.h"
#include "scroll.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

RoomControls gRoomControls;
PlayerEntity gPlayerEntity;
PlayerState gPlayerState;
RoomTransition gRoomTransition;
Area gArea;
u8 gUpdateVisibleTiles;
static RoomResInfo nextRoom;
static int wide = 1;
static int fills;
static int filledX;
static int filledRoom;
int Port_Widescreen_EffectiveViewWidth(void) {
    return wide && gRoomControls.scrollAction != 2 && gRoomControls.scrollAction != 4 ? 384 : 240;
}
int Port_Widescreen_CameraRestX(int x) {
    int width = Port_Widescreen_EffectiveViewWidth();
    int rest = x - width / 2;
    int max = gRoomControls.origin_x + gRoomControls.width - width;
    if (rest > max) rest = max;
    if (rest < gRoomControls.origin_x) rest = gRoomControls.origin_x;
    return rest;
}
void UpdateScreenShake(void) {}
void UpdateScrollVram(void) {
    if (gUpdateVisibleTiles == 1) {
        fills++;
        filledX = gRoomControls.scroll_x;
        filledRoom = gRoomControls.room;
    }
}
void MemFill32(u32 value, void* dest, u32 size) { memset(dest, value, size); }
u32 sub_0807BEEC(u32 x, u32 y, u32 direction) { return 1; }
RoomResInfo* GetCurrentRoomInfo(void) { return &nextRoom; }
void SetInitializationPriority(void) {}
void ClearEventPriority(void) {}
void sub_0807FEC8(RoomControls*);
#include "widescreen_scroll_functions.inc"

int main(void) {
    gRoomControls.width = 1024;
    gRoomControls.height = 512;
    gRoomControls.camera_target = &gPlayerEntity.base;
    gRoomControls.scrollAction = 1;
    gRoomControls.scroll_x = 640; /* 1024 - 384 */
    gRoomControls.scroll_y = 80;
    gPlayerEntity.base.x.HALF.HI = 1016;
    gPlayerEntity.base.y.HALF.HI = 160;
    assert(sub_0807BD14(&gPlayerEntity.base, 1));
    assert(gRoomControls.scroll_x == 784); /* native outgoing right edge */
    Scroll2Sub0(&gRoomControls);
    assert(fills == 1 && filledX == 784 && filledRoom == 1);
    /* GameMain loads destination AFTER Scroll2Sub0. */
    gRoomControls.origin_x = 1024;
    Scroll2Sub1(&gRoomControls);
    for (int i = 0; i < 59; ++i) {
        Scroll2Sub2(&gRoomControls);
        assert(gRoomControls.scrollAction == 2 && gUpdateVisibleTiles == 2);
    }
    Scroll2Sub2(&gRoomControls);
    assert(gRoomControls.scrollAction == 0 && gUpdateVisibleTiles == 1);
    assert(gRoomControls.scroll_x == 1024);
    UpdateScrollVram();
    assert(fills == 2 && filledX == 1024);

    gRoomControls.scrollAction = 4;
    gRoomControls.scroll_x = 1500;
    gPlayerEntity.base.x.HALF.HI = 1600;
    gRoomControls.unk_18 = 19;
    gRoomTransition.frameCount = 0;
    Scroll4Sub1(&gRoomControls);
    assert(gRoomControls.scrollAction == 0 && gUpdateVisibleTiles == 1);
    assert(gRoomControls.scroll_x == 1408);
    puts("Widescreen scrolling entry, rolling frames and completion: PASS");
}
