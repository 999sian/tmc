/*
 * port/port_reborn.h — runtime toggles for Minish Cap Reborn parity
 * features. Each cherry-picked Reborn enhancement (clean-room from the
 * upstream README description) checks its toggle at the call site, so
 * users can flip individual features on or off without rebuilding.
 *
 * Defaults: all ON (these are quality-of-life wins).
 *
 * Surface in F8 ribbon → "Reborn" tab.
 */

#ifndef PORT_REBORN_H
#define PORT_REBORN_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    REBORN_FEAT_SHELLS_9999,           /* shells cap raised 999 → 9999 */
    REBORN_FEAT_EQUIP_LR_BOOTS,        /* L + R → Pegasus Boots */
    REBORN_FEAT_EQUIP_LSELECT_OCARINA, /* L + SELECT → Ocarina */
    REBORN_FEAT_NO_EZLO_ON_RESUME,     /* skip Ezlo hint after file resume */
    REBORN_FEAT_LIBRARY_REOPEN,        /* Library accessible after Four Sword */
    REBORN_FEAT_FIGURINE_MIN_RAISED,   /* raise minimum figurine drop chance */
    REBORN_FEAT_SKIP_EZLO_TUTORIALS,   /* skip most early-game Ezlo tutorials */
    REBORN_FEAT_HERO_MODE,             /* damage doubled — toggleable Hero Mode */
    REBORN_FEAT_RUPEE_LIKE_OVERHAUL,   /* rupee-likes steal shield, time-out 10t */
    REBORN_FEAT_SECONDARY_LAB,         /* L+A / L+B → use SLOT_LA / SLOT_LB items */
    REBORN_FEAT_SELECT_HOLD_EQUIP,     /* SELECT-hold in pause menu → equip secondary slot */
    REBORN_FEAT_ANALOG_360_MOVEMENT,   /* 360° left-stick movement (32-step snap) */
    REBORN_FEAT_COUNT,
} RebornFeature;

bool Port_Reborn_IsEnabled(RebornFeature f);
void Port_Reborn_SetEnabled(RebornFeature f, bool on);

/* Human-readable label for a feature. Used by the F8 ribbon UI. */
const char* Port_Reborn_FeatureLabel(RebornFeature f);

/* Short description / what it does. Tooltip-style. */
const char* Port_Reborn_FeatureDescription(RebornFeature f);

/* Marks that a save-resume just happened. Set by Port_QuickLoad and
 * by the save-load entry path in src/save.c (PC_PORT only). The next
 * area-init that would have fired an Ezlo hint consumes this flag
 * and suppresses the hint when REBORN_FEAT_NO_EZLO_ON_RESUME is on. */
void Port_Reborn_NotifyJustResumed(void);
bool Port_Reborn_ConsumeJustResumed(void);

#ifdef __cplusplus
}
#endif

#endif /* PORT_REBORN_H */
