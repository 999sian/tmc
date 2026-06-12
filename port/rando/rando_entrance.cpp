/*
 * port/rando/rando_entrance.cpp — coupled dungeon-entrance shuffle.
 *
 * Mirrors the GBA randomizer's entrance-shuffle ROM writes as a runtime
 * remap.
 * All tuples below were cross-checked against the decomp's own data:
 *
 *   enter doors      src/data/transitions.c (exit lists; see kEnter notes)
 *   exit doors       src/data/transitions.c (dungeon entrance-room lists)
 *   ToD/PoW warps    src/data/screenTransitions.c (gUnk_0813AB58/gUnk_0813AD88)
 *   element warps    src/data/screenTransitions.c (gUnk_0813AB6C/gUnk_0813ABBC)
 *   PoW ledge hole   src/manager/holeManager.c (gHoleTransitions, tower-roof entry)
 *
 * Semantics (coupled swap): entering the physical door of entrance-location L
 * leads into the dungeon assigned to L; every exit of that dungeon deposits
 * the player back at L's exterior. Fixed tables only, no heap, no per-frame
 * work — hooks run once per room transition.
 */

#include "rando/rando.h"
#include "rando/rando_logic.h"
#include "rando/rando_entrance.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace {

constexpr int kEntranceCount = 8; /* Items.Entrance.0x01 .. 0x08 */

/* Index convention: 0=Deepwood 1=CoF 2=Fortress 3=Droplets 4=Crypt 5=Palace
 * 6=DHC main 7=DHC side — both for entrance LOCATIONS (physical doors) and
 * for entrance ITEMS (dungeon interiors); vanilla pairing is the identity. */
const char* const kLocationNames[kEntranceCount] = {
    "Deepwood_Entrance", "CoF_Entrance",    "Fortress_Entrance", "Droplets_Entrance",
    "Crypt_Entrance",    "Palace_Entrance", "DHC_Main_Entrance", "DHC_Side_Entrance",
};

struct EntranceTuple {
    uint8_t area;
    uint8_t room;
    uint16_t x, y;
    uint8_t layer;
    uint8_t spawn_type; /* TransitionType / PlayerRoomStatus.spawn_type */
    uint8_t facing;     /* PlayerRoomStatus.start_anim */
};

/* Interior entry points, i.e. what entering dungeon d writes into
 * PlayerRoomStatus (the entrance-shuffle entry tuple: x/y + target area/room).
 * Vanilla carriers of these tuples:
 *   [0] gExitList_MinishWoods_Main[1] / _DeepwoodShrineEntry_Main[0] / _61_0[0]
 *   [1] gExitList_MtCrenel_CaveOfFlamesEntrance[7]
 *   [2] gExitList_Ruins_FortressEntrance[0]
 *   [3] gUnk_0813AB58 (Lake Hylia minish portal, PT_TOD -> drop-in)
 *   [4] gExitList_RoyalValley_Main[0]
 *   [5] gUnk_0813AD88[3] (Wind Tribe tower-roof whirlwind -> hop-in)
 *   [6] gUnk_08134FBC[0] (post-DHC Castle Garden list)
 *   [7] gExitList_CastleGarden_Main[3] / gUnk_08134FBC[3] (castle cellar)   */
const EntranceTuple kEnter[kEntranceCount] = {
    { 0x48, 0x0B, 0x0A8, 0x0D8, 1, 0, 0 },  /* Deepwood Shrine entrance room   */
    { 0x50, 0x03, 0x088, 0x0A8, 1, 0, 0 },  /* Cave of Flames entrance room    */
    { 0x18, 0x00, 0x1D8, 0x0B0, 1, 0, 0 },  /* Outer Fortress entrance hall    */
    { 0x60, 0x03, 0x108, 0x0C8, 2, 2, 4 },  /* Temple of Droplets (drop-in)    */
    { 0x68, 0x08, 0x088, 0x078, 1, 0, 0 },  /* Royal Crypt entrance room       */
    { 0x70, 0x31, 0x268, 0x058, 1, 10, 6 }, /* Palace of Winds (hop-in)        */
    { 0x88, 0x00, 0x198, 0x1F0, 1, 0, 0 },  /* DHC 1F entrance                 */
    { 0x43, 0x00, 0x068, 0x1A8, 1, 0, 0 },  /* Hyrule Castle cellar (DHC side) */
};

