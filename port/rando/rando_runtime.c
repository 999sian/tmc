/*
 * port/rando/rando_runtime.c — native randomizer runtime integrations.
 */

#include "common.h"
#include "area.h"
#include "flags.h"
#include "game.h"
#include "item.h"
#include "player.h"
#include "room.h"
#include "save.h"
#include "sound.h"
#include "transitions.h"
#include "pauseMenu.h"
#include "windcrest.h"
#include "main.h"
#include "rando/rando.h"
#include "rando/rando_logic.h"
#include "rando/rando_runtime.h"
#include "rando/rando_newfile.h"
#include "port_softslots.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define RANDO_WINDCREST_SHIFT 24
static_assert(WINDCREST_MT_CRENEL == 24 && WINDCREST_MINISH_WOODS == 31, "windcrest bit layout changed");

/* First-pickup "seen" latches */
static const u8 kSeenItems[] = {
    ITEM_NONE,         ITEM_MAP,       ITEM_KINSTONE_BAG,    ITEM_SHELLS,      ITEM_DUNGEON_MAP, ITEM_COMPASS,
    ITEM_BIG_KEY,      ITEM_SMALL_KEY, ITEM_RUPEE1,          ITEM_RUPEE5,      ITEM_RUPEE20,     ITEM_RUPEE50,
    ITEM_RUPEE100,     ITEM_RUPEE200,  ITEM_KINSTONE,        ITEM_BOMBS5,      ITEM_ARROWS5,     ITEM_HEART,
    ITEM_FAIRY,        ITEM_SHELLS30,  ITEM_HEART_CONTAINER, ITEM_HEART_PIECE, ITEM_WALLET,
    ITEM_LARGE_QUIVER, ITEM_BOMBS10,   ITEM_BOMBS30,         ITEM_ARROWS10,    ITEM_ARROWS30,
};

/* New-file tables store USA-baseline bit IDs. Only LocalFlags1 has
 * region-dependent ordinals in the fat PC binary. */
static u32 BaselineSaveFlagIndex(u32 index) {
#if defined(PC_PORT) && defined(MULTI_REGION)
    if (index >= FLAG_BANK_1 && index < FLAG_BANK_2) {
        return FLAG_BANK_1 + Port_RemapBaselineLocalFlag(FLAG_BANK_1, index - FLAG_BANK_1);
    }
#endif
    return index;
}

static void ApplyStartInventory(u64 seed) {
    RandomizerSettings settings = Rando_GetSettings();
    u32 granted = 0;
    if (settings.start_sword) {
        SetInventoryValue(ITEM_SMITH_SWORD, 1);
        if (gSave.stats.equipped[SLOT_A] != ITEM_SMITH_SWORD && gSave.stats.equipped[SLOT_B] != ITEM_SMITH_SWORD) {
            PutItemOnSlot(ITEM_SMITH_SWORD);
        }
        granted++;
    }
    if (granted != 0) {
        fprintf(stderr, "[RANDO] start inventory: %u item(s) applied\n", granted);
    }
}

static void ApplyCrests(u64 seed) {
    RandomizerSettings settings = Rando_GetSettings();
    if (settings.early_crests) {
        gSave.windcrests |= 0x18u << RANDO_WINDCREST_SHIFT;
        fprintf(stderr, "[RANDO] wind crests pre-opened: 0x18\n");
    }
}

static void ApplyInstantText(void) {
    RandomizerSettings settings = Rando_GetSettings();
    if (settings.instant_text) {
        gSave.msg_speed = 2;
        gSaveHeader->msg_speed = 2;
        fprintf(stderr, "[RANDO] instant text enabled\n");
    }
}

