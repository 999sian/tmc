/*
 * Clean-room MinishMaker-style .logic parser and verifier.
 *
 * This code implements the public text format described by default.logic:
 * directives, defines, fixed item/location types, and prefix logic
 * expressions. It does not translate GPL C# implementation code. Parsed data
 * lives in fixed static arrays; generation/evaluation uses only stack scratch.
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
#define LINE_MAX_LEN 32767

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

typedef struct CondFrame {
    bool parent_active;
    bool condition_true;
    bool active;
    bool else_seen;
} CondFrame;

typedef struct LogicModel {
    LogicSymbol symbols[RANDO_LOGIC_MAX_SYMBOLS];
    LogicItem items[RANDO_LOGIC_MAX_ITEMS];
    LogicLocation locations[RANDO_LOGIC_MAX_LOCATIONS];
    ExprNode nodes[RANDO_LOGIC_MAX_NODES];
    LogicDefine defines[RANDO_LOGIC_MAX_DEFINES];
    uint32_t symbol_count;
    uint32_t item_count;
    uint32_t location_count;
    uint32_t helper_count;
    uint32_t node_count;
    uint32_t define_count;
    uint32_t native_mapped_items;
    bool loaded;
    bool native_assignable;
    bool ensure_reachability;
    char error[128];
} LogicModel;

static LogicModel sLogic;
static char sLineBuf[LINE_MAX_LEN + 1];
static char sExpandedLine[LINE_MAX_LEN + 1];

static char* LTrim(char* s) {
    while (*s && isspace((unsigned char)*s)) ++s;
    return s;
}

static void RTrim(char* s) {
    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1])) {
        s[--n] = '\0';
    }
}

static char* Trim(char* s) {
    s = LTrim(s);
    RTrim(s);
    return s;
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

static void Undefine(const char* name) {
    int idx = FindDefine(name);
    if (idx < 0) return;
    uint32_t uidx = (uint32_t)idx;
    if (uidx + 1 < sLogic.define_count) {
        memmove(&sLogic.defines[uidx], &sLogic.defines[uidx + 1],
                (sLogic.define_count - uidx - 1) * sizeof(sLogic.defines[0]));
    }
    sLogic.define_count--;
}

static void ResolveDefineToken(const char* token, char* out, size_t out_len) {
    char tmp[NAME_MAX_LEN + 1];
    size_t len = strlen(token);
    if (len >= 2 && token[0] == '`' && token[len - 1] == '`') {
        CopyName(tmp, sizeof(tmp), token + 1, len - 2);
        const char* val = DefineValue(tmp);
        if (val != NULL && val[0] != '\0') {
            CopyName(out, out_len, val, strlen(val));
        } else {
            CopyName(out, out_len, tmp, strlen(tmp));
        }
    } else {
        CopyName(out, out_len, token, len);
    }
}

static int SplitDashFields(char* s, char** fields, int max_fields) {
    int count = 0;
    char* p = s;
    p = Trim(p);
    if (*p == '-') p++;
    while (*p && count < max_fields) {
        p = Trim(p);
        fields[count++] = p;
        char* sep = strstr(p, " - ");
        if (sep == NULL) break;
        *sep = '\0';
        RTrim(p);
        p = sep + 3;
    }
    for (int i = 0; i < count; ++i) fields[i] = Trim(fields[i]);
    return count;
}

static int SplitSemiFields(char* s, char** fields, int max_fields) {
    int count = 0;
    char* p = s;
    while (count < max_fields) {
        fields[count++] = Trim(p);
        char* sep = strchr(p, ';');
        if (sep == NULL) break;
        *sep = '\0';
        p = sep + 1;
    }
    return count;
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

/* MinishMaker `.logic` item symbol (with the leading "Items." stripped) ->
 * native engine item id. Names taken from the public default.logic item pool.
 * Unmapped symbols return ITEM_NONE so the location keeps its vanilla reward. */
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

static uint16_t NativeItemFromSymbolName(const char* symbol_name) {
    const char* name = symbol_name;
    if (StartsWith(name, "Items.")) name += 6;
    return NativeItemFromBareName(name);
}

static RandoLogicItemType ParseItemType(const char* s) {
    if (strcmp(s, "Music") == 0) return RANDO_LOGIC_ITEM_MUSIC;
    if (strcmp(s, "DungeonEntrance") == 0) return RANDO_LOGIC_ITEM_DUNGEON_ENTRANCE;
    if (strcmp(s, "DungeonConstraint") == 0) return RANDO_LOGIC_ITEM_DUNGEON_CONSTRAINT;
    if (strcmp(s, "OverworldConstraint") == 0) return RANDO_LOGIC_ITEM_OVERWORLD_CONSTRAINT;
    if (strcmp(s, "DungeonPrize") == 0) return RANDO_LOGIC_ITEM_DUNGEON_PRIZE;
    if (strcmp(s, "DungeonMajor") == 0) return RANDO_LOGIC_ITEM_DUNGEON_MAJOR;
    if (strcmp(s, "DungeonMinor") == 0) return RANDO_LOGIC_ITEM_DUNGEON_MINOR;
    if (strcmp(s, "Major") == 0) return RANDO_LOGIC_ITEM_MAJOR;
    if (strcmp(s, "Minor") == 0) return RANDO_LOGIC_ITEM_MINOR;
    if (strcmp(s, "Filler") == 0) return RANDO_LOGIC_ITEM_FILLER;
    return RANDO_LOGIC_ITEM_UNKNOWN;
}

