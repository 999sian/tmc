/*
 * port/port_debug_actions_test.c — regression test for the F8 debug-menu
 * action layer (port_debug_actions.c).
 *
 * Guards the per-item toggle exclusivity logic in particular: a post-commit
 * adversarial review found that keying exclusivity off gItemMetaData[].menuSlot
 * cross-cleared unrelated inventory (e.g. owning the Big Wallet wiped every
 * sword), because that engine byte aliases garbage for non-grid item ids. The
 * fix keys exclusivity off an explicit in-table group; this test pins that
 * behaviour plus the flag / dungeon / stat / bottle helpers so the same class
 * of bug can't silently return.
 *
 * Standalone binary: it compiles port_debug_actions.c and provides minimal
 * definitions/stubs for the engine globals + functions that TU references but
 * does not own. Only the save-state effects are asserted.
 */
#include <stdio.h>
#include <string.h>

#include "save.h"
#include "area.h"
#include "player.h"
#include "main.h"        /* Main / gMain */
#include "room.h"        /* RoomControls / RoomHeader / gRoomControls */
#include "transitions.h" /* Transition / RoomTransition / gRoomTransition */
#include "item_ids.h"
#include "port_debug_actions.h"

/* ---- engine state the action layer reads (the bits the test exercises) ---- */
SaveFile gSave;
Area gArea;

/* Capacity tables (4 tiers each, indexed by walletType/bombBagType/quiverType
 * 0..3): realistic caps so the stat-clamp assertions exercise real bounds. */
const Wallet gWalletSizes[] = { { 100, 0 }, { 300, 0 }, { 500, 0 }, { 999, 0 } };
const u8 gBombBagSizes[] = { 10, 20, 30, 40 };
const u8 gQuiverSizes[]  = { 30, 40, 50, 70 };

/* ---- symbols pulled in only by the warp/teleport/enumeration functions the
 *      test never calls; zero-init definitions + no-op stubs satisfy the link. */
Main gMain;
PlayerState gPlayerState;
PlayerEntity gPlayerEntity;
RoomControls gRoomControls;
RoomTransition gRoomTransition;
RoomHeader* gAreaRoomHeaders[0x90];

void LoadItemGfx(void) {}
void DoExitTransition(const Transition* data) { (void)data; }
u32  GetCollisionDataAtTilePos(u32 tilePos, u32 layer) { (void)tilePos; (void)layer; return 0; }
bool32 Port_IsRoomHeaderPtrReadable(const void* ptr) { (void)ptr; return 0; }
void Port_RefreshAreaData(unsigned int area) { (void)area; }

/* ---- assertion harness ---- */
static int g_fails = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: %s\n", (msg)); ++g_fails; } \
} while (0)

static int Owned(int item) { return Port_DebugQuery_ToggleItemOwned(item); }

/* Find a toggle-table index by display name (test reads the table the same way
 * the menu does, so it stays valid if the order changes). */
static int IdxByName(const char* name) {
    int n = Port_DebugQuery_ToggleItemCount();
    for (int i = 0; i < n; ++i) {
        const char* nm = Port_DebugQuery_ToggleItemName(i);
        if (nm && strcmp(nm, name) == 0) return i;
    }
    fprintf(stderr, "FAIL: toggle item '%s' not found\n", name);
    ++g_fails;
    return -1;
}

