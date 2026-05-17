/*
 * port/port_reborn.cpp — implementation of the Reborn-parity feature
 * toggle store. Process-local; resets to defaults on each launch.
 * Persistence to the on-disk config can be wired later.
 */

#include "port_reborn.h"

namespace {

struct {
    bool enabled;
    const char* label;
    const char* description;
} sFeatures[REBORN_FEAT_COUNT] = {
    /* REBORN_FEAT_SHELLS_9999 */
    {
        true,
        "Shells cap 9999",
        "Raise the Mysterious Shells cap from 999 to 9999.",
    },
    /* REBORN_FEAT_EQUIP_LR_BOOTS */
    {
        true,
        "L + R → Pegasus Boots",
        "Press R while holding L to instantly equip Pegasus Boots to slot A.",
    },
    /* REBORN_FEAT_EQUIP_LSELECT_OCARINA */
    {
        true,
        "L + SELECT → Ocarina",
        "Press SELECT while holding L to instantly equip the Ocarina to slot A.",
    },
    /* REBORN_FEAT_NO_EZLO_ON_RESUME */
    {
        true,
        "Skip Ezlo hint on resume",
        "Suppress the area Ezlo hint that fires the first time you re-enter "
        "the room after loading a save.",
    },
    /* REBORN_FEAT_LIBRARY_REOPEN */
    {
        true,
        "Library stays open after Four Sword",
        "Keep the Royal Hyrule Library accessible after obtaining the Four "
        "Sword. Vanilla closes it permanently.",
    },
    /* REBORN_FEAT_FIGURINE_MIN_RAISED */
    {
        true,
        "Raise minimum figurine chance",
        "Increase the floor on figurine-machine drop probability so "
        "duplicates take fewer attempts to clear.",
    },
    /* REBORN_FEAT_SKIP_EZLO_TUTORIALS */
    {
        false,
        "Skip Ezlo tutorial pop-ups",
        "Suppress the early-game Ezlo tutorial hints (movement, signs, "
        "talking, etc.). Doesn't affect plot-critical dialogue.",
    },
    /* REBORN_FEAT_HERO_MODE */
    {
        false,
        "Hero Mode (2× damage)",
        "Enemies and hazards deal double damage. Toggleable mid-run; takes "
        "effect from the next hit. (No save-format change.)",
    },
};

static bool sJustResumed = false;

} /* namespace */

extern "C" void Port_Reborn_NotifyJustResumed(void) {
    sJustResumed = true;
}

extern "C" bool Port_Reborn_ConsumeJustResumed(void) {
    bool v = sJustResumed;
    sJustResumed = false;
    return v;
}


extern "C" bool Port_Reborn_IsEnabled(RebornFeature f) {
    if ((unsigned)f >= REBORN_FEAT_COUNT) return false;
    return sFeatures[f].enabled;
}

extern "C" void Port_Reborn_SetEnabled(RebornFeature f, bool on) {
    if ((unsigned)f >= REBORN_FEAT_COUNT) return;
    sFeatures[f].enabled = on;
}

extern "C" const char* Port_Reborn_FeatureLabel(RebornFeature f) {
    if ((unsigned)f >= REBORN_FEAT_COUNT) return "";
    return sFeatures[f].label;
}

extern "C" const char* Port_Reborn_FeatureDescription(RebornFeature f) {
    if ((unsigned)f >= REBORN_FEAT_COUNT) return "";
    return sFeatures[f].description;
}