static RandoLogicLocationType ParseLocationType(const char* s) {
    if (strcmp(s, "Music") == 0) return RANDO_LOGIC_LOCATION_MUSIC;
    if (strcmp(s, "Helper") == 0) return RANDO_LOGIC_LOCATION_HELPER;
    if (strcmp(s, "Unshuffled") == 0) return RANDO_LOGIC_LOCATION_UNSHUFFLED;
    if (strcmp(s, "UnshuffledPrize") == 0) return RANDO_LOGIC_LOCATION_UNSHUFFLED_PRIZE;
    if (strcmp(s, "DungeonEntrance") == 0) return RANDO_LOGIC_LOCATION_DUNGEON_ENTRANCE;
    if (strcmp(s, "DungeonConstraint") == 0) return RANDO_LOGIC_LOCATION_DUNGEON_CONSTRAINT;
    if (strcmp(s, "OverworldConstraint") == 0) return RANDO_LOGIC_LOCATION_OVERWORLD_CONSTRAINT;
    if (strcmp(s, "DungeonPrize") == 0) return RANDO_LOGIC_LOCATION_DUNGEON_PRIZE;
    if (strcmp(s, "Major") == 0) return RANDO_LOGIC_LOCATION_MAJOR;
    if (strcmp(s, "Dungeon") == 0) return RANDO_LOGIC_LOCATION_DUNGEON;
    if (strcmp(s, "Any") == 0) return RANDO_LOGIC_LOCATION_ANY;
    if (strcmp(s, "Minor") == 0) return RANDO_LOGIC_LOCATION_MINOR;
    if (strcmp(s, "Inaccessible") == 0) return RANDO_LOGIC_LOCATION_INACCESSIBLE;
    return RANDO_LOGIC_LOCATION_UNKNOWN;
}

static char* StripInlineComment(char* line) {
    bool in_quote = false;
    for (char* p = line; *p; ++p) {
        if (*p == '\'') in_quote = !in_quote;
        if (*p == '#' && !in_quote) {
            *p = '\0';
            break;
        }
    }
    return line;
}

static char* ExpandBackticks(char* line) {
    size_t out = 0;
    for (size_t i = 0; line[i] != '\0' && out < LINE_MAX_LEN; ) {
        if (line[i] == '`') {
            size_t j = i + 1;
            while (line[j] != '\0' && line[j] != '`') ++j;
            if (line[j] == '`') {
                char key[NAME_MAX_LEN + 1];
                CopyName(key, sizeof(key), line + i + 1, j - i - 1);
                /* A define with no/empty value expands to nothing; an unknown
                 * token also expands to empty (never the literal key name). */
                const char* value = DefineValue(key);
                if (value == NULL) value = "";
                for (size_t k = 0; value[k] != '\0' && out < LINE_MAX_LEN; ++k) {
                    sExpandedLine[out++] = value[k];
                }
                i = j + 1;
                continue;
            }
        }
        sExpandedLine[out++] = line[i++];
    }
    sExpandedLine[out] = '\0';
    return sExpandedLine;
}

static uint16_t CompileExpr(char* expr);
static uint16_t CompileTerm(char* t);

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

/* Index of the ')' matching the leading '(' of s (s[0] must be '('), or -1. */
static int MatchingParen(const char* s) {
    int depth = 0;
    for (int i = 0; s[i] != '\0'; ++i) {
        if (s[i] == '(') depth++;
        else if (s[i] == ')') {
            if (--depth == 0) return i;
        }
    }
    return -1;
}

/* Compile each top-level (depth-0) comma-separated term of `inner` as a child
 * of `parent`. Index-based, so it always makes forward progress. */
static void CompileChildren(char* inner, uint16_t parent) {
    int depth = 0;
    char* seg = inner;
    for (char* p = inner;; ++p) {
        char c = *p;
        if (c == '(') depth++;
        else if (c == ')') { if (depth > 0) depth--; }
        if (c == '\0' || (c == ',' && depth == 0)) {
            char saved = c;
            *p = '\0';
            char* term = Trim(seg);
            if (*term != '\0') AddChild(parent, CompileTerm(term));
            *p = saved;
            if (saved == '\0') break;
            seg = p + 1;
        }
    }
}

/* A plain `Items.X[:weight]` reference (no operators, parens, or '~'). */
static uint16_t CompileAtom(char* token) {
    token = Trim(token);
    if (*token == '\0') return AddNode(EXPR_TRUE);
    char* weight_sep = strchr(token, ':');
    uint16_t weight = 1;
    if (weight_sep != NULL) {
        *weight_sep = '\0';
        weight = (uint16_t)strtoul(weight_sep + 1, NULL, 0);
        if (weight == 0) weight = 1;
    }
    uint16_t n = AddNode(EXPR_SYMBOL);
    uint16_t sym = FindOrAddSymbol(token, StartsWith(token, "Items.") ? SYMBOL_ITEM : SYMBOL_UNKNOWN);
    sLogic.nodes[n].symbol = sym;
    sLogic.nodes[n].weight = weight;
    return n;
}

/* Inner contents of a parenthesised group: optional leading |/&/+N operator,
 * then comma-separated terms. */
