#include <stdio.h>
#include "src/manager/rollingBarrelManager.c"
PlayerEntity gPlayerEntity;
PlayerState gPlayerState;
RoomControls gRoomControls;
RoomTransition gRoomTransition;
SaveFile gSave;
bool32 CheckGlobalFlag(u32 flag) {
    return TRUE;
}
void SoundReq(u32 req) {
}
int main(void) {
    const int angles[] = { 0x117, 0x118, 0x124, 0x125, 0xf0 };
    const int want[] = { 0, 1, 1, 0, 0 };
    int failures = 0;
    for (int i = 0; i < 5; i++) {
        RollingBarrelManager barrel = { 0 };
        barrel.unk_20 = angles[i];
        gPlayerState.queued_action = 0;
        gPlayerEntity.base.x.HALF.HI = 0x78;
        gPlayerEntity.base.y.HALF.HI = 0x50;
        sub_08058A04(&barrel);
        int fall = gPlayerState.queued_action == PLAYER_FALL;
        printf("barrel angle %x: fall=%d expected=%d\n", angles[i], fall, want[i]);
        failures += fall != want[i];
    }
    return failures;
}
