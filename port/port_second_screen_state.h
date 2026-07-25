#ifndef PORT_SECOND_SCREEN_STATE_H
#define PORT_SECOND_SCREEN_STATE_H

/*
 * Thread-safe game-state snapshot for the second-screen render thread.
 *
 * Modeled on port_m4a_backend.cpp's sStateMutex pattern: the game thread
 * publishes a small copy of the fields the second screen needs once per
 * tick; the second-screen render thread only ever reads that published
 * copy, never gSave/gRoomControls/gPlayerEntity directly. Lock is held only
 * for a memcpy on both sides — never around anything that renders or
 * blocks, so the second-screen thread can never stall the game thread.
 *
 * Input flows the other way through the same file: the UI thread requests
 * an equip (tap on the item grid) via Port_SecondScreenState_RequestEquip;
 * Publish() applies it on the game thread through the engine's own
 * ForceEquipItem, so the save file is only ever touched from the thread
 * that owns it.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Matches MAX_ROOMS (include/area.h). */
#define SECOND_SCREEN_MAX_ROOMS 64

/* MENU_SLOT_SWORD..MENU_SLOT_BOTTLE3 (include/itemMetaData.h) — the 16
 * equipable slots of the pause menu's item screen, excluding the save
 * button pseudo-slots. */
#define SECOND_SCREEN_ITEM_SLOTS 16

typedef struct {
    uint16_t x, y; /* room origin within the area, pixels (RoomResInfo.map_x/y) */
    uint16_t w, h; /* room size, pixels; w==0 or h==0 -> slot unused */
} SecondScreenRoom;

/* gArea.areaMetadata bits (include/area.h AreaStateFlags) — republished
 * verbatim so the render side applies the same gating the engine does
 * (AreaIsDungeon / AreaHasKeys / AreaHasMap in src/gameUtils.c). */
#define SECOND_SCREEN_AR_IS_OVERWORLD 0x01
#define SECOND_SCREEN_AR_HAS_KEYS 0x02
#define SECOND_SCREEN_AR_IS_DUNGEON 0x04
#define SECOND_SCREEN_AR_HAS_MAP 0x08

typedef struct {
    uint8_t inGame; /* gMain.task == TASK_GAME; nothing else below is valid otherwise */
    uint8_t area;
    uint8_t room;
    uint8_t equippedA;
    uint8_t equippedB;
    /* Menu slot (grid index) each equipped item lives in, 0xFF if none.
     * Published alongside the raw ids because variants of one item (e.g.
     * lantern lit/unlit) share a slot but differ in id — highlighting by
     * slot matches what the real pause menu marks. */
    uint8_t equippedSlotA;
    uint8_t equippedSlotB;
    uint8_t health;    /* 8 units per heart */
    uint8_t maxHealth;
    uint16_t rupees;
    int32_t playerX; /* area-space pixels */
    int32_t playerY;
    /* Area identity: SECOND_SCREEN_AR_* bits plus the dungeon slot the
     * engine's own key/compass logic indexes with. */
    uint8_t areaFlags;  /* gArea.areaMetadata */
    uint8_t dungeonIdx; /* gArea.dungeon_idx */
    /* Current dungeon's save state (zeroed outside key-bearing areas):
     * key count plus the raw gSave.dungeonItems byte — bit 0 map, bit 1
     * compass, bit 2 big key, exactly what HasDungeonMap/Compass/BigKey
     * (src/gameUtils.c) test. */
    uint8_t dungeonKeys;
    uint8_t dungeonItemBits;
    /* Quest state for the status strip. */
    uint8_t elements;   /* bit n set = ITEM_EARTH_ELEMENT + n owned */
    uint8_t walletType; /* picks the HUD rupee icon tier */
    uint16_t walletMax; /* gWalletSizes[walletType].size; maxed => yellow digits */
    uint8_t bombCount;
    uint8_t bombMax;
    uint8_t arrowCount;
    uint8_t arrowMax;
    uint8_t kinstoneFused; /* gSave.kinstones.fusedCount */
    uint8_t figurineCount; /* set bits in gSave.figurines[] (owned figurines) */
    uint16_t kinstoneBag; /* pieces currently in the bag (sum of amounts[]) */
    /* gSave.windcrests verbatim; bits 24..31 are the WindcrestID unlock
     * flags (include/windcrest.h) the fast-travel screen checks. */
    uint32_t windcrests;
    /* Pause-menu item screen contents: menuItems[menuSlot] = item id, 0 if
     * that slot is empty. Bottles report the ITEM_BOTTLE1..4 container id;
     * bottleContents[] carries what's inside for icon display. */
    uint8_t menuItems[SECOND_SCREEN_ITEM_SLOTS];
    uint8_t bottleContents[4];
    /* Rooms of the current area, indexed by room id. */
    SecondScreenRoom rooms[SECOND_SCREEN_MAX_ROOMS];
    /* Bit n set = room n of the current area has been entered this session
     * (port-side automap tracking, zelda3-android "visited rooms" style). */
    uint64_t visitedMask;
} SecondScreenSnapshot;

/* Called once per game tick from the main loop (src/main.c). Builds a fresh
 * snapshot from gRoomControls/gPlayerEntity/gSave/gArea, applies any
 * pending equip request, and swaps the snapshot in under a short-held
 * lock. No-op off Android (there is no second-screen thread to publish
 * to). */
void Port_SecondScreenState_Publish(void);

/* Called from the second-screen render thread. Copies the most recently
 * published snapshot into `out`. Safe to call even before the first
 * Publish() — returns a zeroed snapshot in that case. */
void Port_SecondScreenState_Read(SecondScreenSnapshot* out);

/* Called from the UI/JNI thread when the player taps an item on the second
 * screen. slot: 0 = A button, 1 = B button. Applied (and validated against
 * the inventory) on the game thread during the next Publish(); redundant
 * or invalid requests are dropped there. */
void Port_SecondScreenState_RequestEquip(uint8_t itemId, uint8_t slot);

#ifdef __cplusplus
}
#endif

#endif /* PORT_SECOND_SCREEN_STATE_H */