static uint16_t CompileGroup(char* inner) {
    ExprNodeType type = EXPR_AND;
    uint16_t threshold = 0;
    inner = Trim(inner);
    if (*inner == '|') { type = EXPR_OR; inner++; }
    else if (*inner == '&') { type = EXPR_AND; inner++; }
    else if (*inner == '+') {
        type = EXPR_COUNT;
        inner++;
        threshold = (uint16_t)strtoul(inner, &inner, 0);
    }
    if (*inner == ',') inner++;
    uint16_t n = AddNode(type);
    if (n == UINT16_MAX) return n;
    sLogic.nodes[n].threshold = threshold;
    CompileChildren(inner, n);
    return n;
}

/* A single term: `~term`, a `(...)` group (possibly followed by more), or an
 * atom. Bounded recursion; always terminates. */
static int sExprDepth;
static uint16_t CompileTerm(char* t) {
    t = Trim(t);
    if (*t == '\0') return AddNode(EXPR_TRUE);
    if (sExprDepth > 256) return AddNode(EXPR_TRUE);
    sExprDepth++;
    uint16_t result;
    if (*t == '~') {
        uint16_t n = AddNode(EXPR_NOT);
        AddChild(n, CompileTerm(t + 1));
        result = n;
    } else if (*t == '(') {
        int close = MatchingParen(t);
        if (close < 0) {
            result = CompileAtom(t + 1); /* unmatched '(' — best effort */
        } else {
            char* after = t + close + 1;
            t[close] = '\0';
            uint16_t grp = CompileGroup(t + 1);
            t[close] = ')';
            after = Trim(after);
            if (*after == ',') after++;
            after = Trim(after);
            if (*after == '\0') {
                result = grp;
            } else {
                uint16_t n = AddNode(EXPR_AND);
                AddChild(n, grp);
                AddChild(n, CompileExpr(after));
                result = n;
            }
        }
    } else {
        result = CompileAtom(t);
    }
    sExprDepth--;
    return result;
}

static uint16_t CompileExpr(char* expr) {
    expr = Trim(expr);
    if (*expr == '\0') return AddNode(EXPR_TRUE);

    /* Count top-level commas to decide AND-list vs single term. */
    int depth = 0;
    bool has_top_comma = false;
    for (char* p = expr; *p; ++p) {
        if (*p == '(') depth++;
        else if (*p == ')') { if (depth > 0) depth--; }
        else if (*p == ',' && depth == 0) { has_top_comma = true; break; }
    }
    if (!has_top_comma) return CompileTerm(expr);
    uint16_t n = AddNode(EXPR_AND);
    if (n == UINT16_MAX) return n;
    CompileChildren(expr, n);
    return n;
}

static bool AddLogicItem(const char* symbol_name, RandoLogicItemType type, uint32_t amount) {
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
        if (item->native_item != NITEM_NONE) sLogic.native_mapped_items++;
        sLogic.symbols[sym].kind = SYMBOL_ITEM;
        sLogic.symbols[sym].index = (uint16_t)sLogic.item_count;
        sLogic.item_count++;
    }
    return true;
}

static bool ParseItemPoolLine(char* line) {
    char* fields[4] = {0};
    int count = SplitSemiFields(line, fields, 4);
    if (count < 2) return true;
    char item_name[NAME_MAX_LEN + 1];
    char* spec = fields[0];
    char* colon = strchr(spec, ':');
    uint32_t amount = 1;
    if (colon != NULL) {
        /* `:[amount].[multiplier]` — total = ceil(amount / multiplier). */
        char* endp = NULL;
        *colon = '\0';
        amount = (uint32_t)strtoul(colon + 1, &endp, 0);
        if (amount == 0) amount = 1;
        if (endp != NULL && *endp == '.') {
            uint32_t mult = (uint32_t)strtoul(endp + 1, NULL, 0);
            if (mult > 1) amount = (amount + mult - 1) / mult;
            if (amount == 0) amount = 1;
        }
    }
    CopyName(item_name, sizeof(item_name), spec, strlen(spec));
    RandoLogicItemType type = ParseItemType(fields[1]);
    if (type == RANDO_LOGIC_ITEM_UNKNOWN) return true;
    return AddLogicItem(item_name, type, amount);
}
/* Runtime key for the in-game reward hooks: only the `area-room-chest` triple
 * maps to the engine's (area, room, local-flag) chest identity. Precise ROM
 * addresses and `:Define` forms target ROM write addresses the native hooks
 * don't use, so they get no runtime key (UINT32_MAX). */
static uint32_t ParseLocationKey(const char* field) {
    unsigned a, b, c;
    if (field == NULL) return UINT32_MAX;
    while (*field && isspace((unsigned char)*field)) ++field;
    if (sscanf(field, "%x-%x-%x", &a, &b, &c) == 3) {
        return ((a & 0xffu) << 16) | ((b & 0xffu) << 8) | (c & 0xffu);
    }
    return UINT32_MAX;
}


