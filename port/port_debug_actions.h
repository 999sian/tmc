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

#ifdef __cplusplus
}
#endif

#endif /* PORT_DEBUG_ACTIONS_H */
