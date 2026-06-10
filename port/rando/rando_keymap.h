#ifndef PORT_RANDO_KEYMAP_H
#define PORT_RANDO_KEYMAP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    RANDO_SCRIPTED_KEY_STOCKWELL = 1,
    RANDO_SCRIPTED_KEY_GORON_MERCHANT = 2,
    RANDO_SCRIPTED_KEY_DOJO = 3,
    RANDO_SCRIPTED_KEY_CUCCO = 4,
    RANDO_SCRIPTED_KEY_SPECIAL = 5,
    RANDO_SCRIPTED_KEY_SCRUB = 6,
};

enum {
    RANDO_STOCKWELL_SLOT_80 = 0,
    RANDO_STOCKWELL_SLOT_300 = 1,
    RANDO_STOCKWELL_SLOT_600 = 2,
    RANDO_STOCKWELL_SLOT_EXTRA_600 = 3,
    RANDO_STOCKWELL_SLOT_DOGFOOD = 4,
};

enum {
    RANDO_SPECIAL_KEY_CARLOV_MEDAL = 0,
    RANDO_SPECIAL_KEY_DOG_BOTTLE = 1,
    RANDO_SPECIAL_KEY_JABBER_NUT = 2,
    RANDO_SPECIAL_KEY_RED_BOOK = 3,
    RANDO_SPECIAL_KEY_GREEN_BOOK = 4,
    RANDO_SPECIAL_KEY_BLUE_BOOK = 5,
    RANDO_SPECIAL_KEY_MELARI = 6,
    RANDO_SPECIAL_KEY_SHOE_SHOP = 7,
    RANDO_SPECIAL_KEY_BOMB_MINISH_BAG = 8,
    RANDO_SPECIAL_KEY_BOMB_MINISH_REMOTES = 9,
    RANDO_SPECIAL_KEY_MINISH_GREAT_FAIRY = 10,
    RANDO_SPECIAL_KEY_CRENEL_GREAT_FAIRY = 11,
    RANDO_SPECIAL_KEY_VALLEY_GREAT_FAIRY = 12,
    RANDO_SPECIAL_KEY_DAMPE = 13,
    RANDO_SPECIAL_KEY_WITCH_HUT = 14,
    RANDO_SPECIAL_KEY_BIGGORON = 15,
    RANDO_SPECIAL_KEY_LIBRARY_YELLOW_MINISH = 16,
    RANDO_SPECIAL_KEY_DEEPWOOD_PRIZE = 17,
    RANDO_SPECIAL_KEY_COF_PRIZE = 18,
    RANDO_SPECIAL_KEY_DROPLETS_PRIZE = 19,
    RANDO_SPECIAL_KEY_PALACE_PRIZE = 20,
    RANDO_SPECIAL_KEY_CAFE_LADY = 21,
    RANDO_SPECIAL_KEY_CRYPT_PRIZE = 22,
};

enum {
    RANDO_SCRUB_KEY_BOTTLE = 0,
    RANDO_SCRUB_KEY_GRIP = 1,
};

/* Bind curated native runtime keys onto .logic locations that only carry
 * MinishMaker EU-ROM patch addresses. This includes:
 *   - ground items keyed by area-room-flag
 *   - scripted grant sites keyed in the high-bit runtime namespace below
 * Reparse clears bindings; this is rerun after every successful generation. */
void Rando_Keymap_Apply(void);

/* Scripted-grant runtime namespace. Vanilla chest / ground-item keys fit in
 * 24 bits; precise .logic ROM-address keys do too. Reserve bit 31 for native
 * location identities that do not correspond to an area-room-local triple. */
#define RANDO_SCRIPTED_KEY(group, a, b, c) \
    (0x80000000u | ((uint32_t)(group) << 24) | ((uint32_t)(a) << 16) | ((uint32_t)(b) << 8) | (uint32_t)(c))

static inline uint32_t Rando_BuildScriptedKey(uint8_t group, uint8_t a, uint8_t b, uint8_t c) {
    return RANDO_SCRIPTED_KEY(group, a, b, c);
}

#ifdef __cplusplus
}
#endif

#endif /* PORT_RANDO_KEYMAP_H */
