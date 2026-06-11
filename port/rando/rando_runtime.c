/*
 * port/rando/rando_runtime.c — MinishMaker `!eventdefine` runtime features.
 *
 * MinishMaker compiles eventdefines into ROM patches; natively we read the
 * same defines through the rando_logic API and mutate engine state directly.
 *
 * Built as C (not C++) because it needs the real SaveFile/Stats layout from
 * include/save.h + include/player.h, and player.h names parameters `this`.
 *
 * Hook sites:
 *  - src/fileselect.c Port_FileSelectRando_StartSlot -> Rando_Runtime_OnNewFile
 *    (exactly once per newly committed rando file, then persisted).
 *  - src/fileselect.c SetActiveSave -> Rando_Runtime_Refresh (every save
 *    activation; getters also self-refresh on seed change).
 *  - src/gameUtils.c ModHealth -> Rando_Runtime_DamageMultiplier.
 *  - port/port_audio_mute.cpp Port_AudioMute_ShouldSuppress ->
 *    Rando_Runtime_MuteLowHealthBeep / Rando_Runtime_MuteMusic.
 */

#include "common.h"    /* AddKinstoneToBag */
#include "player.h"    /* Stats, Get/SetInventoryValue, gBombBagSizes, gQuiverSizes */
#include "save.h"      /* gSave, gSaveHeader */
#include "item.h"      /* Item ids */
#include "windcrest.h" /* WINDCREST_* bit indices */
#include "room.h"      /* GetRoomProperty, TileEntity */
#include "main.h"      /* gMain, Task */
#include "sound.h"     /* SoundReq, SFX_MENU_CANCEL */

#include "rando/rando.h"
#include "rando/rando_logic.h"
#include "rando/rando_runtime.h"

#include <stdio.h>
#include <string.h>

/* gSave.windcrests crest byte: MinishMaker's crest defines are bit values
 * inside the upper byte (crenel=0x01 ... minish=0x80), i.e. bits 24..31. */
#define RANDO_WINDCREST_SHIFT 24
static_assert(WINDCREST_MT_CRENEL == 24 && WINDCREST_MINISH_WOODS == 31,
              "windcrest bit layout changed");

/* ------------------------------------------------------------------ */
/* eventdefine helpers                                                  */
/* ------------------------------------------------------------------ */

static bool HasDefine(const char* name) {
    bool has_value = false;
    return RandoLogic_HasEventDefine(name, &has_value);
}

/* Value of a define, or `fallback` when undefined/flag-only/unparseable. */
static u32 DefineValue(const char* name, u64 seed, u32 fallback) {
    u32 v = 0;
    if (RandoLogic_EvalEventDefine(name, seed, &v)) {
        return v;
    }
    return fallback;
}

/* ------------------------------------------------------------------ */
/* Start inventory                                                      */
/* ------------------------------------------------------------------ */

/* `startInventory<Suffix>` flag defines that map 1:1 onto a 2-bit
 * inventory slot (mirrors GiveItem()'s plain SetInventoryValue path,
 * minus the equip/SFX/textbox side effects that are unsafe at file-
 * creation time). */
