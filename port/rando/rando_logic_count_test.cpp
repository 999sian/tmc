#include "rando/rando_logic.h"
#include "item_ids.h"

#include <cassert>
#include <cstring>

static bool sHasBombs;
static uint16_t sHeldKeys;

static uint16_t ItemCount(const char* name) {
    if (std::strcmp(name, "Items.SmallKey.0x19") == 0) return sHeldKeys;
    return sHasBombs && std::strcmp(name, "Items.Bombs") == 0 ? 1 : 0;
}

int main() {
    const char* logic =
        "First; Unshuffled; 0x00-0x00-0x01;; Items.SmallKey.0x19\n"
        "Second; Unshuffled; 0x00-0x00-0x02; Items.Bombs; Items.SmallKey.0x19\n"
        "Gate; Any; 0x00-0x00-0x03; (+2, Items.SmallKey.0x19)\n";
    assert(RandoLogic_LoadText(logic, std::strlen(logic)));

    const uint16_t items[] = {ITEM_SMALL_KEY, ITEM_SMALL_KEY, ITEM_NONE};
    const uint8_t subtypes[] = {0x82, 0x82, 0};
    bool reached[3];

    RandoLogic_EvaluateReachability(items, subtypes, 3, 1, ItemCount, reached, 3);
    assert(reached[0] && !reached[1] && !reached[2]);

    sHasBombs = true;
    RandoLogic_EvaluateReachability(items, subtypes, 3, 1, ItemCount, reached, 3);
    assert(reached[0] && reached[1] && reached[2]);

    /* Inventory can contain multiple copies of the same logic symbol. */
    const char* count_logic = "CountGate; Any; 0x00-0x00-0x04; (+2, Items.SmallKey.0x19)\n";
    assert(RandoLogic_LoadText(count_logic, std::strlen(count_logic)));
    const uint16_t no_item[] = {ITEM_NONE};
    const uint8_t no_subtype[] = {0};
    sHeldKeys = 1;
    RandoLogic_EvaluateReachability(no_item, no_subtype, 1, 1, ItemCount, reached, 1);
    assert(!reached[0]);
    sHeldKeys = 2;
    RandoLogic_EvaluateReachability(no_item, no_subtype, 1, 1, ItemCount, reached, 1);
    assert(reached[0]);

    /* An unfilled Dungeon slot joins the Any pool before Minor placement.
     * Plentiful + Keysanity needs this fallback for its extra small keys. */
    const char* fallback_logic =
        "Items.SmallKey.0x18; Minor\n"
        "DungeonOnly; Dungeon; 0x00-0x00-0x05\n";
    assert(RandoLogic_LoadText(fallback_logic, std::strlen(fallback_logic)));
    uint16_t dungeon_award[1] = {ITEM_NONE};
    assert(RandoLogic_Generate(1, nullptr, dungeon_award, 1, nullptr) == RANDO_OK);
    assert(dungeon_award[0] == ITEM_SMALL_KEY);
    assert(RandoLogic_GetGeneratedItemSubtype(0) == 0x81);
}
