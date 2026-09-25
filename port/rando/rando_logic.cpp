/*
 * Picori's compiled item and location tables feed the native placement and
 * reachability model.
 */

#include "rando/rando_logic.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ARRAY_COUNT(a) (sizeof(a) / sizeof((a)[0]))
#define NAME_MAX_LEN 63
#define VALUE_MAX_LEN 1023

#include "rando/picori_rules.hpp"

/* Authoritative engine item ids (shared with the C engine). */
#include "item_ids.h"
#define NITEM_NONE ITEM_NONE

typedef enum SymbolKind {
    SYMBOL_UNKNOWN = 0,
    SYMBOL_ITEM,
    SYMBOL_LOCATION,
    SYMBOL_HELPER,
} SymbolKind;

typedef enum ExprNodeType {
    EXPR_TRUE = 0,
    EXPR_SYMBOL,
    EXPR_AND,
    EXPR_OR,
    EXPR_COUNT,
    EXPR_NOT,
} ExprNodeType;

typedef struct LogicSymbol {
    char name[NAME_MAX_LEN + 1];
    SymbolKind kind;
    uint16_t index;
} LogicSymbol;

typedef struct LogicItem {
    uint16_t symbol;
    RandoLogicItemType type;
    uint16_t native_item;
    uint16_t pool_tag;     /* dungeon-id binding (interned tag) or UINT16_MAX */
} LogicItem;

typedef struct LogicLocation {
    char name[NAME_MAX_LEN + 1];
    uint16_t symbol;
    uint16_t item_symbol;
    uint16_t fixed_item_symbol;
    uint16_t expr;
    uint32_t key;
    RandoLogicLocationType type;
    bool filled_by_generation;
    bool is_helper;
    uint8_t tag_count;
    uint16_t tags[RANDO_LOGIC_MAX_LOC_TAGS];
    bool has_prize_redirect;
    uint16_t prize_redirect_tag;             /* redirect pool or UINT16_MAX = anywhere */
} LogicLocation;

typedef struct ExprNode {
    ExprNodeType type;
    uint16_t symbol;
    uint16_t first_child;
    uint16_t next_sibling;
    uint16_t threshold;
    uint16_t weight;
} ExprNode;

typedef struct LogicDefine {
    char name[NAME_MAX_LEN + 1];
    char value[VALUE_MAX_LEN + 1];
    bool has_value;
} LogicDefine;

typedef struct LogicModel {
    LogicSymbol symbols[RANDO_LOGIC_MAX_SYMBOLS];
    LogicItem items[RANDO_LOGIC_MAX_ITEMS];
    LogicLocation locations[RANDO_LOGIC_MAX_LOCATIONS];
    ExprNode nodes[RANDO_LOGIC_MAX_NODES];
    LogicDefine defines[RANDO_LOGIC_MAX_DEFINES];
    char tag_names[RANDO_LOGIC_MAX_TAGS][32];
    uint32_t symbol_count;
    uint32_t item_count;
    uint32_t location_count;
    uint32_t helper_count;
    uint32_t node_count;
    uint32_t define_count;
    uint32_t native_mapped_items;
    uint32_t tag_count;
    bool loaded;
    bool native_assignable;
    bool ensure_reachability;
    char error[128];
} LogicModel;

static LogicModel sLogic;

/* Entrance-dummy assignment from the last successful generation; -1 = none. */
static int16_t sEntranceAssign[RANDO_LOGIC_MAX_LOCATIONS];
/* Per-area music assignment (song id) from the last generation; -1 = vanilla. */
#define RANDO_LOGIC_MUSIC_AREAS 256
static int16_t sMusicAssign[RANDO_LOGIC_MUSIC_AREAS];
/* Per-location subtype from the last successful generation. Meaning depends on
 * item family (shell amount, kinstone piece id, dungeon id, ...). */
static uint8_t sGeneratedSubtypes[RANDO_LOGIC_MAX_LOCATIONS];
static uint16_t sGeneratedItems[RANDO_LOGIC_MAX_LOCATIONS];
static uint16_t sGeneratedSymbols[RANDO_LOGIC_MAX_LOCATIONS];
static uint64_t sGeneratedSeed;
static bool sHasGeneratedTable;
/* Declared settings for the current rules build. */
static RandoLogicSetting sSettings[RANDO_LOGIC_MAX_SETTINGS];
static uint32_t sSettingCount;

/* Define overrides chosen by the UI; persist across model rebuilds. */
typedef struct LogicOverride {
    char name[48];
    char value[32];
    bool has_value; /* false => "defined without value" (flag on); cleared flag = absent */
} LogicOverride;
static LogicOverride sOverrides[RANDO_LOGIC_MAX_SETTINGS];
static uint32_t sOverrideCount;

static int FindOverride(const char* name) {
    for (uint32_t i = 0; i < sOverrideCount; ++i) {
        if (strcmp(sOverrides[i].name, name) == 0) return (int)i;
    }
    return -1;
}

static void RTrim(char* s) {
    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1])) {
        s[--n] = '\0';
    }
}