static bool ParseLocationLine(char* line) {
    char* fields[6] = {0};
    int count = SplitSemiFields(line, fields, 6);
    if (count < 2) return true;
    RandoLogicLocationType type = ParseLocationType(fields[1]);
    if (type == RANDO_LOGIC_LOCATION_UNKNOWN) return true;
    if (type == RANDO_LOGIC_LOCATION_INACCESSIBLE) return true;
    if (sLogic.location_count >= RANDO_LOGIC_MAX_LOCATIONS) {
        SetError("too many locations");
        return false;
    }

    char name[NAME_MAX_LEN + 1];
    char symbol_name[NAME_MAX_LEN + 10];
    char* name_end = strchr(fields[0], ':');
    if (name_end == NULL) name_end = fields[0] + strlen(fields[0]);
    CopyName(name, sizeof(name), fields[0], (size_t)(name_end - fields[0]));

    LogicLocation* loc = &sLogic.locations[sLogic.location_count];
    memset(loc, 0, sizeof(*loc));
    CopyName(loc->name, sizeof(loc->name), name, strlen(name));
    loc->type = type;
    loc->is_helper = (type == RANDO_LOGIC_LOCATION_HELPER);
    loc->fixed_item_symbol = UINT16_MAX;
    loc->item_symbol = UINT16_MAX;
    loc->expr = UINT16_MAX;
    loc->key = (count >= 3) ? ParseLocationKey(fields[2]) : UINT32_MAX;

    snprintf(symbol_name, sizeof(symbol_name), "%s.%s", loc->is_helper ? "Helpers" : "Locations", name);
    loc->symbol = FindOrAddSymbol(symbol_name, loc->is_helper ? SYMBOL_HELPER : SYMBOL_LOCATION);
    if (loc->symbol == UINT16_MAX) return false;
    sLogic.symbols[loc->symbol].index = (uint16_t)sLogic.location_count;

    if (!loc->is_helper) {
        char alt_symbol[NAME_MAX_LEN + 10];
        snprintf(alt_symbol, sizeof(alt_symbol), "Helpers.%s", name);
        uint16_t alt = FindOrAddSymbol(alt_symbol, SYMBOL_HELPER);
        if (alt != UINT16_MAX) sLogic.symbols[alt].index = (uint16_t)sLogic.location_count;
    } else {
        sLogic.helper_count++;
    }

    if (count >= 4 && fields[3] != NULL && fields[3][0] != '\0') {
        loc->expr = CompileExpr(fields[3]);
    } else {
        loc->expr = AddNode(EXPR_TRUE);
    }
    /* The 5th item field only fixes the reward on Unshuffled locations; on
     * every other type it is just the vanilla item (informational), so the
     * location stays open for placement. */
    if ((type == RANDO_LOGIC_LOCATION_UNSHUFFLED || type == RANDO_LOGIC_LOCATION_UNSHUFFLED_PRIZE) &&
        count >= 5 && fields[4] != NULL && StartsWith(fields[4], "Items.")) {
        loc->fixed_item_symbol = FindOrAddSymbol(fields[4], SYMBOL_ITEM);
    }

    sLogic.location_count++;
    return true;
}

static void ParseFlagDirective(char* args) {
    char* fields[8] = {0};
    int n = SplitDashFields(args, fields, 8);
    /* `!flag - tab - type - group - DEFINE - readable - tooltip - [default]`.
     * The optional default ("true") is field index 6. */
    if (n >= 5) {
        bool default_true = (n >= 7 && fields[6] != NULL && strcmp(fields[6], "true") == 0);
        if (default_true) SetDefineValue(fields[3], NULL, false);
    }
}

static void ParseDropdownDirective(char* args) {
    char* fields[10] = {0};
    int n = SplitDashFields(args, fields, 10);
    if (n >= 7) {
        SetDefineValue(fields[3], fields[6], true);
    }
}

static void ParseNumberboxDirective(char* args) {
    char* fields[10] = {0};
    int n = SplitDashFields(args, fields, 10);
    if (n >= 7) {
        SetDefineValue(fields[3], fields[6], true);
    }
}

static void ParseDefineDirective(char* args) {
    char* fields[3] = {0};
    int n = SplitDashFields(args, fields, 3);
    if (n >= 1 && fields[0][0] != '\0') {
        char name[NAME_MAX_LEN + 1];
        char value[VALUE_MAX_LEN + 1];
        ResolveDefineToken(fields[0], name, sizeof(name));
        if (n >= 2 && fields[1][0] != '\0') {
            ResolveDefineToken(fields[1], value, sizeof(value));
            SetDefineValue(name, value, true);
        } else {
            SetDefineValue(name, NULL, false);
        }
    }
}

static void ParseUndefineDirective(char* args) {
    char* fields[1] = {0};
    int n = SplitDashFields(args, fields, 1);
    if (n >= 1) {
        char name[NAME_MAX_LEN + 1];
        ResolveDefineToken(fields[0], name, sizeof(name));
        Undefine(name);
    }

}

static RandoLogicItemType ItemTypeFromLocationType(RandoLogicLocationType type) {
    switch (type) {
        case RANDO_LOGIC_LOCATION_MAJOR: return RANDO_LOGIC_ITEM_MAJOR;
        case RANDO_LOGIC_LOCATION_MINOR: return RANDO_LOGIC_ITEM_MINOR;
        case RANDO_LOGIC_LOCATION_DUNGEON: return RANDO_LOGIC_ITEM_DUNGEON_MAJOR;
        case RANDO_LOGIC_LOCATION_DUNGEON_PRIZE: return RANDO_LOGIC_ITEM_DUNGEON_PRIZE;
        case RANDO_LOGIC_LOCATION_DUNGEON_ENTRANCE: return RANDO_LOGIC_ITEM_DUNGEON_ENTRANCE;
        case RANDO_LOGIC_LOCATION_DUNGEON_CONSTRAINT: return RANDO_LOGIC_ITEM_DUNGEON_CONSTRAINT;
        case RANDO_LOGIC_LOCATION_OVERWORLD_CONSTRAINT: return RANDO_LOGIC_ITEM_OVERWORLD_CONSTRAINT;
        case RANDO_LOGIC_LOCATION_MUSIC: return RANDO_LOGIC_ITEM_MUSIC;
        default: return RANDO_LOGIC_ITEM_UNKNOWN;
    }
}

