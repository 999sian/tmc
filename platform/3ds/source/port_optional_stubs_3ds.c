#include "port_audio_mute.h"
#include "port_reborn.h"
#include "port_tts.h"
#include "rando/rando_entrance.h"
#include "rando/rando_file_menu.h"
#include "rando/rando_runtime.h"
#include "rando/rando_save.h"

#include <stdbool.h>
#include <stdint.h>

bool Port_Reborn_IsEnabled(RebornFeature feature) { (void)feature; return false; }
void Port_Reborn_SetEnabled(RebornFeature feature, bool enabled) { (void)feature; (void)enabled; }
unsigned Port_Reborn_GetMask(void) { return 0; }
void Port_Reborn_ApplyMask(unsigned mask) { (void)mask; }
void Port_Reborn_NotifyJustResumed(void) {}
bool Port_Reborn_ConsumeJustResumed(void) { return false; }

void Port_TTS_Speak(const char* text, const PortTtsOptions* options) { (void)text; (void)options; }
void Port_TTS_Stop(void) {}
bool Port_TTS_GetEnabled(void) { return false; }

bool Port_AudioMute_ShouldSuppress(unsigned int soundRequest) { (void)soundRequest; return false; }
int Port_Debug_NoclipEnabled(void) { return 0; }

bool Port_RandoFileMenu_ShouldOpenForNewFile(void) { return false; }
void Port_RandoFileMenu_Open(int saveSlot) { (void)saveSlot; }
bool Port_RandoFileMenu_IsOpen(void) { return false; }
void Port_RandoFileMenu_ToggleSidebar(void) {}

bool Port_RandoSave_SaveActiveSlot(int slot) { (void)slot; return true; }
bool Port_RandoSave_LoadSlot(int slot) { (void)slot; return false; }
void Port_RandoSave_ClearSlot(int slot) { (void)slot; }
void Port_RandoSave_CopySlot(int source, int destination) { (void)source; (void)destination; }

void Rando_Reset(void) {}
bool Rando_IsActive(void) { return false; }
void Rando_Runtime_OnNewFile(void) {}
void Rando_Runtime_Refresh(void) {}
int Rando_Runtime_DamageMultiplier(void) { return 1; }
bool Rando_Runtime_OpenTingleBrothers(void) { return false; }
bool Rando_Homewarp_Request(void) { return false; }
bool Rando_RouteDungeonItem(unsigned item, unsigned subtype) { (void)item; (void)subtype; return false; }
int Rando_Music_Remap(int area, int song) { (void)area; return song; }
bool Rando_OverrideItem(uint8_t* type, uint8_t* subtype) { (void)type; (void)subtype; return false; }
bool Rando_OverrideLocationKey(uint32_t key, uint8_t* type, uint8_t* subtype) {
    (void)key; (void)type; (void)subtype; return false;
}
bool RandoLogic_IsLoaded(void) { return false; }
int RandoLogic_FindLocationByKey(uint32_t key) { (void)key; return -1; }

void Rando_Entrance_RemapExit(uint8_t currentArea, uint8_t* area, uint8_t* room, int16_t* x, int16_t* y,
                              uint8_t* layer, uint8_t* spawnType, uint8_t* facing) {
    (void)currentArea; (void)area; (void)room; (void)x; (void)y; (void)layer; (void)spawnType; (void)facing;
}
void Rando_Entrance_RemapHole(uint8_t currentArea, uint8_t* area, uint8_t* room, uint8_t* layer,
                              int16_t* x, int16_t* y) {
    (void)currentArea; (void)area; (void)room; (void)layer; (void)x; (void)y;
}
void Rando_Entrance_RemapGreenWarp(uint8_t currentArea, uint32_t warpType, uint8_t* area, uint8_t* room,
                                   int16_t* x, int16_t* y) {
    (void)currentArea; (void)warpType; (void)area; (void)room; (void)x; (void)y;
}

int Port_QuickSave_AutoEnabled(void) { return 0; }
void Port_QuickSave_SetAutoEnabled(int enabled) { (void)enabled; }