/* Exterior door-return points of each LOCATION (the door-return tuple:
 * exit x/y + entrance area/room). Vanilla carriers:
 *   [0] gExitList_DeepwoodShrine_Entrance[2]   -> Deepwood porch (area 0x4A)
 *   [1] gExitList_CaveOfFlames_Entrance[0]     -> Mt Crenel CoF door room
 *   [2] gExitList_OuterFortressOfWinds_EntranceHall[5] -> Wind Ruins
 *   [3] gUnk_0813AD88[0] (waterspout)          -> Lake Hylia (type 9)
 *   [4] gExitList_RoyalCrypt_Entrance[0]       -> Royal Valley
 *   [5] gHoleTransitions tower-roof entry      -> Wind Tribe tower roof
 *   [6] gExitList_DarkHyruleCastle_1FEntrance[2] -> Castle Garden
 *   [7] gExitList_GreatFairies_Entrance[0] (cellar room 0) -> Castle Garden */
const EntranceTuple kExterior[kEntranceCount] = {
    { 0x4A, 0x00, 0x078, 0x064, 1, 0, 4 }, /* Deepwood Shrine porch       */
    { 0x06, 0x02, 0x068, 0x088, 1, 0, 4 }, /* Mt Crenel, CoF door room    */
    { 0x05, 0x04, 0x198, 0x028, 1, 0, 4 }, /* Wind Ruins, fortress screen */
    { 0x0B, 0x00, 0x128, 0x188, 1, 9, 4 }, /* Lake Hylia main             */
    { 0x09, 0x00, 0x0F0, 0x03C, 1, 0, 4 }, /* Royal Valley main           */
    { 0x31, 0x00, 0x078, 0x068, 1, 2, 4 }, /* Wind Tribe tower roof       */
    { 0x07, 0x00, 0x1F8, 0x038, 1, 0, 4 }, /* Castle Garden (main door)   */
    { 0x07, 0x00, 0x068, 0x058, 1, 0, 0 }, /* Castle Garden (cellar door) */
};

/* Post-boss green-warp landing per LOCATION. area/room as kExterior; x/y
 * decoded from the packed green-warp exit coordinate c:
 * x=((c&0x3f)<<4)+8, y=((c&0xfc0)>>2)+8 (WarpPoint_Action5 packing). */
struct WarpXY {
    uint16_t x, y;
};
const WarpXY kGreenWarpXY[kEntranceCount] = {
    { 0x078, 0x078 }, /* 0x01C7 */ { 0x068, 0x098 }, /* 0x0246 */
    { 0x198, 0x058 }, /* 0x0159 */ { 0x128, 0x1A8 }, /* 0x0692 */
    { 0x0E8, 0x098 }, /* 0x024E */ { 0x078, 0x078 }, /* 0x01C7 */
    { 0x1F8, 0x038 }, /* 0x00DF */ { 0x068, 0x058 }, /* 0x0146 */
};

/* Element-get warp landing per LOCATION (the element-get warp landing tuple). */
const WarpXY kElementWarpXY[kEntranceCount] = {
    { 0x078, 0x078 }, { 0x068, 0x098 }, { 0x198, 0x068 }, { 0x128, 0x1A8 },
    { 0x0E8, 0x098 }, { 0x078, 0x078 }, { 0x1F8, 0x038 }, { 0x068, 0x058 },
};

/* Vanilla element-get warp destinations (only FoW and Crypt end their
 * element/king cutscene on a dedicated warp; src/data/screenTransitions.c
 * gUnk_0813AB6C and gUnk_0813ABBC, fired through npc4E -> DoExitTransition).
 * x == 0 marks "none". */
const EntranceTuple kElementWarpVanilla[kEntranceCount] = {
    {}, {},
    { 0x05, 0x04, 0x198, 0x068, 1, 0, 4 }, /* FoW after Wind Element  */
    {}, /* ToD: ends on green warp */
    { 0x09, 0x00, 0x0F0, 0x0BC, 1, 0, 4 }, /* Crypt after King's gift */
    {}, {}, {},
};

/* Interior area sets per dungeon (main + boss/sub areas), used to decide
 * which dungeon the player is leaving. 0 terminates (area 0 = Minish Woods
 * is never a dungeon interior). */