static void ParseItemSymbolFromChance(char* spec, char* out, size_t out_len) {
    char* p = Trim(spec);
    char* end = p;
    while (*end && *end != ',' && *end != ';' && !isspace((unsigned char)*end)) ++end;
    char* colon = strchr(p, ':');
    if (colon != NULL && colon < end) end = colon;
    CopyName(out, out_len, p, (size_t)(end - p));
}

static uint32_t ParseValueToken(char* token) {
    token = Trim(token);
    if (token[0] == '`') {
        char name[NAME_MAX_LEN + 1];
        ResolveDefineToken(token, name, sizeof(name));
        const char* value = DefineValue(name);
        return value != NULL ? (uint32_t)strtoul(value, NULL, 0) : (uint32_t)strtoul(name, NULL, 0);
    }
    const char* value = DefineValue(token);
    return value != NULL ? (uint32_t)strtoul(value, NULL, 0) : (uint32_t)strtoul(token, NULL, 0);
}

static void ParseAdditionDirective(char* args) {
    char* fields[2] = {0};
    int n = SplitDashFields(args, fields, 2);
    if (n < 2) return;
    uint32_t sum = 0;
    char* p = fields[1];
    while (p != NULL && *p != '\0') {
        char* comma = strchr(p, ',');
        if (comma != NULL) *comma = '\0';
        char token[VALUE_MAX_LEN + 1];
        CopyName(token, sizeof(token), p, strlen(p));
        sum += ParseValueToken(token);
        p = (comma != NULL) ? comma + 1 : NULL;
    }
    char value[VALUE_MAX_LEN + 1];
    snprintf(value, sizeof(value), "%u", sum);
    SetDefineValue(fields[0], value, true);
}

static void ReplaceItemsBySymbol(const char* old_symbol, const char* new_symbol, uint32_t amount) {
    uint16_t new_sym = FindOrAddSymbol(new_symbol, SYMBOL_ITEM);
    if (new_sym == UINT16_MAX) return;
    uint32_t replaced = 0;
    for (uint32_t i = 0; i < sLogic.item_count; ++i) {
        if (strcmp(sLogic.symbols[sLogic.items[i].symbol].name, old_symbol) != 0) continue;
        sLogic.items[i].symbol = new_sym;
        sLogic.items[i].native_item = NativeItemFromSymbolName(new_symbol);
        if (amount != 0 && ++replaced >= amount) break;
        if (amount == 0) replaced++;
    }
}

static void ParseReplaceDirective(char* args) {
    char* fields[2] = {0};
    int n = SplitDashFields(args, fields, 2);
    if (n < 2) return;
    char new_symbol[NAME_MAX_LEN + 1];
    ParseItemSymbolFromChance(fields[1], new_symbol, sizeof(new_symbol));
    if (new_symbol[0] != '\0') ReplaceItemsBySymbol(fields[0], new_symbol, 0);
}

static void ParseReplaceAmountDirective(char* args) {
    char* fields[3] = {0};
    int n = SplitDashFields(args, fields, 3);
    if (n < 3) return;
    char new_symbol[NAME_MAX_LEN + 1];
    ParseItemSymbolFromChance(fields[0], new_symbol, sizeof(new_symbol));
    uint32_t amount = (uint32_t)strtoul(fields[1], NULL, 0);
    if (new_symbol[0] != '\0' && amount != 0) ReplaceItemsBySymbol(fields[2], new_symbol, amount);
}

static void ParseReplaceIncrementDirective(char* args) {
    char* fields[3] = {0};
    int n = SplitDashFields(args, fields, 3);
    if (n < 3) return;
    uint32_t amount = (uint32_t)strtoul(fields[1], NULL, 0);
    char old_symbol[NAME_MAX_LEN + 1];
    ParseItemSymbolFromChance(fields[2], old_symbol, sizeof(old_symbol));
    if (amount == 0 || old_symbol[0] == '\0') return;
    for (uint32_t i = 0, replaced = 0; i < sLogic.item_count && replaced < amount; ++i) {
        if (strcmp(sLogic.symbols[sLogic.items[i].symbol].name, old_symbol) != 0) continue;
        char new_symbol[NAME_MAX_LEN + 1];
        snprintf(new_symbol, sizeof(new_symbol), "%s%u", fields[0], replaced);
        uint16_t sym = FindOrAddSymbol(new_symbol, SYMBOL_ITEM);
        if (sym == UINT16_MAX) return;
        sLogic.items[i].symbol = sym;
        sLogic.items[i].native_item = NativeItemFromSymbolName(new_symbol);
        replaced++;
    }
}

static void ParseSetTypeDirective(char* args) {
    char* fields[2] = {0};
    int n = SplitDashFields(args, fields, 2);
    if (n < 2) return;
    RandoLogicItemType new_type = ItemTypeFromLocationType(ParseLocationType(fields[1]));
    if (new_type == RANDO_LOGIC_ITEM_UNKNOWN) new_type = ParseItemType(fields[1]);
    if (new_type == RANDO_LOGIC_ITEM_UNKNOWN) return;
    for (uint32_t i = 0; i < sLogic.item_count; ++i) {
        if (strcmp(sLogic.symbols[sLogic.items[i].symbol].name, fields[0]) == 0) {
            sLogic.items[i].type = new_type;
        }
    }
}