static const struct {
    const char* suffix;
    u8 item;
} kSimpleGrants[] = {
    { "SmithSword", ITEM_SMITH_SWORD },
    { "GreenSword", ITEM_GREEN_SWORD },
    { "RedSword", ITEM_RED_SWORD },
    { "BlueSword", ITEM_BLUE_SWORD },
    { "FourSword", ITEM_FOURSWORD },
    { "Shield", ITEM_SHIELD },
    { "MirrorShield", ITEM_MIRROR_SHIELD },
    { "Lantern", ITEM_LANTERN_OFF },
    { "GustJar", ITEM_GUST_JAR },
    { "Pacci", ITEM_PACCI_CANE },
    { "MoleMitts", ITEM_MOLE_MITTS },
    { "Cape", ITEM_ROCS_CAPE },
    { "Boots", ITEM_PEGASUS_BOOTS },
    { "Ocarina", ITEM_OCARINA },
    { "Flippers", ITEM_FLIPPERS },
    { "GripRing", ITEM_GRIP_RING },
    { "PowerBracelets", ITEM_POWER_BRACELETS },
    { "EarthElement", ITEM_EARTH_ELEMENT },
    { "FireElement", ITEM_FIRE_ELEMENT },
    { "WaterElement", ITEM_WATER_ELEMENT },
    { "WindElement", ITEM_WIND_ELEMENT },
    { "Book1", ITEM_QST_BOOK1 },
    { "Book2", ITEM_QST_BOOK2 },
    { "Book3", ITEM_QST_BOOK3 },
    { "GraveyardKey", ITEM_QST_GRAVEYARD_KEY },
    { "RanchKey", ITEM_QST_LONLON_KEY },
    { "DogFood", ITEM_QST_DOGFOOD },
    { "Mushroom", ITEM_QST_MUSHROOM },
    { "JabberNut", ITEM_JABBERNUT },
    { "TingleTrophy", ITEM_QST_TINGLE_TROPHY },
    { "CarlovMedal", ITEM_QST_CARLOV_MEDAL },
    { "ScrollSpin", ITEM_SKILL_SPIN_ATTACK },
    { "ScrollRoll", ITEM_SKILL_ROLL_ATTACK },
    { "ScrollDash", ITEM_SKILL_DASH_ATTACK },
    { "ScrollRock", ITEM_SKILL_ROCK_BREAKER },
    { "ScrollSwordBeam", ITEM_SKILL_SWORD_BEAM },
    { "ScrollGreatSpin", ITEM_SKILL_GREAT_SPIN },
    { "ScrollDownThrust", ITEM_SKILL_DOWN_THRUST },
    { "ScrollPerilBeam", ITEM_SKILL_PERIL_BEAM },
    { "ScrollFastSpin", ITEM_SKILL_FAST_SPIN },
    { "ScrollFastSplit", ITEM_SKILL_FAST_SPLIT },
    { "ScrollLongSpin", ITEM_SKILL_LONG_SPIN },
    { "ArrowButterfly", ITEM_ARROW_BUTTERFLY },
    { "DigButterfly", ITEM_DIG_BUTTERFLY },
    { "SwimButterfly", ITEM_SWIM_BUTTERFLY },
};

/* Kinstone bag piece-type ids: gKinstoneWorldEvents entries 0x65..0x75
 * (src/common.c). Shapes cross-checked against the fusion table: gold
 * cloud pieces 0x65..0x69 (shapes 1/2/3/3/2 for the 5 cloud fusions),
 * gold swamp 0x6A..0x6C (Castor Wilds statues), gold falls crown 0x6D,
 * red W/V/E 0x6E/0x6F/0x70 (9/7/8 fusions), blue L/S 0x71/0x72,
 * green C/G/P 0x73/0x74/0x75 (17/16/16 fusions). */
static const u8 kKinstoneGoldCloud[] = { 0x65, 0x66, 0x67, 0x68, 0x69 };
static const u8 kKinstoneGoldSwamp[] = { 0x6A, 0x6B, 0x6C };
static const u8 kKinstoneGoldFalls[] = { 0x6D };
static const u8 kKinstoneRedW[] = { 0x6E };
static const u8 kKinstoneRedV[] = { 0x6F };
static const u8 kKinstoneRedE[] = { 0x70 };
static const u8 kKinstoneBlueL[] = { 0x71 };
static const u8 kKinstoneBlueS[] = { 0x72 };
static const u8 kKinstoneGreenC[] = { 0x73 };
static const u8 kKinstoneGreenG[] = { 0x74 };
static const u8 kKinstoneGreenP[] = { 0x75 };