const uint8_t kInteriorAreas[kEntranceCount][5] = {
    { 0x48, 0x49, 0, 0, 0 },          /* DWS, DWS boss                       */
    { 0x50, 0x51, 0, 0, 0 },          /* CoF, CoF boss                       */
    { 0x18, 0x58, 0x59, 0x5A, 0 },    /* Outer FoW, FoW, FoW top, In. Mazaal */
    { 0x60, 0, 0, 0, 0 },             /* ToD                                 */
    { 0x68, 0, 0, 0, 0 },             /* Royal Crypt                         */
    { 0x70, 0x71, 0, 0, 0 },          /* PoW, PoW boss                       */
    { 0x88, 0x89, 0x8D, 0, 0 },       /* DHC, DHC outside, DHC bridge        */
    { 0x43, 0, 0, 0, 0 },             /* Hyrule Castle cellar                */
};

/* assignment: location -> dungeon (entrance item subtype - 1), -1 = none. */
int8_t sAssign[kEntranceCount];
int8_t sInverse[kEntranceCount]; /* dungeon -> location */
bool sEnabled = false;
bool sCacheValid = false;
uint64_t sCacheSeed = 0;
uint32_t sCacheLocCount = 0;

int DungeonFromInteriorArea(uint8_t area) {
    for (int d = 0; d < kEntranceCount; ++d) {
        for (int i = 0; i < 5 && kInteriorAreas[d][i] != 0; ++i) {
            if (kInteriorAreas[d][i] == area) return d;
        }
    }
    return -1;
}

bool TupleMatches(const EntranceTuple& t, uint8_t area, uint8_t room, int16_t x, int16_t y) {
    return t.area == area && t.room == room && (int16_t)t.x == x && (int16_t)t.y == y;
}

/* Refresh the location->dungeon mapping from the logic engine. Cached per
 * (seed, raw location count); cheap to redo when a new seed is generated. */
bool EnsureMapping() {
    if (!Rando_IsActive()) {
        sCacheValid = false;
        return false;
    }
    uint64_t seed = Rando_GetSeed64();
    uint32_t loc_count = RandoLogic_GetLocationCountRaw();
    if (sCacheValid && sCacheSeed == seed && sCacheLocCount == loc_count) return sEnabled;

    sCacheValid = true;
    sCacheSeed = seed;
    sCacheLocCount = loc_count;
    sEnabled = false;
    for (int i = 0; i < kEntranceCount; ++i) {
        sAssign[i] = -1;
        sInverse[i] = -1;
    }

    for (uint32_t i = 0; i < loc_count; ++i) {
        const char* name = RandoLogic_GetLocationName(i);
        if (name == nullptr || name[0] == '\0') continue;
        for (int l = 0; l < kEntranceCount; ++l) {
            if (strcmp(name, kLocationNames[l]) != 0) continue;
            int subtype = RandoLogic_GetEntranceAssignment(i);
            if (subtype >= 1 && subtype <= kEntranceCount) sAssign[l] = (int8_t)(subtype - 1);
            break;
        }
    }

    for (int l = 0; l < kEntranceCount; ++l) {
        int d = sAssign[l];
        if (d < 0) continue;
        if (sInverse[d] != -1) {
            /* Duplicate assignment: corrupt logic state; refuse to remap. */
            fprintf(stderr, "[RANDO] entrance shuffle: duplicate assignment for dungeon %d, disabling\n", d + 1);
            sEnabled = false;
            return false;
        }
        sInverse[d] = (int8_t)l;
        sEnabled = true;
    }

    if (sEnabled) {
        /* Coupled swaps need every exit mapped; a partial assignment set
         * (some -1) would strand the player. Require a full bijection. */
        for (int l = 0; l < kEntranceCount; ++l) {
            if (sAssign[l] < 0) {
                fprintf(stderr, "[RANDO] entrance shuffle: incomplete assignment (%s empty), disabling\n",
                        kLocationNames[l]);
                sEnabled = false;
                return false;
            }
        }
        fprintf(stderr, "[RANDO] entrance shuffle active:");
        for (int l = 0; l < kEntranceCount; ++l) {
            fprintf(stderr, " %d->%d", l + 1, sAssign[l] + 1);
        }
        fprintf(stderr, " (location->dungeon, 1=DWS..8=DHCside)\n");
    }
    return sEnabled;
}

void WriteTuple(const EntranceTuple& t, uint8_t* area, uint8_t* room, int16_t* x, int16_t* y, uint8_t* layer,
                uint8_t* spawn_type, uint8_t* facing) {
    *area = t.area;
    *room = t.room;
    *x = (int16_t)t.x;
    *y = (int16_t)t.y;
    *layer = t.layer;
    *spawn_type = t.spawn_type;
    *facing = t.facing;
}

} // namespace

