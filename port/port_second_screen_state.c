#include "port_second_screen_state.h"

#include <string.h>

#ifdef __ANDROID__

#include <pthread.h>

#include "area.h"
#include "game.h"
#include "item.h"
#include "itemMetaData.h"
#include "main.h"
#include "player.h"
#include "room.h"
#include "save.h"

static SecondScreenSnapshot sSnapshot;
static pthread_mutex_t sSnapshotMutex = PTHREAD_MUTEX_INITIALIZER;

/* Pending tap-to-equip request from the UI thread, consumed by Publish().
 * itemId 0 = nothing pending. Guarded by sSnapshotMutex — same one-memcpy
 * discipline, never held across engine calls. */
static uint8_t sPendingEquipItem = 0;
static uint8_t sPendingEquipSlot = 0;

/* Port-side automap: which rooms of each area have been entered this
 * session. TMC's own per-room "visited" state is scattered across
 * area-specific local flags with no uniform room->flag mapping, so the
 * port tracks it directly — same approach as zelda3-android's visited-room
 * dungeon map. Game-thread only. */
static uint64_t sVisitedByArea[256];

void Port_SecondScreenState_Publish(void) {
    /* Assembled outside the lock: this runs on the game thread itself, the
     * same thread that owns gRoomControls/gPlayerEntity/gSave during normal
     * gameplay, so reading them here is exactly as safe as any other engine
     * code doing so — no cross-thread race on this side. The lock only
     * needs to guard the swap into sSnapshot, which the second-screen
     * thread does read cross-thread. */
    SecondScreenSnapshot next;
    memset(&next, 0, sizeof(next));

    uint8_t equipItem;
    uint8_t equipSlot;
    pthread_mutex_lock(&sSnapshotMutex);
    equipItem = sPendingEquipItem;
    equipSlot = sPendingEquipSlot;
    sPendingEquipItem = 0;
    pthread_mutex_unlock(&sSnapshotMutex);

    next.inGame = gMain.task == TASK_GAME;
    if (next.inGame) {
        /* Tap-to-equip goes through the engine's own path (swap handling,
         * HUD refresh) and only for items actually in the inventory — a
         * stale tap from a previous save can't equip something Link
         * doesn't own. */
        if (equipItem != 0 && GetInventoryValue(equipItem) == 1) {
            ForceEquipItem(equipItem, equipSlot ? EQUIP_SLOT_B : EQUIP_SLOT_A);
        }

        next.area = gRoomControls.area;
        next.room = gRoomControls.room;
        next.playerX = gPlayerEntity.base.x.HALF.HI;
        next.playerY = gPlayerEntity.base.y.HALF.HI;
        next.equippedA = gSave.stats.equipped[SLOT_A];
        next.equippedB = gSave.stats.equipped[SLOT_B];
        next.equippedSlotA = next.equippedA ? gItemMetaData[next.equippedA].menuSlot : 0xFF;
        next.equippedSlotB = next.equippedB ? gItemMetaData[next.equippedB].menuSlot : 0xFF;
        next.health = gSave.stats.health;
        next.maxHealth = gSave.stats.maxHealth;
        next.rupees = gSave.stats.rupees;

        /* Area identity + per-dungeon save state, gated exactly like the
         * engine gates it: dungeonKeys/dungeonItems are only meaningful
         * where AreaHasKeys() holds (src/gameUtils.c reads them through
         * gArea.dungeon_idx under that same check). */
        next.areaFlags = gArea.areaMetadata;
        next.dungeonIdx = gArea.dungeon_idx;
        if ((next.areaFlags & SECOND_SCREEN_AR_HAS_KEYS) && next.dungeonIdx < 0x10) {
            next.dungeonKeys = gSave.dungeonKeys[next.dungeonIdx];
            next.dungeonItemBits = gSave.dungeonItems[next.dungeonIdx];
        }

        /* Quest state for the status strip — plain gSave field reads. */
        for (u32 i = 0; i < 4; i++) {
            if (GetInventoryValue(ITEM_EARTH_ELEMENT + i) == 1) {
                next.elements |= (uint8_t)(1u << i);
            }
        }
        next.walletType = gSave.stats.walletType & 3;
        next.walletMax = gWalletSizes[next.walletType].size;
        next.bombCount = gSave.stats.bombCount;
        next.bombMax = gBombBagSizes[gSave.stats.bombBagType & 3];
        next.arrowCount = gSave.stats.arrowCount;
        next.arrowMax = gQuiverSizes[gSave.stats.quiverType & 3];
        next.kinstoneFused = gSave.kinstones.fusedCount;
        for (u32 i = 0; i < 19; i++) {
            next.kinstoneBag += gSave.kinstones.amounts[i];
        }
        {
            /* Owned figurines = set bits in the save's figurine bitset. */
            u32 n = 0;
            for (u32 i = 0; i < sizeof(gSave.figurines); i++) {
                u8 b = gSave.figurines[i];
                while (b) {
                    n += b & 1;
                    b >>= 1;
                }
            }
            next.figurineCount = n > 255 ? 255 : (uint8_t)n;
        }
        next.windcrests = gSave.windcrests;
        memcpy(next.fusedKinstones, gSave.kinstones.fusedKinstones, sizeof(next.fusedKinstones));
        memcpy(next.fusionUnmarked, gSave.kinstones.fusionUnmarked, sizeof(next.fusionUnmarked));

        /* Mirror of the pause menu's item-screen fill loop
         * (src/menu/pauseMenu.c: PauseMenu_ItemMenu_Init): every owned
         * activatable item lands in its ItemMetaData menu slot; later item
         * ids overwrite earlier ones in the same slot, exactly like the
         * real menu. */
        for (u32 item = ITEM_SMITH_SWORD; item < ITEM_BOTTLE_EMPTY; item++) {
            if (GetInventoryValue(item) == 1) {
                u32 slot = gItemMetaData[item].menuSlot;
                if (slot < SECOND_SCREEN_ITEM_SLOTS) {
                    next.menuItems[slot] = (uint8_t)item;
                }
            }
        }
        for (u32 i = 0; i < 4; i++) {
            next.bottleContents[i] = gSave.stats.bottles[i];
        }

        for (u32 i = 0; i < SECOND_SCREEN_MAX_ROOMS && i < MAX_ROOMS; i++) {
            const RoomResInfo* info = &gArea.roomResInfos[i];
            next.rooms[i].x = info->map_x;
            next.rooms[i].y = info->map_y;
            next.rooms[i].w = info->pixel_width;
            next.rooms[i].h = info->pixel_height;
        }

        sVisitedByArea[next.area] |= 1ull << (next.room & 63);
        next.visitedMask = sVisitedByArea[next.area];
    }

    pthread_mutex_lock(&sSnapshotMutex);
    sSnapshot = next;
    pthread_mutex_unlock(&sSnapshotMutex);
}

void Port_SecondScreenState_Read(SecondScreenSnapshot* out) {
    pthread_mutex_lock(&sSnapshotMutex);
    *out = sSnapshot;
    pthread_mutex_unlock(&sSnapshotMutex);
}

void Port_SecondScreenState_RequestEquip(uint8_t itemId, uint8_t slot) {
    pthread_mutex_lock(&sSnapshotMutex);
    sPendingEquipItem = itemId;
    sPendingEquipSlot = slot;
    pthread_mutex_unlock(&sSnapshotMutex);
}

#else /* !__ANDROID__ — no second-screen thread to publish to. */

void Port_SecondScreenState_Publish(void) {}

void Port_SecondScreenState_Read(SecondScreenSnapshot* out) {
    memset(out, 0, sizeof(*out));
}

void Port_SecondScreenState_RequestEquip(uint8_t itemId, uint8_t slot) {
    (void)itemId;
    (void)slot;
}

#endif