static void GrantKinstones(const u8* types, u32 type_count, u32 count) {
    u32 i;
    if (count > 99u * type_count) {
        count = 99u * type_count; /* AddKinstoneToBag caps each type at 99 */
    }
    /* Round-robin across the family's piece types (matters for the
     * multi-shape gold families; single-type families just repeat). */
    for (i = 0; i < count; i++) {
        AddKinstoneToBag(types[i % type_count]);
    }
}

static const struct {
    const char* suffix;
    const u8* types;
    u8 type_count;
} kKinstoneGrants[] = {
    { "KinstonesGoldCloud", kKinstoneGoldCloud, 5 },
    { "KinstonesGoldSwamp", kKinstoneGoldSwamp, 3 },
    { "KinstonesGoldFalls", kKinstoneGoldFalls, 1 },
    { "KinstonesRedW", kKinstoneRedW, 1 },
    { "KinstonesRedV", kKinstoneRedV, 1 },
    { "KinstonesRedE", kKinstoneRedE, 1 },
    { "KinstonesBlueL", kKinstoneBlueL, 1 },
    { "KinstonesBlueS", kKinstoneBlueS, 1 },
    { "KinstonesGreenC", kKinstoneGreenC, 1 },
    { "KinstonesGreenG", kKinstoneGreenG, 1 },
    { "KinstonesGreenP", kKinstoneGreenP, 1 },
};

/* Families whose members interact (mutual exclusion, counts, capacity
 * clamps) are collected during the scan and resolved afterwards so the
 * outcome does not depend on eventdefine declaration order. */
typedef struct {
    bool bow, light_arrows;
    bool boomerang, magic_boomerang;
    bool remote_bombs;
    u32 bomb_bags; /* 1..4 = number of bags */
    u32 bombs;
    bool bombs_set;
    u32 quivers; /* 1..3 = quiver upgrades */
    u32 arrows;
    bool arrows_set;
    u32 wallets; /* 1..3 = wallet upgrades */
    bool bottle[4];
    u32 bottle_content[4];
} StartInventory;

static void ApplyStartInventoryFamilies(const StartInventory* si) {
    /* Bow / light arrows: GiveItem (case 0xb) also makes sure the base
     * quiver exists so the arrow counter works. */
    if (si->bow) {
        SetInventoryValue(ITEM_BOW, 1);
    }
    if (si->light_arrows) {
        SetInventoryValue(ITEM_LIGHT_ARROW, 1);
    }
    if (si->quivers != 0) {
        gSave.stats.quiverType = si->quivers > 3 ? 3 : (u8)si->quivers;
    }
    if (si->bow || si->light_arrows) {
        u32 cap;
        if (GetInventoryValue(ITEM_LARGE_QUIVER) == 0) {
            SetInventoryValue(ITEM_LARGE_QUIVER, 1);
        }
        cap = gQuiverSizes[gSave.stats.quiverType];
        gSave.stats.arrowCount = (u8)(si->arrows_set && si->arrows < cap ? si->arrows : cap);
    }

    /* Bombs: remote bombs and normal bombs are mutually exclusive 2-bit
     * slots (GiveItem cases 7/8). */
    if (si->bomb_bags != 0) {
        u32 extra = si->bomb_bags - 1;
        gSave.stats.bombBagType = extra > 3 ? 3 : (u8)extra;
        SetInventoryValue(ITEM_BOMBS, 1);
    }
    if (si->remote_bombs) {
        SetInventoryValue(ITEM_REMOTE_BOMBS, 1);
        SetInventoryValue(ITEM_BOMBS, 0);
    }
    if (si->bomb_bags != 0 || si->remote_bombs) {
        u32 cap = gBombBagSizes[gSave.stats.bombBagType];
        gSave.stats.bombCount = (u8)(si->bombs_set && si->bombs < cap ? si->bombs : cap);
    }

    /* Boomerangs: mutually exclusive (GiveItem case 0x12); the magic
     * boomerang wins when both are defined. */
    if (si->magic_boomerang) {
        SetInventoryValue(ITEM_MAGIC_BOOMERANG, 1);
        SetInventoryValue(ITEM_BOOMERANG, 0);
    } else if (si->boomerang) {
        SetInventoryValue(ITEM_BOOMERANG, 1);
    }

    if (si->wallets != 0) {
        gSave.stats.walletType = si->wallets > 3 ? 3 : (u8)si->wallets;
    }

    /* Bottles: slot flag + content byte (0x20 = empty; randomized
     * contents are (RAND%0x12)+0x20, i.e. 0x20..0x31). */
    {
        u32 i;
        for (i = 0; i < 4; i++) {
            u32 content;
            if (!si->bottle[i]) {
                continue;
            }
            SetInventoryValue(ITEM_BOTTLE1 + i, 1);
            content = si->bottle_content[i];
            if (content < ITEM_BOTTLE_EMPTY || content > BOTTLE_CHARM_DIN) {
                content = ITEM_BOTTLE_EMPTY;
            }
            gSave.stats.bottles[i] = (u8)content;
        }
    }
}

