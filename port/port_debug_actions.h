#ifndef PORT_DEBUG_ACTIONS_H
#define PORT_DEBUG_ACTIONS_H

#ifdef __cplusplus
extern "C" {
#endif

void Port_DebugAction_GiveAllItems(void);
void Port_DebugAction_MaxHearts(void);
void Port_DebugAction_HealFull(void);
void Port_DebugAction_MaxRupees(void);
void Port_DebugAction_MaxShells(void);
void Port_DebugAction_AllKinstones(void);
/* Figurine completion. The 130 variant grants all pre-credits figurines without
 * touching game-clear state; the 100 variant grants true 100% (136) but also
 * marks the game beaten (saw_staffroll), the only self-consistent full state. */
void Port_DebugAction_AllFigurines130(void);
void Port_DebugAction_AllFigurines100(void);

int Port_DebugAction_Warp(unsigned char area, unsigned char room,
                          unsigned short x, unsigned short y,
                          unsigned char layer);

int Port_DebugAction_WarpSpawnOverride(unsigned char area, unsigned char room,
                                       unsigned short* x, unsigned short* y,
                                       unsigned char* layer);

void Port_DebugAction_ArmWarpNudge(void);
void Port_DebugAction_WarpTick(void);

/* Free-coordinate teleport within the current room. TeleportXY drops Link at
 * world pixel (x, y); returns 1 on success, 0 if not in live gameplay.
 * PlayerXY reads Link's current position (returns 0 when not in gameplay). */
int Port_DebugAction_TeleportXY(unsigned short x, unsigned short y);
int Port_DebugQuery_PlayerXY(unsigned short* x, unsigned short* y);

/* Per-item ownership toggles. The table is enumerated by index 0..count-1;
 * each item exposes a name + group (section label). SetToggleItem enforces
 * pause-grid slot exclusivity, equip-clear, and held-item gfx reload. */
int         Port_DebugQuery_ToggleItemCount(void);
const char* Port_DebugQuery_ToggleItemName(int i);
const char* Port_DebugQuery_ToggleItemGroup(int i);
int         Port_DebugQuery_ToggleItemOwned(int i);
void        Port_DebugAction_SetToggleItem(int i, int owned);

/* Per-dungeon items/keys, dungeon id 0..15. which: 0=Map, 1=Compass,
 * 2=Big Key. CurrentDungeon returns the active dungeon id or -1 when the
 * player is not in a keyed dungeon. */
int  Port_DebugQuery_CurrentDungeon(void);
int  Port_DebugQuery_DungeonItems(int dungeon);
int  Port_DebugQuery_DungeonKeys(int dungeon);
void Port_DebugAction_SetDungeonItem(int dungeon, int which, int owned);
void Port_DebugAction_SetDungeonKeys(int dungeon, int count);

/* Charm / Picolyte timed buffs. id 0 = off; timer in frames at 60fps
 * (charm normal 3600, picolyte normal 900). Type ids are validated. The
 * query helpers return 1 when the effect is active. */
void Port_DebugAction_SetCharm(int charmId, int timer);
void Port_DebugAction_SetPicolyte(int picoId, int timer);
int  Port_DebugQuery_Charm(int* id, int* timer);
int  Port_DebugQuery_Picolyte(int* id, int* timer);

/* Adjustable bottle contents. Bottle 0..3. The content list is enumerated by
 * index 0..ContentCount-1 (id + name); ContentIndex maps a content id back to
 * its list index. SetBottleContent also owns the bottle. */
int         Port_DebugQuery_BottleContentCount(void);
const char* Port_DebugQuery_BottleContentName(int i);
int         Port_DebugQuery_BottleContentId(int i);
int         Port_DebugQuery_BottleContentIndex(int contentId);
int         Port_DebugQuery_BottleOwned(int bottle);
int         Port_DebugQuery_BottleContent(int bottle);
void        Port_DebugAction_SetBottleContent(int bottle, int contentId);

/* Numeric stat / capacity sliders, enumerated by index 0..StatCount-1. Min/Max
 * are computed live (counts clamp to the capacity tier), so the UI is a plain
 * slider; SetStat clamps into [Min,Max]. */
int         Port_DebugQuery_StatCount(void);
const char* Port_DebugQuery_StatName(int i);
int         Port_DebugQuery_StatMin(int i);
int         Port_DebugQuery_StatMax(int i);
int         Port_DebugQuery_StatValue(int i);
void        Port_DebugAction_SetStat(int i, int value);

/* Raw flag browser. Flags are addressed (bank, index): bank 0 = global,
 * 1..12 = local-flag pools. FlagBankSize gives the index count for a bank;
 * CurrentFlagBank is the bank the current area's local flags live in (-1 if
 * none). Flag reads/SetFlag toggles a single bit in gSave.flags. */
int          Port_DebugQuery_FlagBankCount(void);
const char*  Port_DebugQuery_FlagBankName(int bank);
unsigned int Port_DebugQuery_FlagBankOffset(int bank);
int          Port_DebugQuery_FlagBankSize(int bank);
int          Port_DebugQuery_CurrentFlagBank(void);
int          Port_DebugQuery_Flag(int bank, int index);
void         Port_DebugAction_SetFlag(int bank, int index, int on);

#ifdef __cplusplus
}
#endif

#endif /* PORT_DEBUG_ACTIONS_H */