static bool IsItemLine(const char* s) {
    return StartsWith(s, "Items.") && strchr(s, ';') != NULL;
}

static bool IsLocationCandidate(const char* s) {
    return strchr(s, ';') != NULL && !StartsWith(s, "Items.") && !StartsWith(s, "!");
}

static uint32_t CountNativeMappedItems(void) {
    uint32_t count = 0;
    for (uint32_t i = 0; i < sLogic.item_count; ++i) {
        sLogic.items[i].native_item = NativeItemFromSymbolName(sLogic.symbols[sLogic.items[i].symbol].name);
        if (sLogic.items[i].native_item != NITEM_NONE) count++;
    }
    return count;
}

static bool ProcessDirective(char* line, CondFrame* stack, int* depth, bool* active) {
    if (StartsWith(line, "!ifdef")) {
        char* args = line + 6;
        char* fields[1] = {0};
        SplitDashFields(args, fields, 1);
        bool cond = fields[0] != NULL && DefineExists(fields[0]);
        if (*depth < 64) {
            stack[*depth].parent_active = *active;
            stack[*depth].condition_true = cond;
            stack[*depth].active = *active && cond;
            stack[*depth].else_seen = false;
            *active = stack[*depth].active;
            (*depth)++;
        }
        return true;
    }
    if (StartsWith(line, "!ifndef")) {
        char* args = line + 7;
        char* fields[1] = {0};
        SplitDashFields(args, fields, 1);
        bool cond = fields[0] != NULL && !DefineExists(fields[0]);
        if (*depth < 64) {
            stack[*depth].parent_active = *active;
            stack[*depth].condition_true = cond;
            stack[*depth].active = *active && cond;
            stack[*depth].else_seen = false;
            *active = stack[*depth].active;
            (*depth)++;
        }
        return true;
    }
    if (StartsWith(line, "!else")) {
        if (*depth > 0) {
            CondFrame* f = &stack[*depth - 1];
            f->active = f->parent_active && !f->condition_true && !f->else_seen;
            f->else_seen = true;
            *active = f->active;
        }
        return true;
    }
    if (StartsWith(line, "!endif")) {
        if (*depth > 0) {
            (*depth)--;
            *active = (*depth > 0) ? stack[*depth - 1].active : true;
        }
        return true;
    }

    if (!*active) return true;
    if (StartsWith(line, "!flag")) { ParseFlagDirective(line + 5); return true; }
    if (StartsWith(line, "!dropdown")) { ParseDropdownDirective(line + 9); return true; }
    if (StartsWith(line, "!numberbox")) { ParseNumberboxDirective(line + 10); return true; }
    if (StartsWith(line, "!define")) { ParseDefineDirective(line + 7); return true; }
    if (StartsWith(line, "!undefine")) { ParseUndefineDirective(line + 9); return true; }
    if (StartsWith(line, "!addition")) { ParseAdditionDirective(line + 9); return true; }
    if (StartsWith(line, "!replaceamount")) { ParseReplaceAmountDirective(line + 14); return true; }
    if (StartsWith(line, "!replaceincrement")) { ParseReplaceIncrementDirective(line + 17); return true; }
    if (StartsWith(line, "!replace")) { ParseReplaceDirective(line + 8); return true; }
    if (StartsWith(line, "!settype")) { ParseSetTypeDirective(line + 8); return true; }
    if (StartsWith(line, "!import")) { return true; }
    if (StartsWith(line, "!prizeplacement")) { return true; }
    if (StartsWith(line, "!ensurereachability")) { sLogic.ensure_reachability = true; return true; }

    /* Other MinishMaker directives are patch/UI controls ignored by the
     * native generator until a matching engine subsystem exists. */
    return true;
}

static const char* NextLine(const char* text, const char* end) {
    size_t n = 0;
    while (text < end && *text != '\n' && n < LINE_MAX_LEN) {
        sLineBuf[n++] = *text++;
    }
    while (text < end && *text != '\n') ++text;
    if (text < end && *text == '\n') ++text;
    if (n > 0 && sLineBuf[n - 1] == '\r') n--;
    sLineBuf[n] = '\0';
    return text;
}

extern "C" void RandoLogic_Reset(void) {
    memset(&sLogic, 0, sizeof(sLogic));
}

extern "C" bool RandoLogic_LoadText(const char* text, size_t len) {
    CondFrame stack[64];
    int depth = 0;
    bool active = true;

    RandoLogic_Reset();

    if (text == NULL) {
        SetError("null logic text");
        return false;
    }

    const char* p = text;
    const char* end = text + len;
    while (p < end) {
        p = NextLine(p, end);
        char* line = Trim(ExpandBackticks(StripInlineComment(sLineBuf)));
        if (*line == '\0') continue;
        if (*line == '!') {
            ProcessDirective(line, stack, &depth, &active);
            continue;
        }
        if (!active) continue;
        if (IsItemLine(line)) {
            ParseItemPoolLine(line);
        } else if (IsLocationCandidate(line)) {
            ParseLocationLine(line);
        }
        if (sLogic.error[0] != '\0') break;
    }

    sLogic.native_mapped_items = CountNativeMappedItems();
    sLogic.loaded = (sLogic.error[0] == '\0');
    sLogic.native_assignable = sLogic.loaded && sLogic.location_count <= RANDO_LOCATION_COUNT &&
                               sLogic.item_count > 0 && sLogic.native_mapped_items == sLogic.item_count;
    return sLogic.loaded;
}