static void ApplyStartInventory(u64 seed) {
    StartInventory si;
    u32 count = RandoLogic_GetEventDefineCount();
    u32 i, j;
    u32 granted = 0;

    memset(&si, 0, sizeof(si));

    for (i = 0; i < count; i++) {
        const char* name = RandoLogic_GetEventDefineName(i);
        const char* suffix;
        bool handled = false;

        if (name == NULL || strncmp(name, "startInventory", 14) != 0) {
            continue;
        }
        suffix = name + 14;

        for (j = 0; j < (u32)(sizeof(kSimpleGrants) / sizeof(kSimpleGrants[0])); j++) {
            if (strcmp(suffix, kSimpleGrants[j].suffix) == 0) {
                SetInventoryValue(kSimpleGrants[j].item, 1);
                granted++;
                handled = true;
                break;
            }
        }
        if (handled) {
            continue;
        }

        for (j = 0; j < (u32)(sizeof(kKinstoneGrants) / sizeof(kKinstoneGrants[0])); j++) {
            if (strcmp(suffix, kKinstoneGrants[j].suffix) == 0) {
                GrantKinstones(kKinstoneGrants[j].types, kKinstoneGrants[j].type_count,
                               DefineValue(name, seed, 1));
                granted++;
                handled = true;
                break;
            }
        }
        if (handled) {
            continue;
        }

        if (strcmp(suffix, "Bow") == 0) {
            si.bow = true;
        } else if (strcmp(suffix, "LightArrows") == 0) {
            si.light_arrows = true;
        } else if (strcmp(suffix, "Boomerang") == 0) {
            si.boomerang = true;
        } else if (strcmp(suffix, "MagicBoomerang") == 0) {
            si.magic_boomerang = true;
        } else if (strcmp(suffix, "RemoteBombs") == 0) {
            si.remote_bombs = true;
        } else if (strcmp(suffix, "BombBags") == 0) {
            si.bomb_bags = DefineValue(name, seed, 1);
        } else if (strcmp(suffix, "Bombs") == 0) {
            si.bombs = DefineValue(name, seed, 0);
            si.bombs_set = true;
        } else if (strcmp(suffix, "Quivers") == 0) {
            si.quivers = DefineValue(name, seed, 0);
        } else if (strcmp(suffix, "Arrows") == 0) {
            si.arrows = DefineValue(name, seed, 0);
            si.arrows_set = true;
        } else if (strcmp(suffix, "Wallets") == 0) {
            si.wallets = DefineValue(name, seed, 0);
        } else if (strncmp(suffix, "Bottle", 6) == 0 && suffix[6] >= '1' && suffix[6] <= '4' &&
                   suffix[7] == '\0') {
            si.bottle[suffix[6] - '1'] = true;
        } else if (strncmp(suffix, "BottleContent", 13) == 0 && suffix[13] >= '1' &&
                   suffix[13] <= '4' && suffix[14] == '\0') {
            si.bottle_content[suffix[13] - '1'] = DefineValue(name, seed, ITEM_BOTTLE_EMPTY);
        } else {
            fprintf(stderr, "[RANDO] start-inventory: unmapped '%s'\n", name);
            continue;
        }
        granted++;
    }

    ApplyStartInventoryFamilies(&si);

    if (granted != 0) {
        fprintf(stderr, "[RANDO] start inventory: %u define(s) applied\n", granted);
    }
}

