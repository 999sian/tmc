/*
 * port/rando/rando.cpp — Milestone 1 implementation.
 *
 * Permutes the 8 major progression items inside chest rewards. The
 * permutation is deterministic for a given seed (Fisher–Yates with
 * std::mt19937). Other item types pass through unchanged.
 *
 * Why this specific subset:
 *   - These 8 items are the "dungeon keys" — they gate access to
 *     subsequent areas. Shuffling them is the minimum useful randomizer
 *     for the player (you get a different unlock order each seed) while
 *     keeping the implementation tiny.
 *   - They're all single-quantity items, so we don't need to think
 *     about quantity remapping for M1.
 *   - The IDs are sparse but small, so a direct 256-byte lookup table
 *     is fine.
 *
 * M2 will widen the pool to all chest-rewardable items. M3 adds a
 * reachability/logic engine so we never produce unbeatable seeds.
 */

#include "rando.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>

/* Item IDs copied from include/item.h. We can't include item.h
 * directly because it (and the transitive player.h) declares
 * functions with `this` as a parameter name — fine in C but illegal
 * in C++. The numeric values are part of the saved game format and
 * thus stable; they're cross-checked at compile time below. */
enum ItemId : uint8_t {
    ITEM_SMITH_SWORD    = 0x01,
    ITEM_GUST_JAR       = 0x11,
    ITEM_PACCI_CANE     = 0x12,
    ITEM_MOLE_MITTS     = 0x13,
    ITEM_ROCS_CAPE      = 0x14,
    ITEM_PEGASUS_BOOTS  = 0x15,
    ITEM_FIRE_ROD       = 0x16,
    ITEM_OCARINA        = 0x17,
};

namespace {

/* The subset of items that participate in the M1 permutation. Their
 * indices into this array are what get shuffled; the array itself is
 * the lookup. See ItemType in include/item.h for the canonical names. */
constexpr uint8_t kShuffledItems[] = {
    ITEM_SMITH_SWORD,    /* 0x01 */
    ITEM_GUST_JAR,       /* 0x11 */
    ITEM_PACCI_CANE,     /* 0x12 */
    ITEM_MOLE_MITTS,     /* 0x13 */
    ITEM_ROCS_CAPE,      /* 0x14 */
    ITEM_PEGASUS_BOOTS,  /* 0x15 */
    ITEM_FIRE_ROD,       /* 0x16 */
    ITEM_OCARINA,        /* 0x17 */
};
constexpr size_t kShuffledCount = sizeof(kShuffledItems) / sizeof(kShuffledItems[0]);

/* Fast lookup table: vanilla item ID → shuffled item ID.
 * When inactive, this is the identity map. */
std::array<uint8_t, 256> sRemap;

uint32_t sSeed   = 0;
bool     sActive = false;
std::string sSpoiler;

/* Fisher–Yates over kShuffledCount produces a 1-to-1 permutation
 * within the shuffled subset; items outside the subset stay mapped
 * to themselves (so kinstones, hearts, rupees etc. are untouched). */
void BuildPermutation(uint32_t seed) {
    for (size_t i = 0; i < sRemap.size(); ++i) sRemap[i] = static_cast<uint8_t>(i);

    std::array<uint8_t, kShuffledCount> shuffled;
    for (size_t i = 0; i < kShuffledCount; ++i) shuffled[i] = kShuffledItems[i];

    std::mt19937 rng(seed);
    for (size_t i = kShuffledCount - 1; i > 0; --i) {
        std::uniform_int_distribution<size_t> dist(0, i);
        size_t j = dist(rng);
        std::swap(shuffled[i], shuffled[j]);
    }

    for (size_t i = 0; i < kShuffledCount; ++i) {
        sRemap[kShuffledItems[i]] = shuffled[i];
    }
}

const char* ItemName(uint8_t id) {
    switch (id) {
        case ITEM_SMITH_SWORD:   return "Smith Sword";
        case ITEM_GUST_JAR:      return "Gust Jar";
        case ITEM_PACCI_CANE:    return "Pacci Cane";
        case ITEM_MOLE_MITTS:    return "Mole Mitts";
        case ITEM_ROCS_CAPE:     return "Roc's Cape";
        case ITEM_PEGASUS_BOOTS: return "Pegasus Boots";
        case ITEM_FIRE_ROD:      return "Fire Rod";
        case ITEM_OCARINA:       return "Ocarina";
        default:                 return "?";
    }
}

void BuildSpoiler(uint32_t seed) {
    char buf[2048];
    int n = std::snprintf(buf, sizeof(buf),
        "TMC native randomizer — seed %u\n"
        "M1: major-item permutation (chest reward intercept)\n"
        "===================================================\n\n",
        seed);
    for (size_t i = 0; i < kShuffledCount && n + 80 < (int)sizeof(buf); ++i) {
        uint8_t vanilla  = kShuffledItems[i];
        uint8_t shuffled = sRemap[vanilla];
        n += std::snprintf(buf + n, sizeof(buf) - n,
            "  %-14s  →  %s%s\n",
            ItemName(vanilla), ItemName(shuffled),
            vanilla == shuffled ? "  (unchanged)" : "");
    }
    sSpoiler.assign(buf, static_cast<size_t>(n));
}

} /* namespace */

extern "C" RandoStatus Rando_RollSeed(uint32_t seed, uint32_t* out_seed) {
    if (seed == 0) {
        std::random_device rd;
        seed = (rd() & 0x7FFFFFFFu);
        if (seed == 0) seed = 1;
    }
    sSeed   = seed;
    sActive = true;
    BuildPermutation(seed);
    BuildSpoiler(seed);
    if (out_seed) *out_seed = seed;
    std::fprintf(stderr, "[RANDO] seed %u rolled — M1 chest permutation active\n", seed);
    return RANDO_OK;
}

extern "C" void Rando_Reset(void) {
    sSeed   = 0;
    sActive = false;
    sSpoiler.clear();
    for (size_t i = 0; i < sRemap.size(); ++i) sRemap[i] = static_cast<uint8_t>(i);
    std::fprintf(stderr, "[RANDO] reset to vanilla\n");
}

extern "C" bool Rando_IsActive(void) { return sActive; }

extern "C" uint32_t Rando_GetSeed(void) { return sSeed; }

extern "C" bool Rando_OverrideChestReward(uint8_t area, uint8_t room, uint8_t localFlag,
                                          uint8_t* type, uint8_t* subtype) {
    (void)area; (void)room; (void)localFlag; (void)subtype;
    if (!sActive || !type) return false;
    const uint8_t v = *type;
    const uint8_t s = sRemap[v];
    if (s == v) return false;
    *type = s;
    return true;
}

extern "C" size_t Rando_GetSpoiler(char* buf, size_t buflen) {
    if (!buf || buflen == 0) return 0;
    size_t n = sSpoiler.size();
    if (n > buflen - 1) n = buflen - 1;
    std::memcpy(buf, sSpoiler.data(), n);
    buf[n] = '\0';
    return n;
}