static void ApplyStorySkip(void) {
    SetGlobalFlag(START);
    SetGlobalFlag(EZERO_1ST);
    SetGlobalFlag(TABIDACHI);
    SetGlobalFlag(OUTDOOR);
    SetGlobalFlag(ENTRANCE_0);
    SetLocalFlagByBankB(FLAG_BANK_1, MORI_00_KOBITO);
    SetLocalFlagByBankB(FLAG_BANK_1, MORI_ENTRANCE_1ST);
    SetLocalFlagByBankB(FLAG_BANK_1, SOUGEN_01_ZELDA);
    SetLocalFlagByBankB(FLAG_BANK_1, SOUGEN_06_WAKAGI_1);
    SetLocalFlagByBankB(FLAG_BANK_1, SOUGEN_06_WAKAGI_2);
    SetLocalFlagByBankB(FLAG_BANK_1, SOUGEN_06_WAKAGI_3);
    SetLocalFlagByBankB(FLAG_BANK_1, SOUGEN_06_AKINDO);
    SetLocalFlagByBankB(FLAG_BANK_1, CASTLE_04_MEZAME);
    SetLocalFlagByBankB(FLAG_BANK_1, MACHI_01_DEMO);
    SetLocalFlagByBankB(FLAG_BANK_2, MHOUSE15_OP1ST);
    SetLocalFlagByBankB(FLAG_BANK_2, M_PRIEST_TALK);
    SetLocalFlagByBankB(FLAG_BANK_2, M_ELDER_TALK1ST);
    SetLocalFlagByBankB(FLAG_BANK_2, M_PRIEST_MOVE);
    SetLocalFlagByBankB(FLAG_BANK_2, KOBITO_MORI_1ST);
    SetLocalFlagByBankB(FLAG_BANK_5, LV1_0B_WALK);
    fprintf(stderr, "[RANDO] story skip: intro flags set (post-Ezlo start)\n");
}

static void ApplyWorldOpen(void) {
    RandomizerSettings settings = Rando_GetSettings();
    if (settings.open_world) {
        SetGlobalFlag(KUMOTATSUMAKI);
        SetGlobalFlag(WARP_EVENT_END);
        SetGlobalFlag(TINGLE_TALK1ST);
        SetGlobalFlag(MIZUKAKI_START);
        SetLocalFlagByBankB(FLAG_BANK_1, BEANDEMO_00);
        SetLocalFlagByBankB(FLAG_BANK_1, BEANDEMO_01);
        SetLocalFlagByBankB(FLAG_BANK_1, BEANDEMO_02);
        SetLocalFlagByBankB(FLAG_BANK_1, BEANDEMO_03);
        SetLocalFlagByBankB(FLAG_BANK_1, BEANDEMO_04);
        SetLocalFlagByBankB(FLAG_BANK_1, YAMA_04_BOMBWALL0);
        fprintf(stderr, "[RANDO] world open: speed-up flags applied\n");
    }
}

static void ApplyBaselineNewFile(u64 seed) {
    size_t count = 0;
    const u16* flags = Rando_NewFile_BaselineFlags(&count);
    size_t i;

    for (i = 0; i < count; i++) {
        WriteBit(gSave.flags, BaselineSaveFlagIndex(flags[i]));
    }

    SetLocalFlagByBankB(FLAG_BANK_10, LV6_SOTO_01_00);
    SetLocalFlagByBankB(FLAG_BANK_10, LV6_SOTO_01_01);
    SetLocalFlagByBankB(FLAG_BANK_10, LV6_SOTO_01_02);
    SetLocalFlagByBankB(FLAG_BANK_10, LV6_35_00);

    // Skip cucco rounds, leaving 1 round
    SetGlobalFlag(ANJU_LV_BIT0);
    SetGlobalFlag(ANJU_LV_BIT3);

    for (i = 0; i < (u32)(sizeof(kSeenItems) / sizeof(kSeenItems[0])); i++) {
        if (GetInventoryValue(kSeenItems[i]) == 0) {
            SetInventoryValue(kSeenItems[i], 1);
        }
    }

    // Unconditional QoL figurines
    for (i = 0; i < RANDO_NEWFILE_FIGURINE_BYTES; i++) {
        gSave.figurines[i] |= kRandoNewFileFigurines[i];
    }

    gSave.windcrests |= RANDO_NEWFILE_MAP_REVEAL_MASK;
    gSave.map_hints |= RANDO_NEWFILE_MAP_HINTS_MASK;
    gSave.saved_status.overworld_map_x = RANDO_NEWFILE_WORLDMAP_X;
    gSave.saved_status.overworld_map_y = RANDO_NEWFILE_WORLDMAP_Y;

    fprintf(stderr, "[RANDO] new-file baseline: %u flag(s) + QoL state applied\n", (unsigned)count);
}