extern "C" void Rando_Entrance_RemapExit(uint8_t cur_area, uint8_t* area, uint8_t* room, int16_t* x, int16_t* y,
                                         uint8_t* layer, uint8_t* spawn_type, uint8_t* facing) {
    if (!EnsureMapping()) return;

    /* Entering a dungeon door: destination matches the vanilla interior
     * entry of dungeon d0 == door of location l0=d0 (vanilla pairing). The
     * source-area guard keeps in-dungeon warps that share the entrance-room
     * destination (e.g. the Royal Crypt wallmaster) untouched. */
    for (int d0 = 0; d0 < kEntranceCount; ++d0) {
        if (!TupleMatches(kEnter[d0], *area, *room, *x, *y)) continue;
        if (DungeonFromInteriorArea(cur_area) == d0) return; /* internal warp */
        int target = sAssign[d0];
        if (target < 0 || target == d0) return;
        WriteTuple(kEnter[target], area, room, x, y, layer, spawn_type, facing);
        fprintf(stderr, "[RANDO] entrance remap: door %s -> dungeon %d\n", kLocationNames[d0], target + 1);
        return;
    }

    /* Leaving dungeon V (door exit or element-get warp): redirect to the
     * exterior of the location V is assigned to. */
    int v = DungeonFromInteriorArea(cur_area);
    if (v < 0) return;
    int loc = sInverse[v];
    if (loc < 0 || loc == v) return;

    if (TupleMatches(kExterior[v], *area, *room, *x, *y)) {
        WriteTuple(kExterior[loc], area, room, x, y, layer, spawn_type, facing);
        fprintf(stderr, "[RANDO] entrance remap: dungeon %d exit -> %s\n", v + 1, kLocationNames[loc]);
        return;
    }
    const EntranceTuple& ev = kElementWarpVanilla[v];
    if (ev.x != 0 && TupleMatches(ev, *area, *room, *x, *y)) {
        EntranceTuple out = kExterior[loc];
        out.x = kElementWarpXY[loc].x;
        out.y = kElementWarpXY[loc].y;
        WriteTuple(out, area, room, x, y, layer, spawn_type, facing);
        fprintf(stderr, "[RANDO] entrance remap: dungeon %d element warp -> %s\n", v + 1, kLocationNames[loc]);
    }
}

extern "C" void Rando_Entrance_RemapHole(uint8_t cur_area, uint8_t* area, uint8_t* room, uint8_t* layer, int16_t* x,
                                         int16_t* y) {
    if (!EnsureMapping()) return;
    /* Palace of Winds entrance-room ledge jump: vanilla hole lands on the
     * Wind Tribe tower roof (kExterior[5]). The GBA randomizer rewrites only
     * area/room/layer/x/y of this gHoleTransitions entry. */
    if (cur_area != kEnter[5].area) return;
    if (!TupleMatches(kExterior[5], *area, *room, *x, *y)) return;
    int loc = sInverse[5];
    if (loc < 0 || loc == 5) return;
    *area = kExterior[loc].area;
    *room = kExterior[loc].room;
    *layer = 1;
    *x = (int16_t)kExterior[loc].x;
    *y = (int16_t)kExterior[loc].y;
    fprintf(stderr, "[RANDO] entrance remap: PoW ledge exit -> %s\n", kLocationNames[loc]);
}

extern "C" void Rando_Entrance_RemapGreenWarp(uint8_t cur_area, uint32_t warp_type, uint8_t* area, uint8_t* room,
                                              int16_t* x, int16_t* y) {
    if (warp_type != 2) return; /* only the post-boss (green) warp leaves */
    if (!EnsureMapping()) return;
    int v = DungeonFromInteriorArea(cur_area);
    if (v < 0) return;
    int loc = sInverse[v];
    if (loc < 0) return;
    /* The GBA randomizer rewrites the in-dungeon green warp unconditionally for
     * every shuffled dungeon (ToD's even points back inside in vanilla), so
     * identity assignments are normalized to the exterior pad as well. */
    *area = kExterior[loc].area;
    *room = kExterior[loc].room;
    *x = (int16_t)kGreenWarpXY[loc].x;
    *y = (int16_t)kGreenWarpXY[loc].y;
    fprintf(stderr, "[RANDO] entrance remap: dungeon %d green warp -> %s\n", v + 1, kLocationNames[loc]);
}