static bool LoadLogicFilePath(const char* path) {
    if (path == NULL || path[0] == '\0') return false;
    FILE* f = fopen(path, "rb");
    if (f == NULL) return false;
    static char sLogicFileBuffer[1024 * 1024 + 1];
    size_t n = fread(sLogicFileBuffer, 1, sizeof(sLogicFileBuffer) - 1, f);
    int extra = fgetc(f);
    fclose(f);
    if (extra != EOF) {
        RandoLogic_Reset();
        SetError("logic file too large");
        fprintf(stderr, "[RANDO] logic file too large: %s\n", path);
        return false;
    }
    sLogicFileBuffer[n] = '\0';
    if (!RandoLogic_LoadText(sLogicFileBuffer, n)) {
        RandoLogicStats stats = RandoLogic_GetStats();
        fprintf(stderr, "[RANDO] logic parse failed for %s: %s\n", path, stats.error);
        return false;
    }
    RandoLogicStats stats = RandoLogic_GetStats();
    fprintf(stderr,
            "[RANDO] loaded logic %s (%u items, %u locations, %u helpers, %u nodes, native=%u)\n",
            path,
            stats.item_count,
            stats.location_count,
            stats.helper_count,
            stats.node_count,
            stats.native_assignable ? 1u : 0u);
    return true;
}