static void ApplyLocationDisableFlags(void) {
    WriteBit(gSave.flags, BaselineSaveFlagIndex(FLAG_BANK_1 + LOST_00_ENTER));
}

static void ApplyOpenWorld(void) {
    size_t count = 0;
    const u16* flags;
    size_t i;

    if (!Rando_GetSettings().open_world) {
        return;
    }

    flags = Rando_NewFile_WorldOpenFlags(&count);
    for (i = 0; i < count; i++) {
        WriteBit(gSave.flags, BaselineSaveFlagIndex(flags[i]));
    }

    gSave.areaVisitFlags[0] |= RANDO_NEWFILE_VISIT_MASK;

    SetInventoryValue(ITEM_QST_GRAVEYARD_KEY, GetInventoryValue(ITEM_QST_GRAVEYARD_KEY) | 2u);

    fprintf(stderr, "[RANDO] open world: %u obstacle flag(s) cleared\n", (unsigned)count);
}

static struct {
    bool active;
    u64 seed;
    int damage_multiplier;
    bool mute_low_health_beep;
    bool mute_music;
    bool allow_homewarp;
    bool open_tingle;
} sRuntime = { false, 0, 1, false, false, false, false };

void Rando_Runtime_Refresh(void) {
    sRuntime.active = Rando_IsActive();
    sRuntime.seed = Rando_GetSeed64();
    sRuntime.damage_multiplier = 1;
    sRuntime.mute_low_health_beep = false;
    sRuntime.mute_music = false;
    sRuntime.allow_homewarp = false;
    sRuntime.open_tingle = false;

    if (!sRuntime.active) {
        return;
    }

    RandomizerSettings settings = Rando_GetSettings();
    sRuntime.allow_homewarp = settings.homewarp;
    sRuntime.open_tingle = settings.open_world;
}

static void EnsureFresh(void) {
    if (sRuntime.active != Rando_IsActive() || sRuntime.seed != Rando_GetSeed64()) {
        Rando_Runtime_Refresh();
    }
}

int Rando_Runtime_DamageMultiplier(void) {
    EnsureFresh();
    return sRuntime.damage_multiplier;
}

bool Rando_Runtime_MuteLowHealthBeep(void) {
    EnsureFresh();
    return sRuntime.mute_low_health_beep;
}

bool Rando_Runtime_MuteMusic(void) {
    EnsureFresh();
    return sRuntime.mute_music;
}

bool Rando_Runtime_AllowHomewarp(void) {
    EnsureFresh();
    return sRuntime.active && sRuntime.allow_homewarp;
}

bool Rando_Runtime_OpenTingleBrothers(void) {
    EnsureFresh();
    return sRuntime.active && sRuntime.open_tingle;
}

void Rando_Runtime_OnNewFile(void) {
    u64 seed;
    if (!Rando_IsActive()) {
        return;
    }
    seed = Rando_GetSeed64();
    fprintf(stderr, "[RANDO] applying new-file grants (seed %llu)\n", (unsigned long long)seed);
    ApplyBaselineNewFile(seed);
    ApplyLocationDisableFlags();
    ApplyStorySkip();
    ApplyWorldOpen();
    ApplyStartInventory(seed);
    ApplyOpenWorld();
    /* Upstream's new-file blobs precollect these rewards, but Picori's
     * native check table still shuffles them. Keep their pickups alive. */
    ClearBit(gSave.flags, BaselineSaveFlagIndex(FLAG_BANK_1 + MORI_00_H1));
    ClearBit(gSave.flags, BaselineSaveFlagIndex(FLAG_BANK_1 + YAMA_04_R00));
    ClearBit(gSave.flags, BaselineSaveFlagIndex(FLAG_BANK_1 + SOUGEN_06_R1));
    ClearBit(gSave.flags, BaselineSaveFlagIndex(FLAG_BANK_1 + SOUGEN_05_R0));
    ApplyCrests(seed);
    ApplyInstantText();
    Rando_Runtime_Refresh();
}

