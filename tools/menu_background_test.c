/* cc -std=gnu11 -DPC_PORT -I. -Iinclude -Iport tools/menu_background_test.c port/port_menu_background.c -o /tmp/menu_background_test */
#include "main.h"
#include "pauseMenu.h"
#include "port_menu_background.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

Main gMain;
UI gUI;
PauseMenuOptions gPauseMenuOptions;
u8 gIoMem[0x400];
u8 gVram[0x18000];
u16 gBgPltt[256];

static void Write16(u8* bytes, unsigned offset, unsigned value) {
    bytes[offset] = value;
    bytes[offset + 1] = value >> 8;
}

int main(void) {
    gMain.task = TASK_FILE_SELECT;
    gMain.state = GAMETASK_INIT;
    Write16(gIoMem, 0, 0x0800);
    Write16(gIoMem, 0x0e, 0x1e0b);
    gBgPltt[0] = 0x03e0;
    gBgPltt[1] = 0x001f;
    gBgPltt[17] = 0x7c00;
    /* Distinct corners in tile 1 exercise nibble order and both flip bits. */
    gVram[0x8020] = 1;
    gVram[0x8023] = 0x10;
    gVram[0x803c] = 1;
    Write16(gVram, 0xf000, 1);
    const uint32_t* pixels = Port_MenuBackground_GetPixels();
    assert(pixels && pixels[0] == 0xff0000f8u && pixels[1] == 0xff00f800u);
    Write16(gVram, 0xf000, 1 | 0x1000);
    assert(Port_MenuBackground_GetPixels()[0] == 0xfff80000u);
    Write16(gVram, 0xf000, 1 | 0x400);
    assert(Port_MenuBackground_GetPixels()[0] == 0xff0000f8u);
    Write16(gVram, 0xf000, 1 | 0x800);
    assert(Port_MenuBackground_GetPixels()[0] == 0xff0000f8u);
    Write16(gVram, 0xf000, 1 | 0xc00);
    assert(Port_MenuBackground_GetPixels()[0] == 0xff00f800u);
    Write16(gVram, 0xf000, 1);
    Write16(gIoMem, 0x1c, 1);
    assert(Port_MenuBackground_GetPixels()[0] == 0xff00f800u);
    Write16(gIoMem, 0x1c, 0);
    Write16(gIoMem, 0x50, 0xc8);
    Write16(gIoMem, 0x54, 16);
    assert(Port_MenuBackground_GetPixels()[0] == 0xff000000u);
    assert(Port_MenuBackground_GetPixels()[1] == 0xff00f800u); /* Transparent tile pixels use backdrop targeting. */
    Write16(gIoMem, 0x50, 0x88);
    assert(Port_MenuBackground_GetPixels()[0] == 0xfff8f8f8u);
    Write16(gIoMem, 0x50, 0);
    Write16(gIoMem, 0x50, 0x48); /* Alpha-composited BG3 needs the original presenter. */
    assert(!Port_MenuBackground_GetPixels());
    Write16(gIoMem, 0x50, 0);
    Write16(gIoMem, 0, 0x0880);
    assert(!Port_MenuBackground_GetPixels());
    Write16(gIoMem, 0, 0x0800);
    Write16(gIoMem, 0x0e, 0x1e8b);
    assert(!Port_MenuBackground_GetPixels());
    Write16(gIoMem, 0x0e, 0x1e0b);
    gMain.task = TASK_GAME;
    assert(!Port_MenuBackground_GetPixels());
    gMain.state = GAMETASK_MAIN;
    gMain.substate = GAMEMAIN_SUBTASK;
    gUI.nextToLoad = 2;
    gUI.lastState = SUBTASK_PAUSEMENU;
    gPauseMenuOptions.unk11 = 2;
    gPauseMenuOptions.screen = PauseMenuScreen_1;
    assert(Port_MenuBackground_GetPixels());
    gPauseMenuOptions.screen = PauseMenuScreen_2;
    assert(Port_MenuBackground_GetPixels());
    gPauseMenuOptions.screen = PauseMenuScreen_7;
    assert(Port_MenuBackground_GetPixels());
    gPauseMenuOptions.screen = PauseMenuScreen_8;
    assert(Port_MenuBackground_GetPixels());
    gPauseMenuOptions.screen = PauseMenuScreen_4;
    assert(!Port_MenuBackground_GetPixels());
    gPauseMenuOptions.screen = PauseMenuScreen_1;
    gUI.lastState = SUBTASK_AUXCUTSCENE;
    assert(!Port_MenuBackground_GetPixels());
    puts("menu background: tile decoding, palette, fade, and scene gates passed");
    return 0;
}