/* ------------------------------------------------------------------ */
/* Crests, portals, instant text                                        */
/* ------------------------------------------------------------------ */

static void ApplyCrests(u64 seed) {
    /* Each define evaluates to its crest bit or 0x00 (both branches are
     * always emitted by default.logic). Crest byte lives in bits 24..31
     * of gSave.windcrests (see src/object/windcrest.c, include/windcrest.h). */
    static const char* const kCrestDefines[] = {
        "crenelCrest", "fallsCrest", "cloudCrest", "townCrest",
        "lakeCrest",   "swampCrest", "southCrest", "minishCrest",
    };
    u32 mask = 0;
    u32 i;
    for (i = 0; i < (u32)(sizeof(kCrestDefines) / sizeof(kCrestDefines[0])); i++) {
        mask |= DefineValue(kCrestDefines[i], seed, 0) & 0xFFu;
    }
    if (mask != 0) {
        gSave.windcrests |= mask << RANDO_WINDCREST_SHIFT;
        fprintf(stderr, "[RANDO] wind crests pre-opened: 0x%02X\n", mask);
    }
}

static void ApplyPortals(u64 seed) {
    /* gSave.dungeonWarps[] is indexed by gArea.dungeon_idx =
     * AreaHeader.location - 23 (src/gameUtils.c). Bit 0 = blue portal,
     * bit 1 = red portal (EnableDungeonWarp). The defines evaluate to
     * 0x01/0x02 or 0x00. */
    static const struct {
        const char* blue;
        const char* red;
        u8 dungeon_idx;
    } kPortals[] = {
        { "dwsBluePortal", "dwsRedPortal", 1 }, /* Deepwood Shrine (24-23) */
        { "cofBluePortal", "cofRedPortal", 2 }, /* Cave of Flames */
        { "fowBluePortal", "fowRedPortal", 3 }, /* Fortress of Winds */
        { "todBluePortal", "todRedPortal", 4 }, /* Temple of Droplets */
        { "powBluePortal", "powRedPortal", 5 }, /* Palace of Winds */
        { "dhcBluePortal", "dhcRedPortal", 6 }, /* Dark Hyrule Castle */
    };
    u32 i;
    u32 opened = 0;
    for (i = 0; i < (u32)(sizeof(kPortals) / sizeof(kPortals[0])); i++) {
        u32 bits = (DefineValue(kPortals[i].blue, seed, 0) |
                    DefineValue(kPortals[i].red, seed, 0)) &
                   0x3u;
        if (bits != 0) {
            gSave.dungeonWarps[kPortals[i].dungeon_idx] |= (u8)bits;
            opened++;
        }
    }
    if (opened != 0) {
        fprintf(stderr, "[RANDO] dungeon portals pre-opened in %u dungeon(s)\n", opened);
    }
}

static void ApplyInstantText(void) {
    if (HasDefine("instantText")) {
        /* msg_speed indexes speeds[] = {5, 3, 1} in src/message.c
         * TextDispUpdate; 2 is the fastest native setting. */
        gSave.msg_speed = 2;
        gSaveHeader->msg_speed = 2;
        fprintf(stderr, "[RANDO] instant text enabled\n");
    }
}

