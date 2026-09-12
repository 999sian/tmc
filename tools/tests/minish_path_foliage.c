/* Exercise the real per-frame parallax selection, including re-entry. */
#include <stdio.h>
#include "src/manager/verticalMinishPathBackgroundManager.c"
#include <assert.h>

Screen gScreen;
RoomControls gRoomControls;
RoomVars gRoomVars;
u16 gMapDataTopSpecial[0x4000];
static u32 loadedGroup;
void LoadGfxGroup(u32 group) { loadedGroup = group; }

static void check(int scroll, int bg3, int bg1) {
    VerticalMinishPathBackgroundManager manager = { 0 };
    gRoomControls.origin_y = 100;
    gRoomControls.scroll_y = 100 + scroll;
    sub_0805754C(&manager);
    assert((u8*)gScreen.bg3.subTileMap == (u8*)gMapDataTopSpecial + bg3);
    assert((u8*)gScreen.bg1.subTileMap == (u8*)gMapDataTopSpecial + bg1);
    assert(gScreen.bg3.updated && gScreen.bg1.updated);
    gScreen.bg3.updated = gScreen.bg1.updated = 0;
    sub_0805754C(&manager);
    assert(!gScreen.bg3.updated && !gScreen.bg1.updated);
    gRoomVars.graphicsGroups[0] = 31; /* Hyrule Town Minish entrance */
    VerticalMinishPathBackgroundManager_OnEnterRoom(&manager);
    assert(loadedGroup == 31);
    assert(gScreen.bg3.updated && gScreen.bg1.updated);
    assert((u8*)gScreen.bg3.subTileMap == (u8*)gMapDataTopSpecial + bg3);
    assert((u8*)gScreen.bg1.subTileMap == (u8*)gMapDataTopSpecial + bg1);
}
int main(void) {
    check(0, 0, 0x2000);
    check(64, 0x200, 0x2200);
    check(512, 0x1200, 0x3400);
    check(-512, 0, 0x0c00);
    check(30000, 0x7800, 0x7800);
    puts("Minish foliage page selection and re-entry: PASS");
    return 0;
}