int main(void) {
    memset(&gSave, 0, sizeof(gSave));
    memset(&gArea, 0, sizeof(gArea));

    /* ---- #1 the regression: non-grid items must NOT clear unrelated items ---- */
    const int smith  = IdxByName("Smith's Sword");
    const int green  = IdxByName("Green Sword");
    const int wallet = IdxByName("Big Wallet");
    const int water  = IdxByName("Water Element");
    const int bow    = IdxByName("Bow");

    Port_DebugAction_SetToggleItem(smith, 1);
    CHECK(Owned(smith), "smith sword should be owned");
    Port_DebugAction_SetToggleItem(wallet, 1);
    CHECK(Owned(wallet), "big wallet should be owned");
    CHECK(Owned(smith), "REGRESSION: big wallet cleared the sword");

    Port_DebugAction_SetToggleItem(bow, 1);
    Port_DebugAction_SetToggleItem(water, 1);
    CHECK(Owned(water), "water element should be owned");
    CHECK(Owned(bow), "REGRESSION: water element cleared the bow");

    /* ---- exclusivity WITHIN a real group still works ---- */
    Port_DebugAction_SetToggleItem(green, 1);
    CHECK(Owned(green), "green sword should be owned");
    CHECK(!Owned(smith), "owning green sword should clear smith sword (same group)");

    const int bombs  = IdxByName("Bombs");
    const int remote = IdxByName("Remote Bombs");
    Port_DebugAction_SetToggleItem(bombs, 1);
    Port_DebugAction_SetToggleItem(remote, 1);
    CHECK(Owned(remote) && !Owned(bombs), "remote bombs should clear plain bombs");

    /* ---- equip cleared when an owned item is un-owned ---- */
    gSave.stats.equipped[0] = ITEM_GREEN_SWORD;
    Port_DebugAction_SetToggleItem(green, 0);
    CHECK(!Owned(green), "green sword un-owned");
    CHECK(gSave.stats.equipped[0] == ITEM_NONE, "un-owning should drop the A-slot equip");

    /* ---- bottle content ---- */
    Port_DebugAction_SetBottleContent(0, ITEM_BOTTLE_RED_POTION);
    CHECK(Port_DebugQuery_BottleOwned(0), "setting content should own the bottle");
    CHECK(Port_DebugQuery_BottleContent(0) == ITEM_BOTTLE_RED_POTION, "bottle content set");
    Port_DebugAction_SetBottleContent(0, 9999); /* out of range -> ignored */
    CHECK(Port_DebugQuery_BottleContent(0) == ITEM_BOTTLE_RED_POTION, "invalid content rejected");

    /* ---- dungeon items (bit 1=map,2=compass,4=bigkey) + keys ---- */
    Port_DebugAction_SetDungeonItem(3, 0, 1); /* map */
    Port_DebugAction_SetDungeonItem(3, 2, 1); /* big key */
    CHECK((gSave.dungeonItems[3] & 1) && (gSave.dungeonItems[3] & 4), "map+bigkey set");
    CHECK(!(gSave.dungeonItems[3] & 2), "compass not set");
    Port_DebugAction_SetDungeonItem(3, 0, 0);
    CHECK(!(gSave.dungeonItems[3] & 1), "map cleared");
    Port_DebugAction_SetDungeonKeys(3, 5);
    CHECK(gSave.dungeonKeys[3] == 5, "dungeon keys set");

    /* ---- flag browser raw bit round-trip ---- */
    Port_DebugAction_SetFlag(1, 10, 1); /* bank 1 (offset 0x100), index 10 -> bit 0x10A */
    CHECK(Port_DebugQuery_Flag(1, 10) == 1, "flag set reads back");
    CHECK((gSave.flags[0x10A >> 3] >> (0x10A & 7)) & 1, "flag bit at correct absolute position");
    Port_DebugAction_SetFlag(1, 10, 0);
    CHECK(Port_DebugQuery_Flag(1, 10) == 0, "flag cleared");

    /* ---- stat clamp to wallet capacity ---- */
    Port_DebugAction_SetStat(1 /* shells */, 5000);
    CHECK(gSave.stats.shells == 999, "shells clamped to 999");
    Port_DebugAction_SetStat(2 /* max hearts */, 99);
    CHECK(gSave.stats.maxHealth == 20 * 8, "max hearts clamped to 20 (=0xA0)");
    gSave.stats.walletType = 2; /* cap 500 */
    Port_DebugAction_SetStat(0 /* rupees */, 5000);
    CHECK(gSave.stats.rupees == 500, "rupees clamped to wallet-tier cap (500)");

    if (g_fails == 0) {
        fprintf(stderr, "DEBUG-ACTIONS REGRESSION OK\n");
        return 0;
    }
    fprintf(stderr, "DEBUG-ACTIONS REGRESSION: %d failure(s)\n", g_fails);
    return 1;
}
