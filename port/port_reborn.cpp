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
