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

#ifdef __cplusplus
}
#endif

#endif /* PORT_DEBUG_ACTIONS_H */
