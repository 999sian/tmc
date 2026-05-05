/*
 * port_debug_actions.c — C-only shim that exposes game-state mutations
 * for the (C++) debug menu. Keeping these in C avoids pulling the game
 * headers (which use `this` as a C parameter name) into C++ TUs where
 * they would not parse.
 */

#include "save.h"
#include "room.h"
#include "global.h"
#include "common.h"
#include "main.h"
#include "player.h"
#include "item.h"
#include "transitions.h"

void DoExitTransition(const Transition* data);
void LoadItemGfx(void);

/* Set a single 2-bit slot in gSave.inventory[] without touching neighbours. */
static void SetItem(unsigned int item, unsigned int value) {
    unsigned int byteIdx = item / 4;
    unsigned int shift = (item % 4) * 2;
    if (byteIdx >= sizeof(gSave.inventory)) {
        return;
    }
    gSave.inventory[byteIdx] = (gSave.inventory[byteIdx] & ~(3u << shift)) | ((value & 3u) << shift);
}

void Port_DebugAction_GiveAllItems(void) {
    /* The inventory array uses 2 bits per item, and the *index* range
     * [0..0x69] covers a mix of: real equipment (sword/bow/boomerang/...)
     * AND virtual ids used as bottle-contents tags + quest items + drops
     * + progress flags. Setting every byte to a fixed pattern (0xFF or
     * 0x55) lights up entries the pause menu doesn't expect to draw
     * sprites for, which corrupts the inventory grid and the menu
     * renders black. Whitelist the real equipment items + key progress
     * items only. */

    /* Weapons / equippable items — basic versions only. The "upgrade
     * pairs" (bombs/remote-bombs, boomerang/magic-boomerang, shield/
     * mirror-shield, lantern off/on) are mutually exclusive in the
     * normal upgrade flow; setting both members of a pair confuses the
     * item-use dispatcher and Link can render with the wrong held-item
     * sprite (e.g. pot). Only set one member of each pair. */
    SetItem(ITEM_SMITH_SWORD, 1);
    SetItem(ITEM_BOMBS, 1);
    SetItem(ITEM_BOW, 1);
    SetItem(ITEM_BOOMERANG, 1);
    SetItem(ITEM_SHIELD, 1);
    SetItem(ITEM_LANTERN_OFF, 1);
    SetItem(ITEM_GUST_JAR, 1);
    SetItem(ITEM_PACCI_CANE, 1);
    SetItem(ITEM_MOLE_MITTS, 1);
    SetItem(ITEM_ROCS_CAPE, 1);
    SetItem(ITEM_PEGASUS_BOOTS, 1);
    SetItem(ITEM_FIRE_ROD, 1);
    SetItem(ITEM_OCARINA, 1);

    /* Elements / quest progression. */
    SetItem(ITEM_EARTH_ELEMENT, 1);
    SetItem(ITEM_FIRE_ELEMENT, 1);
    SetItem(ITEM_WATER_ELEMENT, 1);
    SetItem(ITEM_WIND_ELEMENT, 1);

    /* Movement + reach upgrades. */
    SetItem(ITEM_GRIP_RING, 1);
    SetItem(ITEM_POWER_BRACELETS, 1);
    SetItem(ITEM_FLIPPERS, 1);

    /* Capacity upgrades + bag. */
    SetItem(ITEM_WALLET, 1);
    SetItem(ITEM_BOMBBAG, 1);
    SetItem(ITEM_LARGE_QUIVER, 1);
    SetItem(ITEM_KINSTONE_BAG, 1);

    /* LoadItemGfx() chooses bomb/boomerang VRAM gfx based on the
     * upgraded-or-basic inventory bits and writes them to the slots
     * Link's item-use entities reference. Without this, the sprite
     * VRAM still holds whatever the previous area put there (commonly
     * the pot graphic) and using the boomerang renders Link as a pot. */
    LoadItemGfx();
}

void Port_DebugAction_MaxHearts(void) {
    /* health/maxHealth are stored in eighths-of-hearts (each heart-container
     * pickup in script.c:1663 adds 8 and caps at 0xA0). 20 hearts = 160 = 0xA0.
     * The previous value (80) silently set 10 hearts and matched the in-game
     * cap that fileselect/ui paths special-case at 10 — see #52. */
    gSave.stats.maxHealth = 0xA0;
    gSave.stats.health = gSave.stats.maxHealth;
}

void Port_DebugAction_HealFull(void) {
    gSave.stats.health = gSave.stats.maxHealth;
}

void Port_DebugAction_MaxRupees(void) {
    gSave.stats.rupees = 999;
}

void Port_DebugAction_AllKinstones(void) {
    int i;
    for (i = 0; i < (int)sizeof(gSave.kinstones.fuserOffers); i++) {
        gSave.kinstones.fuserOffers[i] = 0xFF;
    }
    for (i = 0; i < (int)sizeof(gSave.kinstones.fusedKinstones); i++) {
        gSave.kinstones.fusedKinstones[i] = 0xFF;
    }
    gSave.kinstones.fusedCount = 100;
    gSave.kinstones.didAllFusions = 1;
}

/* Trigger a warp by building a Transition struct in place and handing it
 * to DoExitTransition() — the exact path the wallmaster + scripted area
 * exits use, so the player ends up properly initialized (correct spawn
 * type, facing direction, layer, fade type). Returns 0 if it can't fire
 * right now (game not in TASK_GAME, or player dying), 1 if armed.
 *
 * The previous hand-rolled version wrote gRoomTransition fields directly,
 * which (a) didn't run the area-warp transition-type setup so dungeon
 * entries arrived without proper spawn handling, and (b) didn't clear
 * stairs_idx, letting CheckRoomExit's StairsAreValid() cancel the warp
 * into a stairs-spawn state. */
int Port_DebugAction_Warp(unsigned char area, unsigned char room,
                          unsigned short x, unsigned short y, unsigned char layer) {
    Transition t;
    if (gMain.task != TASK_GAME) {
        return 0;
    }
    if (gSave.stats.health == 0 || gPlayerState.framestate == PL_STATE_DIE) {
        return 0;
    }

    t.warp_type = WARP_TYPE_AREA;
    t.startX = 0;
    t.startY = 0;
    t.endX = x;
    t.endY = y;
    t.shape = 0;
    t.area = area;
    t.room = room;
    t.layer = layer;
    t.transition_type = 2; /* matches wallmaster table - PL_SPAWN_DOOR-ish */
    t.facing_direction = 0; /* face down on arrival */
    t.transitionSFX = 0;    /* SFX_NONE */
    t.unk2 = 0;
    t.unk3 = 0;

    gRoomTransition.stairs_idx = 0; /* prevent StairsAreValid() cancellation */
    DoExitTransition(&t);
    return 1;
}