extern "C" bool RandoLogic_LoadDefaultFiles(void) {
    const char* env_path = getenv("TMC_RANDO_LOGIC");
    if (LoadLogicFilePath(env_path)) return true;

    static const char* const kCandidates[] = {
        "assets/rando/default.logic",

        "dist/USA/assets/rando/default.logic",
        "dist/EU/assets/rando/default.logic",
        "rando/default.logic",
        "default.logic",
    };
    for (size_t i = 0; i < ARRAY_COUNT(kCandidates); ++i) {
        if (LoadLogicFilePath(kCandidates[i])) return true;
    }

    fprintf(stderr, "[RANDO] no external .logic file found; using built-in native graph\n");
    return false;
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

extern "C" uint32_t RandoLogic_GetLocationCountRaw(void) {
    return sLogic.location_count;
}

extern "C" const char* RandoLogic_GetLocationName(uint32_t index) {
    return index < sLogic.location_count ? sLogic.locations[index].name : "";
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
    stats.loaded = sLogic.loaded;
    stats.native_assignable = sLogic.native_assignable;
    CopyName(stats.error, sizeof(stats.error), sLogic.error, strlen(sLogic.error));
    return stats;
}

/* Item-type -> acceptable location-type matrix, matching the documented
 * placement fallbacks (Major -> Major/Dungeon/Any; Minor -> Minor/Any; leftover
 * dungeon/major locations behave as Any; Filler fills whatever is left). */
static bool AllowedAt(RandoLogicItemType it, RandoLogicLocationType lt) {
    switch (it) {
        case RANDO_LOGIC_ITEM_DUNGEON_ENTRANCE:     return lt == RANDO_LOGIC_LOCATION_DUNGEON_ENTRANCE;
        case RANDO_LOGIC_ITEM_DUNGEON_CONSTRAINT:   return lt == RANDO_LOGIC_LOCATION_DUNGEON_CONSTRAINT;
        case RANDO_LOGIC_ITEM_OVERWORLD_CONSTRAINT: return lt == RANDO_LOGIC_LOCATION_OVERWORLD_CONSTRAINT;
        case RANDO_LOGIC_ITEM_DUNGEON_PRIZE:        return lt == RANDO_LOGIC_LOCATION_DUNGEON_PRIZE ||
                                                           lt == RANDO_LOGIC_LOCATION_DUNGEON;
        case RANDO_LOGIC_ITEM_DUNGEON_MAJOR:        return lt == RANDO_LOGIC_LOCATION_DUNGEON;
        case RANDO_LOGIC_ITEM_DUNGEON_MINOR:        return lt == RANDO_LOGIC_LOCATION_DUNGEON;
        case RANDO_LOGIC_ITEM_MAJOR:                return lt == RANDO_LOGIC_LOCATION_MAJOR ||
                                                           lt == RANDO_LOGIC_LOCATION_DUNGEON ||
                                                           lt == RANDO_LOGIC_LOCATION_ANY;
        case RANDO_LOGIC_ITEM_MINOR:                return lt == RANDO_LOGIC_LOCATION_MINOR ||
                                                           lt == RANDO_LOGIC_LOCATION_ANY;
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
    bool item_owned[RANDO_LOGIC_MAX_SYMBOLS];
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
            if (sym->kind == SYMBOL_ITEM) return state->item_owned[n->symbol];
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
                if (EvalNode(c, state)) count += sLogic.nodes[c].weight;
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

static void CollectReachable(EvalState* state, uint16_t* assignment) {
    bool progressed;
    do {
        progressed = false;
        for (uint32_t i = 0; i < sLogic.location_count; ++i) {
            LogicLocation* loc = &sLogic.locations[i];
            if (loc->is_helper || state->location_reached[i]) continue;
            if (!EvalLocation((uint16_t)i, state)) continue;
            state->location_reached[i] = true;
            progressed = true;
            uint16_t sym = loc->fixed_item_symbol;
            if (assignment != NULL && assignment[i] != UINT16_MAX) sym = assignment[i];
            if (sym != UINT16_MAX) state->item_owned[sym] = true;
        }
    } while (progressed);
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

/* Assumed-fill placement matching the documented MinishMaker algorithm:
 * advancement items are placed so the seed stays beatable (every item is
 * reachable assuming you already hold all not-yet-placed advancement items),
 * typed pools are honoured in priority order with the documented fallbacks,
 * `~Items.X` guards block placement, and the result is verified per the
 * selected accessibility mode. Filler fills whatever is left. */
extern "C" RandoStatus RandoLogic_Generate(uint64_t seed, const RandomizerSettings* settings,
                                            uint16_t* out_table, size_t out_table_count,
                                            uint64_t* out_seed) {
    (void)settings;
    if (!sLogic.loaded) return RANDO_INACTIVE;
    if (out_table == NULL || out_table_count < sLogic.location_count) return RANDO_BAD_SETTINGS;

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
    rng.state = seed ? seed : 1u;
    ComputeAdvancement(sym_in_logic);
    static bool is_pool_item[RANDO_LOGIC_MAX_SYMBOLS];
    static bool free_item[RANDO_LOGIC_MAX_SYMBOLS];
    memset(assumed_count, 0, sizeof(assumed_count));
    memset(placed, 0, sizeof(placed));
    memset(is_pool_item, 0, sizeof(is_pool_item));
    for (uint32_t i = 0; i < sLogic.location_count; ++i) assignment[i] = UINT16_MAX;
    for (uint32_t i = 0; i < sLogic.item_count; ++i) {
        uint16_t s = sLogic.items[i].symbol;
        is_pool_item[s] = true;
        if (sLogic.items[i].type != RANDO_LOGIC_ITEM_FILLER && sym_in_logic[s]) {
            assumed_count[s]++;
        }
    }
    /* Item symbols that appear in logic but are not in the shuffled pool are
     * start items / fixed grants / guaranteed events: assume them owned so the
     * assumed-fill baseline matches "have everything". Final verification still
     * uses strict reachability (pool + fixed items only). */
    for (uint32_t s = 0; s < sLogic.symbol_count; ++s) {
        free_item[s] = sym_in_logic[s] && sLogic.symbols[s].kind == SYMBOL_ITEM && !is_pool_item[s];
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
        CollectReachable(&state, NULL);
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
                    state.item_owned[s] = (assumed_count[s] > 0) || free_item[s];
                }
                CollectReachable(&state, assignment);
            }

            for (uint32_t l = 0; l < sLogic.location_count; ++l) {
                LogicLocation* loc = &sLogic.locations[l];
                if (loc->is_helper || loc->fixed_item_symbol != UINT16_MAX || assignment[l] != UINT16_MAX) continue;
                if (!AllowedAt(type, loc->type)) continue;
                if (NodeForbidsSymbol(loc->expr, sym)) continue;
                /* Constraint/entrance items are structural dummies that grant
                 * no real item, so they need not sit in a reachable slot. */
                bool needs_reach = (type != RANDO_LOGIC_ITEM_DUNGEON_CONSTRAINT &&
                                    type != RANDO_LOGIC_ITEM_OVERWORLD_CONSTRAINT &&
                                    type != RANDO_LOGIC_ITEM_DUNGEON_ENTRANCE);
                if (!no_logic && needs_reach && !state.location_reached[l]) continue;
                candidates[cand_count++] = (uint16_t)l;
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
            assignment[candidates[BoundedRandom(&rng, cand_count)]] = sym;
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

    /* Emit the native item table (0 = leave the vanilla reward in place). */
    for (uint32_t l = 0; l < sLogic.location_count; ++l) {
        LogicLocation* loc = &sLogic.locations[l];
        uint16_t sym = (loc->fixed_item_symbol != UINT16_MAX) ? loc->fixed_item_symbol : assignment[l];
        out_table[l] = (loc->is_helper || sym == UINT16_MAX || sym >= sLogic.symbol_count)
                           ? (uint16_t)NITEM_NONE
                           : NativeItemFromSymbolName(sLogic.symbols[sym].name);
    }

    /* Verify per accessibility mode. */
    if (!no_logic) {
        const char* acc = DefineValue("ACCESSIBILITY");
        /* `!ensurereachability` (or ACCESS_LOCATIONS) requires every location
         * reachable; ACCESS_BEATABLE only requires the goal. */
        bool beatable_only = !sLogic.ensure_reachability &&
                             (acc != NULL && strcmp(acc, "ACCESS_BEATABLE") == 0);
        /* Same baseline as placement: pool items are obtained from their
         * placed locations (via assignment), and non-pool logic symbols
         * (start items / guaranteed events) are owned. */
        memset(&state, 0, sizeof(state));
        for (uint32_t s = 0; s < sLogic.symbol_count; ++s) state.item_owned[s] = free_item[s];
        CollectReachable(&state, assignment);
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
            if (goal >= 0 && !EvalLocation((uint16_t)goal, &state)) {
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

    if (out_seed) *out_seed = seed;
    return RANDO_OK;
}
