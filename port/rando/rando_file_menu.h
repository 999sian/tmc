#ifndef PORT_RANDO_FILE_MENU_H
#define PORT_RANDO_FILE_MENU_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Seed text capacity (excluding the NUL). The ImGui InputText writes into
 * Port_RandoFileMenu_SeedBuffer(), which is this + 1 bytes. */
#define RANDO_FILE_MENU_SEED_MAX 32

/* Lifecycle — driven by src/fileselect.c (STATE_RANDOMIZER_CONFIG). */
bool Port_RandoFileMenu_ShouldOpenForNewFile(void);
void Port_RandoFileMenu_Open(int save_slot);
void Port_RandoFileMenu_Close(void);
bool Port_RandoFileMenu_IsOpen(void);
bool Port_RandoFileMenu_GetRandoOptionEnabled(void);
void Port_RandoFileMenu_SetRandoOptionEnabled(bool enabled);

/* State accessors + mutation helpers. The modal itself is rendered by
 * port_imgui_menu.cpp (DrawRandoFileMenuModal) so it works on every
 * backend; this file owns the state machine and the commit logic. The
 * headless repro harness (port_repro_rando.c) calls the same helpers. */
bool Port_RandoFileMenu_LogicMode(void);
const char* Port_RandoFileMenu_Status(void);
char* Port_RandoFileMenu_SeedBuffer(void);
void Port_RandoFileMenu_SeedEdited(void);     /* resync after direct buffer edit */
void Port_RandoFileMenu_SetSeed(const char* text);
void Port_RandoFileMenu_RandomizeSeed(void);
bool Port_RandoFileMenu_IsSeedChar(char c);

/* Built-in graph mode (no .logic file loaded). */
bool* Port_RandoFileMenu_GlitchlessLogic(void);
bool* Port_RandoFileMenu_ShuffleKinstones(void);
bool* Port_RandoFileMenu_ShuffleDojos(void);
int Port_RandoFileMenu_Difficulty(void);
void Port_RandoFileMenu_SetDifficulty(int difficulty);

/* Logic mode (.logic settings → override + reparse). Index = the
 * RandoLogic_GetSetting() index. ChangeLogicSetting toggles flags /
 * steps dropdowns and numbers by `delta`; the Set* variants commit an
 * absolute value (ImGui combos and sliders report absolutes). */
void Port_RandoFileMenu_ChangeLogicSetting(int idx, int delta);
void Port_RandoFileMenu_SetLogicOption(int idx, int option_index);
void Port_RandoFileMenu_SetLogicNumber(int idx, int value);

/* Commit paths: generate + start the slot, or back out to file select. */
void Port_RandoFileMenu_CommitAndStart(void);
void Port_RandoFileMenu_Cancel(void);

#ifdef __cplusplus
}
#endif

#endif /* PORT_RANDO_FILE_MENU_H */