/* ------------------------------------------------------------------ */
/* Per-seed cached runtime state                                        */
/* ------------------------------------------------------------------ */

static struct {
    bool active;
    u64 seed;
    int damage_multiplier;
    bool mute_low_health_beep;
    bool mute_music;
} sRuntime = { false, 0, 1, false, false };

void Rando_Runtime_Refresh(void) {
    u64 seed;
    u32 v;

    sRuntime.active = Rando_IsActive();
    sRuntime.seed = Rando_GetSeed64();
    sRuntime.damage_multiplier = 1;
    sRuntime.mute_low_health_beep = false;
    sRuntime.mute_music = false;

    if (!sRuntime.active) {
        return;
    }
    seed = sRuntime.seed;

    /* dmgMulti is the full multiplier and wins over heroMode (MinishMaker
     * emits heroMode alongside every dmgMulti > 1; applying both would
     * double-count). */
    if (RandoLogic_EvalEventDefine("dmgMulti", seed, &v) && v >= 2 && v <= 4) {
        sRuntime.damage_multiplier = (int)v;
    } else if (HasDefine("heroMode")) {
        sRuntime.damage_multiplier = 2;
    }

    /* lowHealthBeep = frames between beeps (HEALTH_BEEP numberbox;
     * vanilla 90). The native beep cadence is fixed at 90 frames
     * (src/interrupts.c), so only "0 = off" is honored. */
    if (RandoLogic_EvalEventDefine("lowHealthBeep", seed, &v)) {
        if (v == 0) {
            sRuntime.mute_low_health_beep = true;
        } else if (v != 90) {
            fprintf(stderr, "[RANDO] lowHealthBeep interval %u unsupported (only 0=off); keeping vanilla 90\n", v);
        }
    }

    sRuntime.mute_music = HasDefine("no_music");

    if (sRuntime.damage_multiplier != 1 || sRuntime.mute_low_health_beep || sRuntime.mute_music) {
        fprintf(stderr, "[RANDO] runtime: damage x%d%s%s\n", sRuntime.damage_multiplier,
                sRuntime.mute_low_health_beep ? ", low-health beep off" : "",
                sRuntime.mute_music ? ", music off" : "");
    }
}

/* The cheap getters self-refresh when the active seed changes so every
 * activation path (file select, sidecar reload, F8 menu) is covered even
 * without an explicit Rando_Runtime_Refresh() call. */
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

/* ------------------------------------------------------------------ */
/* New-file commit                                                      */
/* ------------------------------------------------------------------ */

void Rando_Runtime_OnNewFile(void) {
    u64 seed;
    if (!Rando_IsActive()) {
        return;
    }
    seed = Rando_GetSeed64();
    fprintf(stderr, "[RANDO] applying new-file grants (seed %llu)\n",
            (unsigned long long)seed);
    ApplyStartInventory(seed);
    ApplyCrests(seed);
    ApplyPortals(seed);
    ApplyInstantText();
    Rando_Runtime_Refresh();
}

unsigned Rando_GetChestLocalFlag(unsigned area, unsigned room, unsigned chestIndex) {
    TileEntity* te = (TileEntity*)GetRoomProperty(area, room, 3);
    int index = 0;
    if (te == NULL) return 0xFF;
    for (int i = 0; te[i].tilePos != 0; ++i) {
        if (te[i].type != SMALL_CHEST && te[i].type != BIG_CHEST) continue;
        if (index == (int)chestIndex) return te[i].localFlag;
        index++;
    }
    return 0xFF;
}

unsigned Rando_GetDungeonKeyCount(unsigned dungeon_idx) {
    if (dungeon_idx >= 16) return 0;
    return gSave.dungeonKeys[dungeon_idx];
}

bool Rando_GetDungeonHasBigKey(unsigned dungeon_idx) {
    if (dungeon_idx >= 16) return false;
    return (gSave.dungeonItems[dungeon_idx] & 2) != 0;
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