static bool StartsWith(const char* s, const char* prefix) {
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

static void CopyName(char* dst, size_t dst_len, const char* src, size_t src_len) {
    if (dst_len == 0) return;
    if (src_len >= dst_len) src_len = dst_len - 1;
    memcpy(dst, src, src_len);
    dst[src_len] = '\0';
    RTrim(dst);
}

static void SetError(const char* text) {
    if (sLogic.error[0] == '\0') {
        CopyName(sLogic.error, sizeof(sLogic.error), text, strlen(text));
    }
}

static int FindDefine(const char* name) {
    for (uint32_t i = 0; i < sLogic.define_count; ++i) {
        if (strcmp(sLogic.defines[i].name, name) == 0) return (int)i;
    }
    return -1;
}

static bool DefineExists(const char* name) {
    return FindDefine(name) >= 0;
}

static const char* DefineValue(const char* name) {
    int idx = FindDefine(name);
    if (idx < 0 || !sLogic.defines[idx].has_value) return NULL;
    return sLogic.defines[idx].value;
}

static bool SetDefineValue(const char* name, const char* value, bool has_value) {
    int idx = FindDefine(name);
    if (idx < 0) {
        if (sLogic.define_count >= RANDO_LOGIC_MAX_DEFINES) {
            SetError("too many defines");
            return false;
        }
        idx = (int)sLogic.define_count++;
        CopyName(sLogic.defines[idx].name, sizeof(sLogic.defines[idx].name), name, strlen(name));
    }
    sLogic.defines[idx].has_value = has_value;
    if (has_value && value != NULL) {
        CopyName(sLogic.defines[idx].value, sizeof(sLogic.defines[idx].value), value, strlen(value));
    } else {
        sLogic.defines[idx].value[0] = '\0';
    }
    return true;
}

static uint16_t AddNode(ExprNodeType type) {
    if (sLogic.node_count >= RANDO_LOGIC_MAX_NODES) {
        SetError("too many expression nodes");
        return UINT16_MAX;
    }
    uint16_t idx = (uint16_t)sLogic.node_count++;
    sLogic.nodes[idx].type = type;
    sLogic.nodes[idx].symbol = UINT16_MAX;
    sLogic.nodes[idx].first_child = UINT16_MAX;
    sLogic.nodes[idx].next_sibling = UINT16_MAX;
    sLogic.nodes[idx].threshold = 0;
    sLogic.nodes[idx].weight = 1;
    return idx;
}

static int FindSymbol(const char* name) {
    for (uint32_t i = 0; i < sLogic.symbol_count; ++i) {
        if (strcmp(sLogic.symbols[i].name, name) == 0) return (int)i;
    }
    return -1;
}

static uint16_t FindOrAddSymbol(const char* name, SymbolKind kind) {
    int idx = FindSymbol(name);
    if (idx >= 0) {
        if (kind != SYMBOL_UNKNOWN && sLogic.symbols[idx].kind == SYMBOL_UNKNOWN) {
            sLogic.symbols[idx].kind = kind;
        }
        return (uint16_t)idx;
    }
    if (sLogic.symbol_count >= RANDO_LOGIC_MAX_SYMBOLS) {
        SetError("too many symbols");
        return UINT16_MAX;
    }
    idx = (int)sLogic.symbol_count++;
    CopyName(sLogic.symbols[idx].name, sizeof(sLogic.symbols[idx].name), name, strlen(name));
    sLogic.symbols[idx].kind = kind;
    sLogic.symbols[idx].index = UINT16_MAX;
    return (uint16_t)idx;
}

/* Pool tags ("dungeon ids"): interned bare names. Location name fields carry
 * `:Tag` capabilities; item lines bind to one tag via their third field. */
static uint16_t InternTag(const char* name, size_t len) {
    while (len > 0 && name[0] == ':') { ++name; --len; }
    if (len == 0) return UINT16_MAX;
    char tmp[32];
    CopyName(tmp, sizeof(tmp), name, len);
    for (uint32_t i = 0; i < sLogic.tag_count; ++i) {
        if (strcmp(sLogic.tag_names[i], tmp) == 0) return (uint16_t)i;
    }
    if (sLogic.tag_count >= RANDO_LOGIC_MAX_TAGS) {
        SetError("too many pool tags");
        return UINT16_MAX;
    }
    CopyName(sLogic.tag_names[sLogic.tag_count], sizeof(sLogic.tag_names[0]), tmp, strlen(tmp));
    return (uint16_t)sLogic.tag_count++;
}

static bool LocationHasTag(const LogicLocation* loc, uint16_t tag) {
    for (uint8_t i = 0; i < loc->tag_count; ++i) {
        if (loc->tags[i] == tag) return true;
    }
    return false;
}

/* Picori item symbol (with the leading "Items." stripped) -> native item id.
 * Unknown award symbols must fail generation. */
static uint16_t NativeItemFromBareName(const char* name) {
    /* Swords. */
    if (!strcmp(name, "SmithSword") || !strcmp(name, "Sword") || !strcmp(name, "Sword0")) return ITEM_SMITH_SWORD;
    if (!strcmp(name, "GreenSword")) return ITEM_GREEN_SWORD;
    if (!strcmp(name, "RedSword")) return ITEM_RED_SWORD;
    if (!strcmp(name, "BlueSword")) return ITEM_BLUE_SWORD;
    if (!strcmp(name, "FourSword")) return ITEM_FOURSWORD;
    if (!strcmp(name, "SmithSwordQuest")) return ITEM_QST_SWORD;
    if (!strcmp(name, "BrokenPicoriBlade")) return ITEM_QST_BROKEN_SWORD;
    /* Weapons / gear. */
    if (!strcmp(name, "Bombs")) return ITEM_BOMBS;
    if (!strcmp(name, "RemoteBombs")) return ITEM_REMOTE_BOMBS;
    if (!strcmp(name, "Bow")) return ITEM_BOW;
    if (!strcmp(name, "LightArrow")) return ITEM_LIGHT_ARROW;
    if (!strcmp(name, "Boomerang")) return ITEM_BOOMERANG;
    if (!strcmp(name, "MagicBoomerang")) return ITEM_MAGIC_BOOMERANG;
    if (!strcmp(name, "Shield")) return ITEM_SHIELD;
    if (!strcmp(name, "MirrorShield")) return ITEM_MIRROR_SHIELD;
    if (!strcmp(name, "Lantern") || !strcmp(name, "FlameLantern")) return ITEM_LANTERN_OFF;
    if (!strcmp(name, "GustJar")) return ITEM_GUST_JAR;
    if (!strcmp(name, "CaneOfPacci") || !strcmp(name, "PacciCane")) return ITEM_PACCI_CANE;
    if (!strcmp(name, "MoleMitts")) return ITEM_MOLE_MITTS;
    if (!strcmp(name, "RocsCape") || !strcmp(name, "RocCape")) return ITEM_ROCS_CAPE;
    if (!strcmp(name, "PegasusBoots")) return ITEM_PEGASUS_BOOTS;
    if (!strcmp(name, "FireRod") || !strcmp(name, "Firerod")) return ITEM_FIRE_ROD;
    if (!strcmp(name, "Ocarina") || !strcmp(name, "OcarinaOfWind")) return ITEM_OCARINA;
    if (!strcmp(name, "GripRing")) return ITEM_GRIP_RING;
    if (!strcmp(name, "Flippers")) return ITEM_FLIPPERS;
    if (!strcmp(name, "PowerBracelets")) return ITEM_POWER_BRACELETS;
    /* Progressive items map to the base granted item. */
    if (!strcmp(name, "ProgressiveItem.0x00")) return ITEM_SMITH_SWORD;
    if (!strcmp(name, "ProgressiveItem.0x01")) return ITEM_BOW;
    if (!strcmp(name, "ProgressiveItem.0x02")) return ITEM_BOOMERANG;
    if (!strcmp(name, "ProgressiveItem.0x03")) return ITEM_SHIELD;
    if (!strcmp(name, "ProgressiveItem.0x04")) return ITEM_SKILL_SPIN_ATTACK;
    /* Bottles. */
    if (!strcmp(name, "Bottle") || !strcmp(name, "DogFoodBottle")) return ITEM_BOTTLE1;
    /* Quest / key items. */
    if (!strcmp(name, "WakeUpMushroom") || !strcmp(name, "Mushroom")) return ITEM_QST_MUSHROOM;
    if (!strcmp(name, "LonLonKey")) return ITEM_QST_LONLON_KEY;
    if (!strcmp(name, "GraveyardKey")) return ITEM_QST_GRAVEYARD_KEY;
    if (!strcmp(name, "JabberNut") || !strcmp(name, "Jabbernut")) return ITEM_JABBERNUT;
    if (!strcmp(name, "RedBook")) return ITEM_QST_BOOK1;
    if (!strcmp(name, "GreenBook")) return ITEM_QST_BOOK2;
    if (!strcmp(name, "BlueBook")) return ITEM_QST_BOOK3;
    if (!strcmp(name, "TingleTrophy")) return ITEM_QST_TINGLE_TROPHY;
    if (!strcmp(name, "CarlovMedal")) return ITEM_QST_CARLOV_MEDAL;
    /* Elements. */
    if (!strcmp(name, "EarthElement")) return ITEM_EARTH_ELEMENT;
    if (!strcmp(name, "FireElement")) return ITEM_FIRE_ELEMENT;
    if (!strcmp(name, "WaterElement")) return ITEM_WATER_ELEMENT;
    if (!strcmp(name, "WindElement")) return ITEM_WIND_ELEMENT;
    /* Scrolls / sword techniques. */
    if (!strcmp(name, "SpinAttack") || !strcmp(name, "ScrollSpin")) return ITEM_SKILL_SPIN_ATTACK;
    if (!strcmp(name, "RollAttack")) return ITEM_SKILL_ROLL_ATTACK;
    if (!strcmp(name, "DashAttack") || !strcmp(name, "ScrollDash")) return ITEM_SKILL_DASH_ATTACK;
    if (!strcmp(name, "RockBreaker")) return ITEM_SKILL_ROCK_BREAKER;
    if (!strcmp(name, "SwordBeam")) return ITEM_SKILL_SWORD_BEAM;
    if (!strcmp(name, "GreatSpin") || !strcmp(name, "ScrollGreatSpin")) return ITEM_SKILL_GREAT_SPIN;
    if (!strcmp(name, "DownThrust")) return ITEM_SKILL_DOWN_THRUST;
    if (!strcmp(name, "PerilBeam")) return ITEM_SKILL_PERIL_BEAM;
    if (!strcmp(name, "FastSpin")) return ITEM_SKILL_FAST_SPIN;
    if (!strcmp(name, "FastSplit")) return ITEM_SKILL_FAST_SPLIT;
    if (!strcmp(name, "LongSpin")) return ITEM_SKILL_LONG_SPIN;
    /* Dungeon items. */
    if (!strcmp(name, "DungeonMap")) return ITEM_DUNGEON_MAP;
    if (!strcmp(name, "Compass")) return ITEM_COMPASS;
    if (!strcmp(name, "BigKey")) return ITEM_BIG_KEY;
    if (!strcmp(name, "SmallKey")) return ITEM_SMALL_KEY;
    /* Upgrades. */
    if (!strcmp(name, "Wallet")) return ITEM_WALLET;
    if (!strcmp(name, "BombBag")) return ITEM_BOMBBAG;
    if (!strcmp(name, "Quiver") || !strcmp(name, "LargeQuiver")) return ITEM_LARGE_QUIVER;
    if (!strcmp(name, "KinstoneBag")) return ITEM_KINSTONE_BAG;
    /* Foods. */
    if (!strcmp(name, "Brioche")) return ITEM_BRIOCHE;
    if (!strcmp(name, "Croissant")) return ITEM_CROISSANT;
    if (!strcmp(name, "PieSlice") || !strcmp(name, "Pie")) return ITEM_PIE;
    if (!strcmp(name, "CakeSlice") || !strcmp(name, "Cake")) return ITEM_CAKE;
    /* Currency / consumables / collectibles. */
    if (!strcmp(name, "Rupee1") || !strcmp(name, "Rupees1")) return ITEM_RUPEE1;
    if (!strcmp(name, "Rupee5") || !strcmp(name, "Rupees5")) return ITEM_RUPEE5;
    if (!strcmp(name, "Rupee20") || !strcmp(name, "Rupees20")) return ITEM_RUPEE20;
    if (!strcmp(name, "Rupee50") || !strcmp(name, "Rupees50")) return ITEM_RUPEE50;
    if (!strcmp(name, "Rupee100") || !strcmp(name, "Rupees100")) return ITEM_RUPEE100;
    if (!strcmp(name, "Rupee200") || !strcmp(name, "Rupees200")) return ITEM_RUPEE200;
    if (!strcmp(name, "Bombs5")) return ITEM_BOMBS5;
    if (!strcmp(name, "Bombs10")) return ITEM_BOMBS10;
    if (!strcmp(name, "Bombs30")) return ITEM_BOMBS30;
    if (!strcmp(name, "Arrows5")) return ITEM_ARROWS5;
    if (!strcmp(name, "Arrows10")) return ITEM_ARROWS10;
    if (!strcmp(name, "Arrows30")) return ITEM_ARROWS30;
    if (!strcmp(name, "Heart") || !strcmp(name, "SmallHeart")) return ITEM_HEART;
    if (!strcmp(name, "Fairy")) return ITEM_FAIRY;
    if (!strcmp(name, "HeartPiece") || !strcmp(name, "PieceOfHeart")) return ITEM_HEART_PIECE;
    if (!strcmp(name, "ArrowButterfly")) return ITEM_ARROW_BUTTERFLY;
    if (!strcmp(name, "DigButterfly")) return ITEM_DIG_BUTTERFLY;
    if (!strcmp(name, "SwimButterfly")) return ITEM_SWIM_BUTTERFLY;
    if (!strcmp(name, "HeartContainer")) return ITEM_HEART_CONTAINER;
    if (!strcmp(name, "Shells")) return ITEM_SHELLS;
    if (StartsWith(name, "Shells.")) return ITEM_SHELLS;
    if (!strcmp(name, "Shells30") || !strcmp(name, "MysteryShells")) return ITEM_SHELLS30;
    if (StartsWith(name, "Kinstone")) return ITEM_KINSTONE;
    /* Subtyped dungeon-item families: `BigKey.0x1D`, `SmallKey.0x18`,
     * `Compass.0x18`, `DungeonMap.0x18` (the subtype is the dungeon id; the
     * engine resolves the concrete key by current area). */
    if (StartsWith(name, "BigKey")) return ITEM_BIG_KEY;
    if (StartsWith(name, "SmallKey")) return ITEM_SMALL_KEY;
    if (StartsWith(name, "Compass")) return ITEM_COMPASS;
    if (StartsWith(name, "DungeonMap")) return ITEM_DUNGEON_MAP;
    return ITEM_NONE;
}

static bool ParseDotNumberSuffix(const char* name, uint8_t* out) {
    const char* dot = strrchr(name, '.');
    char* end = NULL;
    unsigned long v;
    if (dot == NULL || dot[1] == '\0' || out == NULL) return false;
    v = strtoul(dot + 1, &end, 0);
    if (end == dot + 1 || *end != '\0' || v > 0xfful) return false;
    *out = (uint8_t)v;
    return true;
}

static uint8_t NativeSubtypeFromBareName(const char* name, uint16_t item, uint8_t* gold_cloud_idx, uint8_t* gold_swamp_idx) {
    uint8_t subtype = 0;
    switch (item) {
        case ITEM_SHELLS:
            if (ParseDotNumberSuffix(name, &subtype)) return subtype;
            return 0;
        case ITEM_KINSTONE:
            if (!strcmp(name, "Kinstone.GoldenCloudTops")) {
                static const uint8_t kGoldCloud[] = { 0x65, 0x66, 0x67, 0x68, 0x69 };
                uint8_t idx = gold_cloud_idx ? *gold_cloud_idx : 0;
                if (gold_cloud_idx) *gold_cloud_idx = (uint8_t)((idx + 1) % (uint8_t)ARRAY_COUNT(kGoldCloud));
                return kGoldCloud[idx % ARRAY_COUNT(kGoldCloud)];
            }
            if (!strcmp(name, "Kinstone.GoldenSwamp")) {
                static const uint8_t kGoldSwamp[] = { 0x6A, 0x6B, 0x6C };
                uint8_t idx = gold_swamp_idx ? *gold_swamp_idx : 0;
                if (gold_swamp_idx) *gold_swamp_idx = (uint8_t)((idx + 1) % (uint8_t)ARRAY_COUNT(kGoldSwamp));
                return kGoldSwamp[idx % ARRAY_COUNT(kGoldSwamp)];
            }
            if (!strcmp(name, "Kinstone.GoldenFalls")) return 0x6D;
            if (!strcmp(name, "Kinstone.RedW")) return 0x6E;
            if (!strcmp(name, "Kinstone.RedV")) return 0x6F;
            if (!strcmp(name, "Kinstone.RedE")) return 0x70;
            if (!strcmp(name, "Kinstone.BlueL")) return 0x71;
            if (!strcmp(name, "Kinstone.BlueS")) return 0x72;
            if (!strcmp(name, "Kinstone.GreenC")) return 0x73;
            if (!strcmp(name, "Kinstone.GreenG")) return 0x74;
            if (!strcmp(name, "Kinstone.GreenP")) return 0x75;
            return 0;
        case ITEM_BIG_KEY:
        case ITEM_SMALL_KEY:
        case ITEM_COMPASS:
        case ITEM_DUNGEON_MAP:
            if (ParseDotNumberSuffix(name, &subtype) && subtype >= 0x18 && subtype <= 0x1e)
                return RANDO_DUNGEON_ORIGIN_SUBTYPE(subtype - 0x17);
            return 0;
        default:
            return 0;
    }
    return 0;
}

static uint16_t NativeItemFromSymbolName(const char* symbol_name) {
    const char* name = symbol_name;
    if (StartsWith(name, "Items.")) name += 6;
    return NativeItemFromBareName(name);
}

static bool IsDungeonAward(uint16_t item) {
    return item == ITEM_BIG_KEY || item == ITEM_SMALL_KEY ||
           item == ITEM_COMPASS || item == ITEM_DUNGEON_MAP;
}

static void AddChild(uint16_t parent, uint16_t child) {
    if (parent == UINT16_MAX || child == UINT16_MAX) return;
    if (sLogic.nodes[parent].first_child == UINT16_MAX) {
        sLogic.nodes[parent].first_child = child;
        return;
    }
    uint16_t n = sLogic.nodes[parent].first_child;
    while (sLogic.nodes[n].next_sibling != UINT16_MAX) n = sLogic.nodes[n].next_sibling;
    sLogic.nodes[n].next_sibling = child;
}

static bool AddLogicItem(const char* symbol_name, RandoLogicItemType type, uint32_t amount,
                         uint16_t pool_tag) {
    if (amount == 0) amount = 1;
    uint16_t sym = FindOrAddSymbol(symbol_name, SYMBOL_ITEM);
    if (sym == UINT16_MAX) return false;
    for (uint32_t i = 0; i < amount; ++i) {
        if (sLogic.item_count >= RANDO_LOGIC_MAX_ITEMS) {
            SetError("too many items");
            return false;
        }
        LogicItem* item = &sLogic.items[sLogic.item_count];
        item->symbol = sym;
        item->type = type;
        item->native_item = NativeItemFromSymbolName(symbol_name);
        item->pool_tag = pool_tag;
        if (item->native_item != NITEM_NONE) sLogic.native_mapped_items++;
        sLogic.symbols[sym].kind = SYMBOL_ITEM;
        sLogic.symbols[sym].index = (uint16_t)sLogic.item_count;
        sLogic.item_count++;
    }
    return true;
}

static void CopyTooltip(char* dst, size_t dst_len, const char* src) {
    /* Tooltip text carries literal "\n" (and occasional "\t") escapes. */
    size_t j = 0;
    if (src == NULL) { dst[0] = '\0'; return; }
    for (size_t i = 0; src[i] != '\0' && j + 1 < dst_len; ++i) {
        if (src[i] == '\\' && src[i + 1] == 'n') { dst[j++] = '\n'; ++i; }
        else if (src[i] == '\\' && src[i + 1] == 't') { dst[j++] = ' '; ++i; }
        else dst[j++] = src[i];
    }
    dst[j] = '\0';
}

static RandoLogicSetting* RecordSetting(const char* define, const char* label, RandoSettingType type,
                                        const char* tab, const char* group, const char* tooltip) {
    if (sSettingCount >= RANDO_LOGIC_MAX_SETTINGS || define == NULL || define[0] == '\0') return NULL;
    RandoLogicSetting* s = &sSettings[sSettingCount++];
    memset(s, 0, sizeof(*s));
    if (strlen(define) >= sizeof(s->define)) {
        /* Truncation breaks later override lookups, which match by full name. */
        fprintf(stderr, "[rando] warning: setting name truncated: %s\n", define);
    }
    CopyName(s->define, sizeof(s->define), define, strlen(define));
    CopyName(s->label, sizeof(s->label), label ? label : define, label ? strlen(label) : strlen(define));
    if (tab != NULL) CopyName(s->tab, sizeof(s->tab), tab, strlen(tab));
    if (group != NULL) CopyName(s->group, sizeof(s->group), group, strlen(group));
    CopyTooltip(s->tooltip, sizeof(s->tooltip), tooltip);
    s->type = type;
    return s;
}

static uint32_t CountNativeMappedItems(void) {
    uint32_t count = 0;
    for (uint32_t i = 0; i < sLogic.item_count; ++i) {
        sLogic.items[i].native_item = NativeItemFromSymbolName(sLogic.symbols[sLogic.items[i].symbol].name);
        if (sLogic.items[i].native_item != NITEM_NONE) count++;
    }
    return count;
}

static bool HasNativeAwardMappings(void) {
    for (uint32_t i = 0; i < sLogic.item_count; ++i) {
        const LogicItem* item = &sLogic.items[i];
        if (item->type == RANDO_LOGIC_ITEM_MUSIC || item->type == RANDO_LOGIC_ITEM_DUNGEON_ENTRANCE ||
            item->type == RANDO_LOGIC_ITEM_DUNGEON_CONSTRAINT ||
            item->type == RANDO_LOGIC_ITEM_OVERWORLD_CONSTRAINT) continue;
        const char* name = sLogic.symbols[item->symbol].name;
        if (StartsWith(name, "Items.ProgressiveItem.")) return false;
        if (item->native_item == NITEM_NONE) return false;
        if (IsDungeonAward(item->native_item) &&
            NativeSubtypeFromBareName(name + (StartsWith(name, "Items.") ? 6 : 0),
                                      item->native_item, NULL, NULL) == 0) return false;
    }
    for (uint32_t i = 0; i < sLogic.location_count; ++i) {
        const LogicLocation* loc = &sLogic.locations[i];
        if (loc->fixed_item_symbol == UINT16_MAX || loc->is_helper ||
            loc->type == RANDO_LOGIC_LOCATION_MUSIC ||
            loc->type == RANDO_LOGIC_LOCATION_DUNGEON_ENTRANCE ||
            loc->type == RANDO_LOGIC_LOCATION_DUNGEON_CONSTRAINT ||
            loc->type == RANDO_LOGIC_LOCATION_OVERWORLD_CONSTRAINT) continue;
        const char* name = sLogic.symbols[loc->fixed_item_symbol].name;
        if (StartsWith(name, "Items.Entrance.")) continue;
        if (StartsWith(name, "Items.ProgressiveItem.")) return false;
        if (NativeItemFromSymbolName(name) == NITEM_NONE) return false;
    }
    return true;
}

extern "C" void RandoLogic_Reset(void) {
    memset(&sLogic, 0, sizeof(sLogic));
    sHasGeneratedTable = false;
    memset(sGeneratedSubtypes, 0, sizeof(sGeneratedSubtypes));
    for (uint32_t i = 0; i < RANDO_LOGIC_MAX_LOCATIONS; ++i) sEntranceAssign[i] = -1;
    for (uint32_t i = 0; i < RANDO_LOGIC_MUSIC_AREAS; ++i) sMusicAssign[i] = -1;
}

struct NativeSettingOption { const char* label; const char* value; };
struct NativeSettingSpec {
    const char* define;
    const char* label;
    const char* group;
    const char* tooltip;
    RandoSettingType type;
    const char* default_value;
    bool default_flag;
    NativeSettingOption options[3];
    uint8_t option_count;
};

static const NativeSettingSpec kNativeSettings[] = {
    { "ACCESSIBILITY", "Reachability", "Logic", "Required completion target", RANDO_SETTING_DROPDOWN,
      "ACCESS_BEATABLE", false, {{"Beat game", "ACCESS_BEATABLE"}}, 1 },
    { "DOJO", "Dojo rewards", "World", "Shuffle or retain sword lessons", RANDO_SETTING_DROPDOWN,
      "DOJOANY", false, {{"Shuffled", "DOJOANY"}, {"Original", "DOJOVANILLA"}}, 2 },
    { "ITEM_POOL", "Item pool", "Items", "Select the amount of optional equipment and health", RANDO_SETTING_DROPDOWN,
      "ITEM_POOL_NORMAL", false, {{"Balanced", "ITEM_POOL_NORMAL"}, {"Lean", "ITEM_POOL_RIP"},
                                   {"Plentiful", "ITEM_POOL_PLENTIFUL"}}, 3 },
    { "START_SMITH_SWORD", "Start with Smith Sword", "Items", "Grant the Smith Sword on a new file",
      RANDO_SETTING_FLAG, nullptr, true, {}, 0 },
    { "RUPEEMANIA", "Extra ground checks", "Items", "Shuffle additional flagged pickups",
      RANDO_SETTING_FLAG, nullptr, false, {}, 0 },
};

static void LoadNativeSettings(void) {
    for (const NativeSettingSpec& spec : kNativeSettings) {
        int override_index = FindOverride(spec.define);
        RandoLogicSetting* setting = RecordSetting(spec.define, spec.label, spec.type,
                                                   "Main Settings", spec.group, spec.tooltip);
        if (setting == nullptr) continue;
        if (spec.type == RANDO_SETTING_FLAG) {
            bool on = override_index >= 0 ? strcmp(sOverrides[override_index].value, "true") == 0 : spec.default_flag;
            if (on) SetDefineValue(spec.define, nullptr, false);
            setting->flag_on = on;
            setting->default_flag = spec.default_flag;
            continue;
        }
        const char* chosen = override_index >= 0 ? sOverrides[override_index].value : spec.default_value;
        SetDefineValue(spec.define, chosen, true);
        if (chosen[0] != '\0') SetDefineValue(chosen, nullptr, false);
        setting->option_count = spec.option_count;
        for (uint8_t i = 0; i < spec.option_count; ++i) {
            CopyName(setting->opt_label[i], sizeof(setting->opt_label[i]), spec.options[i].label,
                     strlen(spec.options[i].label));
            CopyName(setting->opt_value[i], sizeof(setting->opt_value[i]), spec.options[i].value,
                     strlen(spec.options[i].value));
            if (strcmp(chosen, spec.options[i].value) == 0) setting->option_index = i;
            if (strcmp(spec.default_value, spec.options[i].value) == 0) setting->default_option = i;
        }
    }
}

static bool NativeConditionActive(NativeCondition when) {
    switch (when) {
        case NATIVE_ALWAYS: return true;
        case NATIVE_START_SWORD: return DefineExists("START_SMITH_SWORD");
        case NATIVE_NO_START_SWORD: return !DefineExists("START_SMITH_SWORD");
        case NATIVE_DOJO_SHUFFLED: return DefineExists("DOJOANY");
        case NATIVE_DOJO_ORIGINAL: return !DefineExists("DOJOANY");
        case NATIVE_POOL_LEAN: return DefineExists("ITEM_POOL_RIP");
        case NATIVE_POOL_NORMAL: return !DefineExists("ITEM_POOL_RIP") && !DefineExists("ITEM_POOL_PLENTIFUL");
        case NATIVE_POOL_PLENTIFUL: return !DefineExists("ITEM_POOL_RIP") && DefineExists("ITEM_POOL_PLENTIFUL");
        case NATIVE_RUPEEMANIA: return DefineExists("RUPEEMANIA");
    }
    return false;
}

/* Compiled Picori requirements are simple comma-joined symbol names. Build
 * the same AND graph and symbol order as the former text path. */
static uint16_t AddNativeRequirements(const char* requirements) {
    if (requirements == nullptr || requirements[0] == '\0') return AddNode(EXPR_TRUE);
    uint16_t root = strchr(requirements, ',') == nullptr ? UINT16_MAX : AddNode(EXPR_AND);
    for (const char* start = requirements; *start != '\0'; ) {
        const char* end = strchr(start, ',');
        if (end == nullptr) end = start + strlen(start);
        if (end == start || (size_t)(end - start) > NAME_MAX_LEN) {
            SetError("bad native requirement");
            return UINT16_MAX;
        }
        char name[NAME_MAX_LEN + 1];
        CopyName(name, sizeof(name), start, (size_t)(end - start));
        uint16_t node = AddNode(EXPR_SYMBOL);
        uint16_t symbol = FindOrAddSymbol(name, StartsWith(name, "Items.") ? SYMBOL_ITEM : SYMBOL_UNKNOWN);
        if (node == UINT16_MAX || symbol == UINT16_MAX) return UINT16_MAX;
        sLogic.nodes[node].symbol = symbol;
        if (root == UINT16_MAX) root = node;
        else AddChild(root, node);
        start = *end == ',' ? end + 1 : end;
    }
    return root;
}

static bool AddNativeLocation(const NativeLocationSpec& spec) {
    if (sLogic.location_count >= RANDO_LOGIC_MAX_LOCATIONS) { SetError("too many locations"); return false; }
    LogicLocation* loc = &sLogic.locations[sLogic.location_count];
    memset(loc, 0, sizeof(*loc));
    CopyName(loc->name, sizeof(loc->name), spec.name, strlen(spec.name));
    loc->type = spec.type;
    loc->is_helper = spec.type == RANDO_LOGIC_LOCATION_HELPER;
    loc->fixed_item_symbol = UINT16_MAX;
    loc->item_symbol = UINT16_MAX;
    loc->key = spec.key;
    if (spec.tag != nullptr) loc->tags[loc->tag_count++] = InternTag(spec.tag, strlen(spec.tag));
    char symbol_name[NAME_MAX_LEN + 10];
    snprintf(symbol_name, sizeof(symbol_name), "%s.%s", loc->is_helper ? "Helpers" : "Locations", loc->name);
    loc->symbol = FindOrAddSymbol(symbol_name, loc->is_helper ? SYMBOL_HELPER : SYMBOL_LOCATION);
    if (loc->symbol == UINT16_MAX) return false;
    sLogic.symbols[loc->symbol].index = (uint16_t)sLogic.location_count;
    if (loc->is_helper) {
        sLogic.helper_count++;
    } else {
        snprintf(symbol_name, sizeof(symbol_name), "Helpers.%s", loc->name);
        uint16_t alt = FindOrAddSymbol(symbol_name, SYMBOL_HELPER);
        if (alt == UINT16_MAX) return false;
        sLogic.symbols[alt].index = (uint16_t)sLogic.location_count;
    }
    loc->expr = AddNativeRequirements(spec.requirements);
    if (loc->expr == UINT16_MAX) return false;
    if (spec.fixed_item != nullptr) {
        loc->fixed_item_symbol = FindOrAddSymbol(spec.fixed_item, SYMBOL_ITEM);
        if (loc->fixed_item_symbol == UINT16_MAX) return false;
    }
    sLogic.location_count++;
    return true;
}

extern "C" bool RandoLogic_LoadBuiltIn(void) {
    RandoLogic_Reset();
    sSettingCount = 0;
    LoadNativeSettings();
    /* This virtual starting check occurs before the item pool in the original
     * rules; preserving that order keeps saved symbol and location indices. */
    if (NativeConditionActive(kNativeLocations[0].when) && !AddNativeLocation(kNativeLocations[0])) return false;
    for (const NativeItemSpec& spec : kNativeItems) {
        if (NativeConditionActive(spec.when) && !AddLogicItem(spec.name, spec.type, spec.count, UINT16_MAX)) return false;
    }
    for (size_t i = 1; i < ARRAY_COUNT(kNativeLocations); ++i) {
        if (NativeConditionActive(kNativeLocations[i].when) && !AddNativeLocation(kNativeLocations[i])) return false;
    }
    sLogic.native_mapped_items = CountNativeMappedItems();
    sLogic.loaded = sLogic.error[0] == '\0';
    sLogic.native_assignable = sLogic.loaded && sLogic.item_count > 0 && HasNativeAwardMappings();
    fprintf(stderr, "[RANDO] loaded built-in Picori rules (%u items, %u locations, %u helpers, native=%u)\n",
            sLogic.item_count, sLogic.location_count, sLogic.helper_count, sLogic.native_assignable ? 1u : 0u);
    return sLogic.loaded;
}

extern "C" int RandoLogic_FindLocationByKey(uint32_t key) {
    if (!sLogic.loaded || key == UINT32_MAX) return -1;
    for (uint32_t i = 0; i < sLogic.location_count; ++i) {
        if (!sLogic.locations[i].is_helper && sLogic.locations[i].key == key) return (int)i;
    }
    return -1;
}

extern "C" uint32_t RandoLogic_GetLocationKeyAt(uint32_t index) {
    if (index >= sLogic.location_count) return UINT32_MAX;
    return sLogic.locations[index].is_helper ? UINT32_MAX : sLogic.locations[index].key;
}

extern "C" RandoLogicLocationType RandoLogic_GetLocationType(uint32_t index) {
    if (index >= sLogic.location_count) return RANDO_LOGIC_LOCATION_UNKNOWN;
    return sLogic.locations[index].type;
}

extern "C" uint32_t RandoLogic_GetLocationCountRaw(void) {
    return sLogic.location_count;
}

extern "C" const char* RandoLogic_GetLocationName(uint32_t index) {
    return index < sLogic.location_count ? sLogic.locations[index].name : "";
}

extern "C" uint32_t RandoLogic_GetSettingCount(void) {
    return sSettingCount;
}

extern "C" const RandoLogicSetting* RandoLogic_GetSetting(uint32_t index) {
    return index < sSettingCount ? &sSettings[index] : NULL;
}

extern "C" void RandoLogic_ClearOverrides(void) {
    sOverrideCount = 0;
}

extern "C" void RandoLogic_SetOverride(const char* define, const char* value) {
    if (define == NULL || define[0] == '\0') return;
    int idx = FindOverride(define);
    if (idx < 0) {
        if (sOverrideCount >= RANDO_LOGIC_MAX_SETTINGS) return;
        idx = (int)sOverrideCount++;
        CopyName(sOverrides[idx].name, sizeof(sOverrides[idx].name), define, strlen(define));
    }
    CopyName(sOverrides[idx].value, sizeof(sOverrides[idx].value), value ? value : "", value ? strlen(value) : 0);
    sOverrides[idx].has_value = true;
}

extern "C" uint32_t RandoLogic_GetOverrideCount(void) {
    return sOverrideCount;
}

extern "C" bool RandoLogic_GetOverride(uint32_t index, const char** out_name, const char** out_value) {
    if (index >= sOverrideCount) return false;
    if (out_name != NULL) *out_name = sOverrides[index].name;
    if (out_value != NULL) *out_value = sOverrides[index].value;
    return true;
}

extern "C" void RandoLogic_ClearEntranceAssignments(void) {
    for (uint32_t l = 0; l < RANDO_LOGIC_MAX_LOCATIONS; ++l) sEntranceAssign[l] = -1;
}

extern "C" bool RandoLogic_RestoreEntranceAssignment(uint32_t location_index, int subtype) {
    if (!sLogic.loaded || location_index >= sLogic.location_count) return false;
    if (sLogic.locations[location_index].type != RANDO_LOGIC_LOCATION_DUNGEON_ENTRANCE) return false;
    sEntranceAssign[location_index] = (int16_t)subtype;
    return true;
}

extern "C" bool RandoLogic_Rebuild(void) {
    return RandoLogic_LoadBuiltIn();
}

extern "C" bool RandoLogic_IsLoaded(void) {
    return sLogic.loaded;
}

extern "C" RandoLogicStats RandoLogic_GetStats(void) {
    RandoLogicStats stats;
    memset(&stats, 0, sizeof(stats));
    stats.item_count = sLogic.item_count;
    stats.location_count = sLogic.location_count;
    stats.helper_count = sLogic.helper_count;
    stats.symbol_count = sLogic.symbol_count;
    stats.node_count = sLogic.node_count;
    stats.define_count = sLogic.define_count;
    stats.native_mapped_items = sLogic.native_mapped_items;
    stats.tag_count = sLogic.tag_count;
    stats.loaded = sLogic.loaded;
    stats.native_assignable = sLogic.native_assignable;
    CopyName(stats.error, sizeof(stats.error), sLogic.error, strlen(sLogic.error));
    return stats;
}

extern "C" uint64_t RandoLogic_SourceFingerprint(void) {
    if (!sLogic.loaded) return 0;
    /* v8 saves bind indexed placements to this rules version. The base is
     * the historical Picori v1 source hash, including its separator byte.
     * Change it whenever row order or semantics change. */
    uint64_t hash = 0x37ea4d0a0957c8e4ull;
    for (uint32_t i = 0; i < sOverrideCount; ++i) {
        const LogicOverride* override = &sOverrides[i];
        for (const unsigned char* p = (const unsigned char*)override->name;; ++p) {
            hash = (hash ^ *p) * 1099511628211ull;
            if (*p == 0) break;
        }
        for (const unsigned char* p = (const unsigned char*)override->value;; ++p) {
            hash = (hash ^ *p) * 1099511628211ull;
            if (*p == 0) break;
        }
    }
    return hash ? hash : 1;
}

/* Item-type -> acceptable location-type matrix, matching the documented
 * placement fallbacks (Major -> Major/Dungeon/Any; Minor -> Minor/Any; leftover
 * dungeon/major locations behave as Any; Filler fills whatever is left). */
static bool AllowedAt(RandoLogicItemType it, RandoLogicLocationType lt) {
    switch (it) {
        case RANDO_LOGIC_ITEM_DUNGEON_ENTRANCE:     return lt == RANDO_LOGIC_LOCATION_DUNGEON_ENTRANCE;
        case RANDO_LOGIC_ITEM_DUNGEON_CONSTRAINT:   return lt == RANDO_LOGIC_LOCATION_DUNGEON_CONSTRAINT;
        case RANDO_LOGIC_ITEM_OVERWORLD_CONSTRAINT: return lt == RANDO_LOGIC_LOCATION_OVERWORLD_CONSTRAINT;
        /* Spec: prize items go ONLY to DungeonPrize locations — reaching
         * Dungeon-pool slots happens exclusively via `!prizeplacement`. */
        case RANDO_LOGIC_ITEM_DUNGEON_PRIZE:        return lt == RANDO_LOGIC_LOCATION_DUNGEON_PRIZE;
        /* Prize locations left over after the prize phase join the Dungeon
         * pool (spec); prizes place first in kPlaceOrder, so they get first
         * pick before dungeon/major items can land on prize slots. */
        case RANDO_LOGIC_ITEM_DUNGEON_MAJOR:        return lt == RANDO_LOGIC_LOCATION_DUNGEON ||
                                                           lt == RANDO_LOGIC_LOCATION_DUNGEON_PRIZE;
        case RANDO_LOGIC_ITEM_DUNGEON_MINOR:        return lt == RANDO_LOGIC_LOCATION_DUNGEON ||
                                                           lt == RANDO_LOGIC_LOCATION_DUNGEON_PRIZE;
        case RANDO_LOGIC_ITEM_MAJOR:                return lt == RANDO_LOGIC_LOCATION_MAJOR ||
                                                           lt == RANDO_LOGIC_LOCATION_DUNGEON ||
                                                           lt == RANDO_LOGIC_LOCATION_DUNGEON_PRIZE ||
                                                           lt == RANDO_LOGIC_LOCATION_ANY;
        /* Once major and dungeon pools have been placed, every unfilled
         * ordinary slot joins the Any pool for minor items. This matters for
         * Plentiful + Keysanity, where the extra keys are typed Minor. */
        case RANDO_LOGIC_ITEM_MINOR:                return lt == RANDO_LOGIC_LOCATION_MINOR ||
                                                           lt == RANDO_LOGIC_LOCATION_ANY ||
                                                           lt == RANDO_LOGIC_LOCATION_MAJOR ||
                                                           lt == RANDO_LOGIC_LOCATION_DUNGEON ||
                                                           lt == RANDO_LOGIC_LOCATION_DUNGEON_PRIZE;
        case RANDO_LOGIC_ITEM_FILLER:               return lt == RANDO_LOGIC_LOCATION_MAJOR ||
                                                           lt == RANDO_LOGIC_LOCATION_MINOR ||
                                                           lt == RANDO_LOGIC_LOCATION_DUNGEON ||
                                                           lt == RANDO_LOGIC_LOCATION_DUNGEON_PRIZE ||
                                                           lt == RANDO_LOGIC_LOCATION_ANY;
        case RANDO_LOGIC_ITEM_MUSIC:                return lt == RANDO_LOGIC_LOCATION_MUSIC;
        default:                                    return false;
    }
}

/* Placement priority of item types, matching the documented order:
 * entrances, constraints, prizes, dungeon items, then world major/minor,
 * then music. Filler is handled separately at the end. */
static const RandoLogicItemType kPlaceOrder[] = {
    RANDO_LOGIC_ITEM_DUNGEON_ENTRANCE,
    RANDO_LOGIC_ITEM_DUNGEON_CONSTRAINT,
    RANDO_LOGIC_ITEM_OVERWORLD_CONSTRAINT,
    RANDO_LOGIC_ITEM_DUNGEON_PRIZE,
    RANDO_LOGIC_ITEM_DUNGEON_MAJOR,
    RANDO_LOGIC_ITEM_DUNGEON_MINOR,
    RANDO_LOGIC_ITEM_MAJOR,
    RANDO_LOGIC_ITEM_MINOR,
    RANDO_LOGIC_ITEM_MUSIC,
};

/* A `~Items.X` node anywhere in a location's logic forbids item X from being
 * placed there (a placement guard, NOT a reachability term). */
static bool NodeForbidsSymbol(uint16_t node_idx, uint16_t symbol) {
    if (node_idx == UINT16_MAX) return false;
    const ExprNode* n = &sLogic.nodes[node_idx];
    if (n->type == EXPR_NOT) {
        uint16_t c = n->first_child;
        if (c != UINT16_MAX && sLogic.nodes[c].type == EXPR_SYMBOL &&
            sLogic.nodes[c].symbol == symbol) {
            return true;
        }
    }
    for (uint16_t c = n->first_child; c != UINT16_MAX; c = sLogic.nodes[c].next_sibling) {
        if (NodeForbidsSymbol(c, symbol)) return true;
    }
    return false;
}

typedef struct EvalState {
    uint16_t item_owned[RANDO_LOGIC_MAX_SYMBOLS];
    bool location_reached[RANDO_LOGIC_MAX_LOCATIONS];
    bool evaluating_location[RANDO_LOGIC_MAX_LOCATIONS];
} EvalState;

static bool EvalLocation(uint16_t loc_idx, EvalState* state);

static bool EvalNode(uint16_t node_idx, EvalState* state) {
    if (node_idx == UINT16_MAX) return true;
    const ExprNode* n = &sLogic.nodes[node_idx];
    switch (n->type) {
        case EXPR_TRUE:
            return true;
        case EXPR_SYMBOL: {
            if (n->symbol == UINT16_MAX || n->symbol >= sLogic.symbol_count) return false;
            const LogicSymbol* sym = &sLogic.symbols[n->symbol];
            if (sym->kind == SYMBOL_ITEM) return state->item_owned[n->symbol] > 0;
            if ((sym->kind == SYMBOL_LOCATION || sym->kind == SYMBOL_HELPER) && sym->index != UINT16_MAX) {
                return EvalLocation(sym->index, state);
            }
            return false;
        }
        case EXPR_NOT:
            /* `~Items.X` is a placement guard consulted during fill
             * (NodeForbidsSymbol), not a reachability requirement. */
            return true;
        case EXPR_AND: {
            for (uint16_t c = n->first_child; c != UINT16_MAX; c = sLogic.nodes[c].next_sibling) {
                if (!EvalNode(c, state)) return false;
            }
            return true;
        }
        case EXPR_OR: {
            for (uint16_t c = n->first_child; c != UINT16_MAX; c = sLogic.nodes[c].next_sibling) {
                if (EvalNode(c, state)) return true;
            }
            return false;
        }
        case EXPR_COUNT: {
            uint32_t count = 0;
            for (uint16_t c = n->first_child; c != UINT16_MAX; c = sLogic.nodes[c].next_sibling) {
                const ExprNode* child = &sLogic.nodes[c];
                if (child->type == EXPR_SYMBOL && child->symbol < sLogic.symbol_count &&
                    sLogic.symbols[child->symbol].kind == SYMBOL_ITEM) {
                    count += (uint32_t)state->item_owned[child->symbol] * child->weight;
                } else if (EvalNode(c, state)) {
                    count += child->weight;
                }
                if (count >= n->threshold) return true;
            }
            return false;
        }
        default:
            return false;
    }
}

static bool EvalLocation(uint16_t loc_idx, EvalState* state) {
    if (loc_idx >= sLogic.location_count) return false;
    if (state->location_reached[loc_idx]) return true;
    if (state->evaluating_location[loc_idx]) return false;
    state->evaluating_location[loc_idx] = true;
    bool ok = EvalNode(sLogic.locations[loc_idx].expr, state);
    state->evaluating_location[loc_idx] = false;
    return ok;
}

/* Walk an expression and return the symbol index of a leaf that is currently
 * unsatisfied and blocks the expression (for diagnostics). -1 if satisfied. */
static int FirstUnsatSymbol(uint16_t node, EvalState* st) {
    if (node == UINT16_MAX) return -1;
    const ExprNode* n = &sLogic.nodes[node];
    switch (n->type) {
        case EXPR_TRUE:
        case EXPR_NOT:
            return -1;
        case EXPR_SYMBOL:
            return EvalNode(node, st) ? -1 : (int)n->symbol;
        case EXPR_AND:
            for (uint16_t c = n->first_child; c != UINT16_MAX; c = sLogic.nodes[c].next_sibling) {
                if (!EvalNode(c, st)) { int r = FirstUnsatSymbol(c, st); if (r >= 0) return r; }
            }
            return -1;
        case EXPR_OR:
        case EXPR_COUNT:
            if (EvalNode(node, st)) return -1;
            return (n->first_child != UINT16_MAX) ? FirstUnsatSymbol(n->first_child, st) : -1;
        default:
            return -1;
    }
}

typedef struct SplitMix64Local {
    uint64_t state;
} SplitMix64Local;

static uint64_t NextRandom(SplitMix64Local* rng) {
    uint64_t z = (rng->state += 0x9e3779b97f4a7c15ull);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}

static uint32_t BoundedRandom(SplitMix64Local* rng, uint32_t bound) {
    return bound <= 1 ? 0 : (uint32_t)(NextRandom(rng) % bound);
}

static void ShuffleU16(uint16_t* values, uint32_t count, SplitMix64Local* rng) {
    for (uint32_t i = count; i > 1; --i) {
        uint32_t j = BoundedRandom(rng, i);
        uint16_t tmp = values[i - 1];
        values[i - 1] = values[j];
        values[j] = tmp;
    }
}

static void CollectReachable(EvalState* state, const uint16_t* assignment, bool grant_fixed) {
    bool progressed;
    do {
        progressed = false;
        for (uint32_t i = 0; i < sLogic.location_count; ++i) {
            LogicLocation* loc = &sLogic.locations[i];
            if (loc->is_helper || state->location_reached[i]) continue;
            if (!EvalLocation((uint16_t)i, state)) continue;
            state->location_reached[i] = true;
            progressed = true;
            uint16_t sym = grant_fixed ? loc->fixed_item_symbol : UINT16_MAX;
            if (assignment != NULL && assignment[i] != UINT16_MAX) sym = assignment[i];
            if (sym != UINT16_MAX && state->item_owned[sym] < UINT16_MAX) state->item_owned[sym]++;
        }
    } while (progressed);
}

/* Saved native tables do not carry logic-symbol ids. Resolve only an exact
 * (item, subtype) match; ambiguity stays unowned rather than inventing an
 * item that could make an impossible check appear reachable. */
static uint16_t SymbolForNativeAward(uint32_t location_index, uint16_t item, uint8_t subtype) {
    const LogicLocation* loc = &sLogic.locations[location_index];
    if (loc->fixed_item_symbol != UINT16_MAX) {
        uint16_t symbol = loc->fixed_item_symbol;
        const char* name = sLogic.symbols[symbol].name;
        if (item == NITEM_NONE && StartsWith(name, "Items.Entrance.")) return symbol;
        if (NativeItemFromSymbolName(name) == item &&
            NativeSubtypeFromBareName(name + (StartsWith(name, "Items.") ? 6 : 0),
                                      item, NULL, NULL) == subtype) return symbol;
        return UINT16_MAX;
    }
    if (item == NITEM_NONE) return UINT16_MAX;
    uint16_t found = UINT16_MAX;
    for (uint32_t i = 0; i < sLogic.item_count; ++i) {
        const LogicItem* candidate = &sLogic.items[i];
        if (candidate->native_item != item) continue;
        const char* name = sLogic.symbols[candidate->symbol].name;
        if (NativeSubtypeFromBareName(name + (StartsWith(name, "Items.") ? 6 : 0),
                                      item, NULL, NULL) != subtype) continue;
        if (found != UINT16_MAX && found != candidate->symbol) return UINT16_MAX;
        found = candidate->symbol;
    }
    return found;
}

extern "C" void RandoLogic_EvaluateReachability(
    const uint16_t* active_table,
    const uint8_t* active_subtypes,
    size_t active_count,
    uint64_t active_seed,
    uint16_t (*get_item_count_fn)(const char* name),
    bool* out_reached,
    uint32_t location_count) {

    if (out_reached == NULL) return;
    memset(out_reached, 0, sizeof(*out_reached) * location_count);
    if (!sLogic.loaded || active_table == NULL || active_subtypes == NULL ||
        active_count != sLogic.location_count) return;

    EvalState state;
    memset(&state, 0, sizeof(state));
    uint16_t assignment[RANDO_LOGIC_MAX_LOCATIONS];
    bool exact = RandoLogic_GeneratedTableMatches(active_seed, active_table, active_subtypes, active_count);
    for (uint32_t l = 0; l < sLogic.location_count; ++l) {
        assignment[l] = exact ? sGeneratedSymbols[l] :
            SymbolForNativeAward(l, active_table[l], active_subtypes[l]);
    }

    /* 1. Seed item ownership from GBA inventory/flags */
    for (uint32_t s = 0; s < sLogic.symbol_count; ++s) {
        if (get_item_count_fn != NULL && sLogic.symbols[s].kind == SYMBOL_ITEM) {
            state.item_owned[s] = get_item_count_fn(sLogic.symbols[s].name);
        }
    }

    /* 2. Propagate reachability across the expressions graph */
    CollectReachable(&state, assignment, false);

    /* 3. Output reached flags */
    for (uint32_t l = 0; l < sLogic.location_count && l < location_count; ++l) {
        out_reached[l] = state.location_reached[l];
    }
}

/* Items referenced anywhere in logic expressions are "advancement": their
 * placement must keep the seed beatable, so they go through assumed fill. */
static void ComputeAdvancement(bool* sym_in_logic) {
    memset(sym_in_logic, 0, sizeof(bool) * RANDO_LOGIC_MAX_SYMBOLS);
    for (uint32_t i = 0; i < sLogic.node_count; ++i) {
        if (sLogic.nodes[i].type == EXPR_SYMBOL) {
            uint16_t s = sLogic.nodes[i].symbol;
            if (s < RANDO_LOGIC_MAX_SYMBOLS && sLogic.symbols[s].kind == SYMBOL_ITEM) {
                sym_in_logic[s] = true;
            }
        }
    }
}

static int FindGoalLocation(void) {
    for (uint32_t i = 0; i < sLogic.location_count; ++i) {
        const char* n = sLogic.locations[i].name;
        if (strstr(n, "Vaati") || strcmp(n, "Goal") == 0 || strcmp(n, "Beat") == 0 ||
            strcmp(n, "DefeatVaati") == 0 || strcmp(n, "BeatGame") == 0) {
            return (int)i;
        }
    }
    return -1;
}

/* Assumed-fill placement:
 * advancement items are placed so the seed stays beatable (every item is
 * reachable assuming you already hold all not-yet-placed advancement items),
 * typed pools are honoured in priority order with the documented fallbacks,
 * `~Items.X` guards block placement, and the result is verified per the
 * selected accessibility mode. Filler fills whatever is left. */
static RandoStatus GenerateOnce(uint64_t seed, uint32_t attempt,
                                const RandomizerSettings* settings,
                                uint16_t* out_table, size_t out_table_count,
                                uint64_t* out_seed) {
    (void)settings;
    sHasGeneratedTable = false;
    if (!sLogic.loaded) return RANDO_INACTIVE;
    if (out_table == NULL || out_table_count < sLogic.location_count) return RANDO_BAD_SETTINGS;
    if (!sLogic.native_assignable) return RANDO_BAD_SETTINGS;

    static bool sym_in_logic[RANDO_LOGIC_MAX_SYMBOLS];
    static uint16_t assumed_count[RANDO_LOGIC_MAX_SYMBOLS];
    static uint16_t assignment[RANDO_LOGIC_MAX_LOCATIONS];
    static bool placed[RANDO_LOGIC_MAX_ITEMS];
    static uint16_t order[RANDO_LOGIC_MAX_ITEMS];
    static uint16_t candidates[RANDO_LOGIC_MAX_LOCATIONS];
    static EvalState state;

    SplitMix64Local rng;
    const bool no_logic = DefineExists("NO_LOGIC");

    static int dbg_calls = 0;
    const bool dbg = getenv("TMC_RANDO_DEBUG") != NULL && sLogic.location_count > 100 && (dbg_calls++ == 0);
    /* Keep attempt zero stable for existing seeds. Later attempts take a
     * deterministic alternate path through the same placement algorithm. */
    rng.state = attempt == 0 ? (seed ? seed : 1u) :
                (seed ^ (0x9e3779b97f4a7c15ull * attempt));
    ComputeAdvancement(sym_in_logic);
    memset(assumed_count, 0, sizeof(assumed_count));
    memset(placed, 0, sizeof(placed));
    for (uint32_t i = 0; i < sLogic.location_count; ++i) assignment[i] = UINT16_MAX;
    for (uint32_t i = 0; i < sLogic.item_count; ++i) {
        uint16_t s = sLogic.items[i].symbol;
        if (sLogic.items[i].type != RANDO_LOGIC_ITEM_FILLER && sym_in_logic[s]) {
            assumed_count[s]++;
        }
    }
    if (dbg) {
        uint32_t loc_hist[16] = {0}, it_hist[16] = {0};
        for (uint32_t i = 0; i < sLogic.location_count; ++i) loc_hist[sLogic.locations[i].type & 15]++;
        for (uint32_t i = 0; i < sLogic.item_count; ++i) it_hist[sLogic.items[i].type & 15]++;
        fprintf(stderr, "[gen] locs: major=%u dungeon=%u any=%u minor=%u prize=%u ent=%u dcon=%u ocon=%u unsh=%u helper=%u music=%u\n",
                loc_hist[RANDO_LOGIC_LOCATION_MAJOR], loc_hist[RANDO_LOGIC_LOCATION_DUNGEON], loc_hist[RANDO_LOGIC_LOCATION_ANY],
                loc_hist[RANDO_LOGIC_LOCATION_MINOR], loc_hist[RANDO_LOGIC_LOCATION_DUNGEON_PRIZE], loc_hist[RANDO_LOGIC_LOCATION_DUNGEON_ENTRANCE],
                loc_hist[RANDO_LOGIC_LOCATION_DUNGEON_CONSTRAINT], loc_hist[RANDO_LOGIC_LOCATION_OVERWORLD_CONSTRAINT],
                loc_hist[RANDO_LOGIC_LOCATION_UNSHUFFLED], loc_hist[RANDO_LOGIC_LOCATION_HELPER], loc_hist[RANDO_LOGIC_LOCATION_MUSIC]);
        fprintf(stderr, "[gen] items: major=%u dmajor=%u dminor=%u minor=%u prize=%u ent=%u dcon=%u ocon=%u filler=%u music=%u\n",
                it_hist[RANDO_LOGIC_ITEM_MAJOR], it_hist[RANDO_LOGIC_ITEM_DUNGEON_MAJOR], it_hist[RANDO_LOGIC_ITEM_DUNGEON_MINOR],
                it_hist[RANDO_LOGIC_ITEM_MINOR], it_hist[RANDO_LOGIC_ITEM_DUNGEON_PRIZE], it_hist[RANDO_LOGIC_ITEM_DUNGEON_ENTRANCE],
                it_hist[RANDO_LOGIC_ITEM_DUNGEON_CONSTRAINT], it_hist[RANDO_LOGIC_ITEM_OVERWORLD_CONSTRAINT],
                it_hist[RANDO_LOGIC_ITEM_FILLER], it_hist[RANDO_LOGIC_ITEM_MUSIC]);
        /* Max reachability with ALL items owned, and the symbols that block the
         * most unreachable locations (pinpoints unmodelled events/imports). */
        memset(&state, 0, sizeof(state));
        for (uint32_t s = 0; s < sLogic.symbol_count; ++s)
            if (sLogic.symbols[s].kind == SYMBOL_ITEM) state.item_owned[s] = true;
        CollectReachable(&state, NULL, true);
        static uint32_t blk[RANDO_LOGIC_MAX_SYMBOLS];
        memset(blk, 0, sizeof(blk));
        uint32_t real = 0, reach = 0;
        for (uint32_t l = 0; l < sLogic.location_count; ++l) {
            if (sLogic.locations[l].is_helper) continue;
            real++;
            if (state.location_reached[l]) { reach++; continue; }
            int s = FirstUnsatSymbol(sLogic.locations[l].expr, &state);
            if (s >= 0) blk[s]++;
        }
        fprintf(stderr, "[gen] MAX-reach with all items: %u/%u real locations\n", reach, real);
        for (int top = 0; top < 14; ++top) {
            uint32_t best = 0; int bi = -1;
            for (uint32_t s = 0; s < sLogic.symbol_count; ++s) if (blk[s] > best) { best = blk[s]; bi = (int)s; }
            if (bi < 0 || best == 0) break;
            fprintf(stderr, "[gen]   blocker x%u: %s (kind=%d)\n", best, sLogic.symbols[bi].name, sLogic.symbols[bi].kind);
            blk[bi] = 0;
        }
    }

    /* A redirected prize consumes its prize location even though the slot
     * stays open for later dungeon-pool items. */
    static bool prize_consumed[RANDO_LOGIC_MAX_LOCATIONS];
    memset(prize_consumed, 0, sizeof(prize_consumed));

    /* Place every non-filler item, in documented type-priority order. */
    for (size_t t = 0; t < ARRAY_COUNT(kPlaceOrder); ++t) {
        RandoLogicItemType type = kPlaceOrder[t];
        uint32_t order_count = 0;
        for (uint32_t i = 0; i < sLogic.item_count; ++i) {
            if (!placed[i] && sLogic.items[i].type == type) order[order_count++] = (uint16_t)i;
        }
        ShuffleU16(order, order_count, &rng);
        if (dbg) {
            uint32_t e[16] = {0};
            for (uint32_t l = 0; l < sLogic.location_count; ++l) {
                LogicLocation* lo = &sLogic.locations[l];
                if (!lo->is_helper && lo->fixed_item_symbol == UINT16_MAX && assignment[l] == UINT16_MAX) e[lo->type & 15]++;
            }
            fprintf(stderr, "[gen] type=%d items=%u | empty prize=%u dungeon=%u any=%u major=%u minor=%u\n",
                    (int)type, order_count, e[RANDO_LOGIC_LOCATION_DUNGEON_PRIZE], e[RANDO_LOGIC_LOCATION_DUNGEON],
                    e[RANDO_LOGIC_LOCATION_ANY], e[RANDO_LOGIC_LOCATION_MAJOR], e[RANDO_LOGIC_LOCATION_MINOR]);
        }

        for (uint32_t k = 0; k < order_count; ++k) {
            uint16_t item_idx = order[k];
            uint16_t sym = sLogic.items[item_idx].symbol;
            uint32_t cand_count = 0;

            if (assumed_count[sym] > 0) assumed_count[sym]--;

            if (!no_logic) {
                memset(&state, 0, sizeof(state));
                for (uint32_t s = 0; s < sLogic.symbol_count; ++s) {
                    state.item_owned[s] = assumed_count[s];
                }
                CollectReachable(&state, assignment, true);
            }

            for (uint32_t l = 0; l < sLogic.location_count; ++l) {
                LogicLocation* loc = &sLogic.locations[l];
                if (loc->is_helper || loc->fixed_item_symbol != UINT16_MAX || assignment[l] != UINT16_MAX) continue;
                if (!AllowedAt(type, loc->type)) continue;
                if (type == RANDO_LOGIC_ITEM_DUNGEON_PRIZE &&
                    loc->type == RANDO_LOGIC_LOCATION_DUNGEON_PRIZE && prize_consumed[l]) continue;
                /* Dungeon-id binding: a tagged item only goes on locations
                 * carrying its tag. Vanilla pins, own-dungeon, own-region —
                 * the whole keysanity matrix is tag data in the file. */
                uint16_t ptag = sLogic.items[item_idx].pool_tag;
                if (ptag != UINT16_MAX && !LocationHasTag(loc, ptag)) continue;
                if (NodeForbidsSymbol(loc->expr, sym)) continue;
                /* Constraint/entrance items are structural dummies that grant
                 * no real item, so they need not sit in a reachable slot. */
                bool needs_reach = (type != RANDO_LOGIC_ITEM_DUNGEON_CONSTRAINT &&
                                    type != RANDO_LOGIC_ITEM_OVERWORLD_CONSTRAINT &&
                                    type != RANDO_LOGIC_ITEM_DUNGEON_ENTRANCE);
                if (!no_logic && needs_reach && !state.location_reached[l]) continue;
                candidates[cand_count++] = (uint16_t)l;
            }
            if (cand_count == 0 && type == RANDO_LOGIC_ITEM_MUSIC) {
                /* The music pool can be larger than the area-slot count;
                 * leftover songs are cosmetic surplus, never a failure. */
                placed[item_idx] = true;
                continue;
            }
            if (cand_count == 0) {
                if (dbg) {
                    uint32_t empty_by_type[16] = {0};
                    uint32_t total_reach = 0, empty_reach = 0;
                    for (uint32_t l = 0; l < sLogic.location_count; ++l) {
                        if (state.location_reached[l]) total_reach++;
                        LogicLocation* lo = &sLogic.locations[l];
                        if (lo->is_helper || lo->fixed_item_symbol != UINT16_MAX || assignment[l] != UINT16_MAX) continue;
                        empty_by_type[lo->type & 15]++;
                        if (state.location_reached[l]) empty_reach++;
                    }
                    fprintf(stderr, "[gen] FAIL place '%s' type=%d: empty prize=%u dungeon=%u any=%u major=%u minor=%u; total_reached=%u empty_reached=%u\n",
                            sLogic.symbols[sym].name, (int)type,
                            empty_by_type[RANDO_LOGIC_LOCATION_DUNGEON_PRIZE], empty_by_type[RANDO_LOGIC_LOCATION_DUNGEON],
                            empty_by_type[RANDO_LOGIC_LOCATION_ANY], empty_by_type[RANDO_LOGIC_LOCATION_MAJOR],
                            empty_by_type[RANDO_LOGIC_LOCATION_MINOR], total_reach, empty_reach);
                }
                return RANDO_UNBEATABLE;
            }
            uint16_t chosen = candidates[BoundedRandom(&rng, cand_count)];
            /* A prize assigned to a redirected prize
             * location is instead placed within the redirect tag pool (or
             * anywhere when the rule has no dungeon id); the prize slot
             * itself stays open and joins the Dungeon pool. */
            if (type == RANDO_LOGIC_ITEM_DUNGEON_PRIZE &&
                sLogic.locations[chosen].has_prize_redirect && !prize_consumed[chosen]) {
                const uint16_t rtag = sLogic.locations[chosen].prize_redirect_tag;
                prize_consumed[chosen] = true;
                cand_count = 0;
                for (uint32_t l = 0; l < sLogic.location_count; ++l) {
                    LogicLocation* loc = &sLogic.locations[l];
                    if (loc->is_helper || loc->fixed_item_symbol != UINT16_MAX || assignment[l] != UINT16_MAX) continue;
                    if (loc->type != RANDO_LOGIC_LOCATION_DUNGEON &&
                        loc->type != RANDO_LOGIC_LOCATION_ANY &&
                        loc->type != RANDO_LOGIC_LOCATION_MAJOR &&
                        loc->type != RANDO_LOGIC_LOCATION_MINOR &&
                        loc->type != RANDO_LOGIC_LOCATION_DUNGEON_PRIZE) continue;
                    if (loc->type == RANDO_LOGIC_LOCATION_DUNGEON_PRIZE && prize_consumed[l]) continue;
                    if (rtag != UINT16_MAX && !LocationHasTag(loc, rtag)) continue;
                    if (NodeForbidsSymbol(loc->expr, sym)) continue;
                    if (!no_logic && !state.location_reached[l]) continue;
                    candidates[cand_count++] = (uint16_t)l;
                }
                if (cand_count == 0) {
                    if (dbg) fprintf(stderr, "[gen] FAIL prize redirect for '%s' (tag pool empty)\n",
                                     sLogic.symbols[sym].name);
                    return RANDO_UNBEATABLE;
                }
                chosen = candidates[BoundedRandom(&rng, cand_count)];
            }
            assignment[chosen] = sym;
            placed[item_idx] = true;
        }
    }

    /* Entrance/constraint pools must be fully consumed on both sides. */
    for (uint32_t l = 0; l < sLogic.location_count; ++l) {
        LogicLocation* loc = &sLogic.locations[l];
        if (loc->is_helper || loc->fixed_item_symbol != UINT16_MAX || assignment[l] != UINT16_MAX) continue;
        if (loc->type == RANDO_LOGIC_LOCATION_DUNGEON_ENTRANCE ||
            loc->type == RANDO_LOGIC_LOCATION_DUNGEON_CONSTRAINT ||
            loc->type == RANDO_LOGIC_LOCATION_OVERWORLD_CONSTRAINT) {
            if (dbg) fprintf(stderr, "[gen] FAIL unfilled entrance/constraint loc '%s' type=%d\n",
                             loc->name, (int)loc->type);
            return RANDO_UNBEATABLE;
        }
    }

    /* Filler fills everything that's left (repeatable). */
    uint16_t filler[RANDO_LOGIC_MAX_ITEMS];
    uint32_t filler_count = 0;
    for (uint32_t i = 0; i < sLogic.item_count; ++i) {
        if (sLogic.items[i].type == RANDO_LOGIC_ITEM_FILLER) filler[filler_count++] = sLogic.items[i].symbol;
    }
    for (uint32_t l = 0; l < sLogic.location_count; ++l) {
        LogicLocation* loc = &sLogic.locations[l];
        if (loc->is_helper || loc->fixed_item_symbol != UINT16_MAX || assignment[l] != UINT16_MAX) continue;
        if (loc->type == RANDO_LOGIC_LOCATION_UNSHUFFLED ||
            loc->type == RANDO_LOGIC_LOCATION_UNSHUFFLED_PRIZE ||
            loc->type == RANDO_LOGIC_LOCATION_MUSIC) continue;
        if (filler_count > 0) assignment[l] = filler[BoundedRandom(&rng, filler_count)];
    }

    /* Emit the native item table + subtype table (0 = leave vanilla reward /
     * no subtype override). Families with indistinguishable symbols but
     * multiple native piece ids (gold cloud/swamp kinstones) are distributed
     * deterministically in encounter order, matching the start-inventory
     * round-robin policy. */
    uint8_t gold_cloud_idx = 0;
    uint8_t gold_swamp_idx = 0;
    for (uint32_t l = 0; l < sLogic.location_count; ++l) {
        LogicLocation* loc = &sLogic.locations[l];
        uint16_t sym = (loc->fixed_item_symbol != UINT16_MAX) ? loc->fixed_item_symbol : assignment[l];
        if (loc->is_helper || (sym < sLogic.symbol_count &&
            StartsWith(sLogic.symbols[sym].name, "Items.Entrance.")) ||
            loc->type == RANDO_LOGIC_LOCATION_MUSIC ||
            loc->type == RANDO_LOGIC_LOCATION_DUNGEON_ENTRANCE ||
            loc->type == RANDO_LOGIC_LOCATION_DUNGEON_CONSTRAINT ||
            loc->type == RANDO_LOGIC_LOCATION_OVERWORLD_CONSTRAINT) {
            out_table[l] = (uint16_t)NITEM_NONE;
            sGeneratedSubtypes[l] = 0;
            continue;
        }
        if (sym == UINT16_MAX || sym >= sLogic.symbol_count) {
            fprintf(stderr, "[RANDO] missing award at '%s'\n", loc->name);
            return RANDO_BAD_SETTINGS;
        }
        const char* name = sLogic.symbols[sym].name;
        if (StartsWith(name, "Items.")) name += 6;
        out_table[l] = NativeItemFromBareName(name);
        if (out_table[l] == NITEM_NONE) {
            fprintf(stderr, "[RANDO] unmapped award '%s' at '%s'\n", name, loc->name);
            return RANDO_BAD_SETTINGS;
        }
        sGeneratedSubtypes[l] = NativeSubtypeFromBareName(name, out_table[l], &gold_cloud_idx, &gold_swamp_idx);
    }

    /* Verify per accessibility mode. */
    if (!no_logic) {
        const char* acc = DefineValue("ACCESSIBILITY");
        /* `!ensurereachability` (or ACCESS_LOCATIONS) requires every location
         * reachable; ACCESS_BEATABLE only requires the goal. */
        bool beatable_only = !sLogic.ensure_reachability &&
                             (acc != NULL && strcmp(acc, "ACCESS_BEATABLE") == 0);
        /* Fixed grants become owned only after their locations are reachable. */
        memset(&state, 0, sizeof(state));
        CollectReachable(&state, assignment, true);
        if (dbg) {
            uint32_t reached = 0, real = 0;
            for (uint32_t l = 0; l < sLogic.location_count; ++l) {
                if (sLogic.locations[l].is_helper) continue;
                real++;
                if (state.location_reached[l]) { reached++; continue; }
                int b = FirstUnsatSymbol(sLogic.locations[l].expr, &state);
                fprintf(stderr, "[gen]   unreached '%s' blocker=%s\n", sLogic.locations[l].name,
                        b >= 0 ? sLogic.symbols[b].name : "(none)");
            }
            fprintf(stderr, "[gen] verify reachability: %u/%u real locations reached\n", reached, real);
        }
        if (beatable_only) {
            /* The goal is typically a helper (e.g. Helpers.BeatVaati); helpers
             * are never flagged location_reached, so evaluate it directly. */
            int goal = FindGoalLocation();
            if (goal < 0) {
                if (dbg) fprintf(stderr, "[gen] FAIL verify: no goal location\n");
                return RANDO_UNBEATABLE;
            }
            if (!EvalLocation((uint16_t)goal, &state)) {
                if (dbg) {
                    int b = FirstUnsatSymbol(sLogic.locations[goal].expr, &state);
                    fprintf(stderr, "[gen] FAIL verify: goal '%s' unreachable; blocker=%s\n",
                            sLogic.locations[goal].name, b >= 0 ? sLogic.symbols[b].name : "(none)");
                }
                return RANDO_UNBEATABLE;
            }
        } else {
            uint32_t unreached = 0;
            int first = -1;
            for (uint32_t l = 0; l < sLogic.location_count; ++l) {
                if (!sLogic.locations[l].is_helper && !state.location_reached[l]) {
                    if (first < 0) first = (int)l;
                    unreached++;
                }
            }
            if (unreached > 0) {
                if (dbg) fprintf(stderr, "[gen] FAIL verify: %u/%u locations unreachable (first='%s')\n",
                                 unreached, sLogic.location_count, first >= 0 ? sLogic.locations[first].name : "?");
                return RANDO_UNBEATABLE;
            }
        }
    }

    /* Record entrance-dummy assignments for the runtime entrance swap. */
    for (uint32_t l = 0; l < sLogic.location_count; ++l) {
        sEntranceAssign[l] = -1;
        if (sLogic.locations[l].type != RANDO_LOGIC_LOCATION_DUNGEON_ENTRANCE) continue;
        uint16_t esym = assignment[l];
        if (esym == UINT16_MAX) esym = sLogic.locations[l].fixed_item_symbol;
        if (esym == UINT16_MAX) continue;
        const char* nm = sLogic.symbols[esym].name; /* "Items.Entrance.0xNN" */
        const char* dot = strrchr(nm, '.');
        if (dot != NULL) sEntranceAssign[l] = (int16_t)strtoul(dot + 1, NULL, 0);
    }

    /* Record per-area music assignments ("Area%xMusic" <- "Items.Music.0xNN"). */
    for (uint32_t a = 0; a < RANDO_LOGIC_MUSIC_AREAS; ++a) sMusicAssign[a] = -1;
    for (uint32_t l = 0; l < sLogic.location_count; ++l) {
        if (sLogic.locations[l].type != RANDO_LOGIC_LOCATION_MUSIC) continue;
        const char* nm = sLogic.locations[l].name;
        if (strncmp(nm, "Area", 4) != 0) continue;
        char* end = NULL;
        unsigned long area = strtoul(nm + 4, &end, 16);
        if (end == nm + 4 || strcmp(end, "Music") != 0 || area >= RANDO_LOGIC_MUSIC_AREAS) continue;
        uint16_t msym = assignment[l];
        if (msym == UINT16_MAX) continue;
        const char* dot = strrchr(sLogic.symbols[msym].name, '.');
        if (dot != NULL) sMusicAssign[area] = (int16_t)strtoul(dot + 1, NULL, 0);
    }

    memcpy(sGeneratedItems, out_table, sLogic.location_count * sizeof(sGeneratedItems[0]));
    for (uint32_t l = 0; l < sLogic.location_count; ++l) {
        const LogicLocation* loc = &sLogic.locations[l];
        sGeneratedSymbols[l] = loc->fixed_item_symbol != UINT16_MAX ? loc->fixed_item_symbol : assignment[l];
    }
    sGeneratedSeed = seed;
    sHasGeneratedTable = true;
    if (out_seed) *out_seed = seed;
    return RANDO_OK;
}

extern "C" RandoStatus RandoLogic_Generate(uint64_t seed, const RandomizerSettings* settings,
                                            uint16_t* out_table, size_t out_table_count,
                                            uint64_t* out_seed) {
    /* Retry incidental assumed-fill dead ends. Keep the displayed seed stable
     * so the same seed and logic source always produce the same native table.
     * Invalid settings fail at once. */
    for (uint32_t attempt = 0; attempt < 32; ++attempt) {
        RandoStatus status = GenerateOnce(seed, attempt, settings, out_table, out_table_count, out_seed);
        if (status != RANDO_UNBEATABLE) return status;
    }
    return RANDO_UNBEATABLE;
}

extern "C" bool RandoLogic_GeneratedTableMatches(uint64_t seed, const uint16_t* items,
                                                    const uint8_t* subtypes, size_t count) {
    return sLogic.loaded && sHasGeneratedTable && seed == sGeneratedSeed &&
           items != NULL && subtypes != NULL && count == sLogic.location_count &&
           memcmp(items, sGeneratedItems, count * sizeof(items[0])) == 0 &&
           memcmp(subtypes, sGeneratedSubtypes, count * sizeof(subtypes[0])) == 0;
}
extern "C" uint8_t RandoLogic_GetGeneratedItemSubtype(uint32_t location_index) {
    if (!sLogic.loaded || !sHasGeneratedTable || location_index >= sLogic.location_count) return 0;
    return sGeneratedSubtypes[location_index];
}


extern "C" int RandoLogic_GetEntranceAssignment(uint32_t location_index) {
    if (!sLogic.loaded || location_index >= sLogic.location_count) return -1;
    return sEntranceAssign[location_index];
}

extern "C" int RandoLogic_GetMusicAssignment(uint32_t area) {
    if (!sLogic.loaded || area >= RANDO_LOGIC_MUSIC_AREAS) return -1;
    return sMusicAssign[area];
}

extern "C" void RandoLogic_ClearMusicAssignments(void) {
    for (uint32_t a = 0; a < RANDO_LOGIC_MUSIC_AREAS; ++a) sMusicAssign[a] = -1;
}

extern "C" bool RandoLogic_RestoreMusicAssignment(uint32_t area, int song) {
    if (area >= RANDO_LOGIC_MUSIC_AREAS) return false;
    sMusicAssign[area] = (int16_t)song;
    return true;
}

extern "C" bool RandoLogic_LocationHasTagName(uint32_t location_index, const char* tag_name) {
    if (!sLogic.loaded || location_index >= sLogic.location_count || tag_name == NULL) return false;
    const LogicLocation* loc = &sLogic.locations[location_index];
    for (uint8_t i = 0; i < loc->tag_count; ++i) {
        if (strcmp(sLogic.tag_names[loc->tags[i]], tag_name) == 0) return true;
    }
    return false;
}

/* Bind a native runtime key onto a compiled location with no direct key. The
 * curated name->key table lives port-side; binding only fills empty keys. */
extern "C" bool RandoLogic_BindRuntimeKey(const char* location_name, uint32_t key) {
    if (!sLogic.loaded || location_name == NULL) return false;
    for (uint32_t l = 0; l < sLogic.location_count; ++l) {
        if (strcmp(sLogic.locations[l].name, location_name) != 0) continue;
        if (sLogic.locations[l].key != UINT32_MAX) return false; /* already keyed */
        sLogic.locations[l].key = key;
        return true;
    }
    return false;
}

extern "C" bool RandoLogic_SetRuntimeKeyAt(uint32_t index, uint32_t key) {
    if (!sLogic.loaded || index >= sLogic.location_count || key == UINT32_MAX ||
        sLogic.locations[index].is_helper) return false;
    for (uint32_t i = 0; i < sLogic.location_count; ++i) {
        if (i != index && !sLogic.locations[i].is_helper && sLogic.locations[i].key == key) return false;
    }
    sLogic.locations[index].key = key;
    return true;
}