unsigned Rando_GetChestLocalFlag(unsigned area, unsigned room, unsigned chestIndex) {
    TileEntity* te = (TileEntity*)GetRoomProperty(area, room, 3);
    int index = 0;
    if (te == NULL)
        return 0xFF;
    for (int i = 0; i < 256 && te[i].type != 0; ++i) {
        if (te[i].type != SMALL_CHEST && te[i].type != BIG_CHEST)
            continue;
        if (index == (int)chestIndex)
            return te[i].localFlag;
        index++;
    }
    return 0xFF;
}

/* The public .logic format numbers the raw TileEntity records in a room.
 * Pickup hooks instead number only chest records, so resolve every direct
 * three-part key against the active region's room data before generation. */
bool Rando_Runtime_BindLogicChests(void) {
    if (!RandoLogic_IsLoaded())
        return false;

    unsigned bound = 0;
    unsigned failed = 0;
    /* Chest-only ordinals never exceed the raw TileEntity index. Remap lower
     * raw indexes first so the parser's duplicate-key guard does not mistake
     * another chest's still-unconverted raw key for a final-key collision. */
    for (unsigned rawOrder = 0; rawOrder < 256; ++rawOrder) {
        for (uint32_t location = 0; location < RandoLogic_GetLocationCountRaw(); ++location) {
            uint32_t rawKey = RandoLogic_GetLocationKeyAt(location);
            if (rawKey == UINT32_MAX || (rawKey & 0xFF000000u) != 0 || (rawKey & 0xFFu) != rawOrder)
                continue;

            const char* name = RandoLogic_GetLocationName(location);
            RandoLogicLocationType type = RandoLogic_GetLocationType(location);
            unsigned area = (rawKey >> 16) & 0xFFu;
            unsigned room = (rawKey >> 8) & 0xFFu;
            unsigned rawIndex = rawKey & 0xFFu;
            unsigned targetFlag = 0x100u;
            unsigned targetTilePos = 0x10000u;
            int expectedOrdinal = -1;

            /* Town's raw positions and local flags vary by region. Identify
             * these two chests by their map tiles. */
            if (strcmp(name, "Town_Inn_LedgeChest") == 0)
                targetTilePos = 0x836u;
            else if (strcmp(name, "Town_School_Roof_Chest") == 0)
                targetTilePos = 0x16Du;

            if (area < 0x90u && room < MAX_ROOMS) {
                const TileEntity* tiles = (const TileEntity*)GetRoomProperty(area, room, 3);
                if (tiles != NULL) {
                    int ordinal = 0;
                    for (unsigned i = 0; i < 256 && tiles[i].type != NONE; ++i) {
                        bool chest = tiles[i].type == SMALL_CHEST || tiles[i].type == BIG_CHEST;
                        bool selected = targetTilePos <= 0xFFFFu ?
                            (chest && tiles[i].tilePos == targetTilePos) : (i == rawIndex && chest);
                        if (selected) {
                            /* Duplicate local flags cannot distinguish the two
                             * pickups in Rando_RoomChestIndex. */
                            if (expectedOrdinal >= 0) {
                                expectedOrdinal = -1;
                                break;
                            }
                            expectedOrdinal = ordinal;
                            targetFlag = tiles[i].localFlag;
                        }
                        if (chest)
                            ++ordinal;
                    }
                }
            }

            int chestOrdinal = expectedOrdinal >= 0 ? Rando_RoomChestIndex(area, room, targetFlag) : -1;
            if (chestOrdinal == expectedOrdinal && chestOrdinal >= 0 && chestOrdinal <= 0xFF) {
                uint32_t runtimeKey = (area << 16) | (room << 8) | (unsigned)chestOrdinal;
                if (RandoLogic_SetRuntimeKeyAt(location, runtimeKey)) {
                    ++bound;
                    continue;
                }
            }

            /* An unshuffled location has no generated reward to lose. Every
             * active shuffled direct chest must bind or generation must stop. */
            if (type != RANDO_LOGIC_LOCATION_UNSHUFFLED &&
                type != RANDO_LOGIC_LOCATION_UNSHUFFLED_PRIZE) {
                fprintf(stderr, "[RANDO] chest key bind failed: %s (raw %02X-%02X-%02X)\n",
                        name, area, room, rawIndex);
                ++failed;
            }
        }
    }
    fprintf(stderr, "[RANDO] chest keys: bound %u, failed %u\n", bound, failed);
    return failed == 0;
}

