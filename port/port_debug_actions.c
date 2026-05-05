/*
 * port_debug_actions.c — C-only shim that exposes game-state mutations
 * for the (C++) debug menu. Keeping these in C avoids pulling the game
 * headers (which use `this` as a C parameter name) into C++ TUs where
 * they would not parse.
 */

#include "save.h"
#include "room.h"
#include "area.h"
#include "global.h"
#include "common.h"
#include "main.h"
#include "player.h"
#include "item.h"
#include "transitions.h"

void DoExitTransition(const Transition* data);
void LoadItemGfx(void);
extern bool32 Port_IsRoomHeaderPtrReadable(const void* ptr);
extern void Port_RefreshAreaData(unsigned int area);

#define DEBUG_AREA_COUNT 0x90 /* matches kAreaCount in port_asset_loader.cpp */

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

/* ------------------------------------------------------------------ */
/*  Debug-menu enumeration helpers                                    */
/* ------------------------------------------------------------------ */

/* Resolve and validate the per-area RoomHeader table pointer, refreshing
 * via Port_RefreshAreaData if the cached pointer doesn't look ROM-backed.
 * Returns NULL for unmapped/invalid areas. */
static RoomHeader* DebugResolveRoomTable(unsigned char area) {
    RoomHeader* table;
    if (area >= DEBUG_AREA_COUNT) {
        return NULL;
    }
    table = gAreaRoomHeaders[area];
    if (!Port_IsRoomHeaderPtrReadable(table)) {
        Port_RefreshAreaData(area);
        table = gAreaRoomHeaders[area];
    }
    if (!Port_IsRoomHeaderPtrReadable(table)) {
        return NULL;
    }
    return table;
}

/* Count contiguous valid rooms (non-zero pixel_width) starting at index 0.
 * Returns 0 when the area has no room data at all. */
int Port_DebugQuery_AreaRoomCount(unsigned char area) {
    RoomHeader* table = DebugResolveRoomTable(area);
    int count = 0;
    int r;

    if (!table) {
        return 0;
    }
    for (r = 0; r < MAX_ROOMS; r++) {
        if (table[r].pixel_width == 0) {
            break;
        }
        count++;
    }
    return count;
}

/* Fill *w / *h with room dimensions in pixels. Returns 1 on success, 0
 * if the area/room is not mapped or the room has zero size. Caller may
 * pass NULL out-pointers to just probe validity. */
int Port_DebugQuery_RoomDimensions(unsigned char area, unsigned char room,
                                   unsigned short* w, unsigned short* h) {
    RoomHeader* table = DebugResolveRoomTable(area);
    if (!table || room >= MAX_ROOMS) {
        return 0;
    }
    if (table[room].pixel_width == 0) {
        return 0;
    }
    if (w) {
        *w = table[room].pixel_width;
    }
    if (h) {
        *h = table[room].pixel_height;
    }
    return 1;
}

/* Friendly area names for the warp menu. NULL entries mean "no useful
 * name" (typically NULL_xx unused slots from include/area.h); the menu
 * falls back to "Area 0xXX" for those. Indexed by AreaID. Keep aligned
 * with the AreaID enum in include/area.h. */
static const char* const kAreaNames[DEBUG_AREA_COUNT] = {
    [0x00] = "Minish Woods",
    [0x01] = "Minish Village",
    [0x02] = "Hyrule Town",
    [0x03] = "Hyrule Field",
    [0x04] = "Castor Wilds",
    [0x05] = "Wind Ruins",
    [0x06] = "Mt Crenel",
    [0x07] = "Castle Garden",
    [0x08] = "Cloud Tops",
    [0x09] = "Royal Valley",
    [0x0A] = "Veil Falls",
    [0x0B] = "Lake Hylia",
    [0x0C] = "Lake Woods Cave",
    [0x0D] = "Beanstalks",
    [0x0F] = "Hyrule Dig Caves",
    [0x10] = "Melari's Mines",
    [0x11] = "Minish Paths",
    [0x12] = "Crenel Minish Paths",
    [0x13] = "Dig Caves",
    [0x14] = "Crenel Dig Cave",
    [0x15] = "Festival Town",
    [0x16] = "Veil Falls Dig Cave",
    [0x17] = "Castor Wilds Dig Cave",
    [0x18] = "Outer Fortress of Winds",
    [0x19] = "Hylia Dig Caves",
    [0x1A] = "Veil Falls Top",
    [0x20] = "Minish House Interiors",
    [0x21] = "House Interiors 1",
    [0x22] = "House Interiors 2",
    [0x23] = "House Interiors 3",
    [0x24] = "Tree Interiors",
    [0x25] = "Dojos",
    [0x26] = "Crenel Caves",
    [0x27] = "Minish Cracks",
    [0x28] = "House Interiors 4",
    [0x29] = "Great Fairies",
    [0x2A] = "Castor Caves",
    [0x2B] = "Castor Darknut",
    [0x2C] = "Armos Interiors",
    [0x2D] = "Town Minish Holes",
    [0x2E] = "Minish Rafters",
    [0x2F] = "Goron Cave",
    [0x30] = "Wind Tribe Tower",
    [0x31] = "Wind Tribe Tower Roof",
    [0x32] = "Caves",
    [0x33] = "Veil Falls Caves",
    [0x34] = "Royal Valley Graves",
    [0x35] = "Minish Caves",
    [0x36] = "Castle Garden Minish Holes",
    [0x38] = "Ezlo Cutscene",
    [0x41] = "Hyrule Town Underground",
    [0x42] = "Garden Fountains",
    [0x43] = "Hyrule Castle Cellar",
    [0x44] = "Simon's Simulation",
    [0x48] = "Deepwood Shrine",
    [0x49] = "Deepwood Shrine - boss",
    [0x4A] = "Deepwood Shrine - entry",
    [0x50] = "Cave of Flames",
    [0x51] = "Cave of Flames - boss",
    [0x58] = "Fortress of Winds",
    [0x59] = "Fortress of Winds Top",
    [0x5A] = "Inner Mazaal",
    [0x60] = "Temple of Droplets",
    [0x62] = "Hyrule Town Minish Caves",
    [0x68] = "Royal Crypt",
    [0x70] = "Palace of Winds",
    [0x71] = "Palace of Winds - boss",
    [0x78] = "Sanctuary",
    [0x80] = "Hyrule Castle",
    [0x81] = "Sanctuary Entrance",
    [0x88] = "Dark Hyrule Castle",
    [0x89] = "Dark Hyrule Castle Outside",
    [0x8A] = "Vaati's Arms",
    [0x8B] = "Vaati 3",
    [0x8C] = "Vaati 2",
    [0x8D] = "Dark Hyrule Castle Bridge",
};

/* NULL when no friendly name is recorded (caller falls back to "Area 0xXX"). */
const char* Port_DebugQuery_AreaName(unsigned char area) {
    if (area >= DEBUG_AREA_COUNT) {
        return NULL;
    }
    return kAreaNames[area];
}
