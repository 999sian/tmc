
#include "common.h"
#include "fade.h"
#include "main.h"
#include "message.h"
#include "screen.h"
#include "sound.h"
#include "structures.h"

// Data globals
GfxSlotList gGFXSlots;
Message gMessage;
TextRender gTextRender;
u16 gBG0Buffer[0x400];
u16 gPaletteBuffer[0x200];
Input gInput;
u32 gRand;
Screen gScreen;
OAMCommand gOamCmd;
Main gMain;
FadeControl gFadeControl;
OAMControls gOAMControls;
SoundPlayingInfo gSoundPlayingInfo;

u32 gUsedPalettes;

// Pointers
struct_02000010 gUnk_02000010;
u32 gUnk_02000030 = 0x02000030;
struct_02000040 gUnk_02000040;
u32 gUnk_020000B0 = 0x020000B0;
struct_gUnk_020000C0 gUnk_020000C0[0x30];
u32 gUnk_02001A3C = 0x02001A3C;
u32 gUnk_02006F00 = 0x02006F00;
u32 gUnk_0200B640 = 0x0200B640;
u32 gUnk_02017830 = 0x02017830;
u32 gUnk_02017AA0 = 0x02017AA0;
u32 gUnk_02017BA0 = 0x02017BA0;
u32 gUnk_02018EA0 = 0x02018EA0;
struct_02018EB0 gUnk_02018EB0;
u32 gUnk_02018EE0 = 0x02018EE0;
u32 gUnk_02021F00 = 0x02021F00;
u32 gUnk_020227DC = 0x020227DC;
u32 gUnk_020227E8 = 0x020227E8;
u32 gUnk_020227F0 = 0x020227F0;
u32 gUnk_020227F8 = 0x020227F8;
u32 gUnk_02022800 = 0x02022800;
u32 gUnk_02022830 = 0x02022830;
u32 gUnk_020246B0 = 0x020246B0;
u32 gUnk_02033290 = 0x02033290;
u32 gUnk_020342F8 = 0x020342F8;
u32 gUnk_02034330 = 0x02034330;
struct_02034480 gUnk_02034480;
u32 gUnk_02034492 = 0x02034492;
u32 gUnk_020344A0 = 0x020344A0;
struct_020354C0 gUnk_020354C0[32];
u32 gUnk_02035542 = 0x02035542;
u32 gUnk_02036540 = 0x02036540;
u32 gUnk_02036A58 = 0x02036A58;
u32 gUnk_02036AD8 = 0x02036AD8;
u32 gUnk_02036BB8 = 0x02036BB8;