unsigned Rando_GetDungeonKeyCount(unsigned dungeon_idx) {
    if (dungeon_idx >= 16)
        return 0;
    return gSave.dungeonKeys[dungeon_idx];
}

bool Rando_GetDungeonHasBigKey(unsigned dungeon_idx) {
    if (dungeon_idx >= 16)
        return false;
    /* Big key is bit 2 (0x4) of dungeonItems; bit1 (0x2) is the compass,
     * bit0 (0x1) the map — see gameUtils.c HasDungeonBigKey/Compass/Map and
     * itemMetaData.c. (The save.h field comment mislabels these.) */
    return (gSave.dungeonItems[dungeon_idx] & 4) != 0;
}

/* GiveItem (itemUtils.c cases 5/6) origin routing: a rando-shuffled dungeon
 * item carries 0x80|origin_dungeon in its subtype (RANDO_DUNGEON_ORIGIN_SUBTYPE)
 * and must credit THAT dungeon's save slot — the vanilla give path credits
 * gArea.dungeon_idx, which is wrong (or out of range) anywhere outside the
 * item's home dungeon. Returns true when the credit was applied here. */
bool Rando_RouteDungeonItem(u32 item, u32 subtype) {
    if (!Rando_IsActive() || !RANDO_SUBTYPE_HAS_ORIGIN(subtype))
        return false;
    unsigned origin = RANDO_SUBTYPE_ORIGIN(subtype);
    if (origin == 0 || origin >= 16)
        return false;
    switch (item) {
        case ITEM_SMALL_KEY:
            if (gSave.dungeonKeys[origin] < 99)
                gSave.dungeonKeys[origin]++;
            return true;
        case ITEM_DUNGEON_MAP:
            gSave.dungeonItems[origin] |= 0x1;
            return true;
        case ITEM_COMPASS:
            gSave.dungeonItems[origin] |= 0x2;
            return true;
        case ITEM_BIG_KEY:
            gSave.dungeonItems[origin] |= 0x4;
            return true;
        default:
            return false;
    }
}

bool Rando_IsInGameplay(void) {
    return gMain.task == TASK_GAME;
}

bool Rando_IsInFileSelect(void) {
    return gMain.task == TASK_FILE_SELECT;
}

void Rando_PlayCancelSfx(void) {
    SoundReq(SFX_MENU_CANCEL);
}

static u8 sHomewarpPending = 0;

void DoExitTransition(const Transition* data);

bool Rando_Homewarp_Request(void) {
    if (!Rando_Runtime_AllowHomewarp() || sHomewarpPending != 0) {
        return false;
    }
    if (gPlayerState.flags & PL_MINISH) {
        return false;
    }
    sHomewarpPending = 1;
    fprintf(stderr, "[RANDO] homewarp armed (sleeping)\n");
    return true;
}

bool Rando_Homewarp_HintVisible(void) {
    return Rando_Runtime_AllowHomewarp() && gPauseMenuOptions.screen == PauseMenuScreen_2 &&
           !(gPlayerState.flags & PL_MINISH);
}

void Rando_Homewarp_Tick(void) {
    Transition t;

    if (sHomewarpPending == 0) {
        return;
    }
    if (gMain.task != TASK_GAME || gSave.stats.health == 0) {
        sHomewarpPending = 0;
        return;
    }
    if (Port_SoftSlots_IsPauseActive()) {
        return;
    }
    sHomewarpPending = 0;

    t.warp_type = WARP_TYPE_AREA;
    t.startX = 0;
    t.startY = 0;
    t.endX = 0x90;
    t.endY = 0x38;
    t.shape = 0;
    t.area = 0x22;
    t.room = 0x15;
    t.layer = 1;
    t.transition_type = 0;
    t.facing_direction = 0;
    t.transitionSFX = 0;
    t.unk2 = 0;
    t.unk3 = 0;
    gRoomTransition.stairs_idx = 0;
    DoExitTransition(&t);
    fprintf(stderr, "[RANDO] homewarp: warped to Link's bed\n");
}
