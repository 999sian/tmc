#include "port_menu_background.h"

#include "main.h"
#include "pauseMenu.h"
#include "port_gba_mem.h"

static uint16_t ReadIo(unsigned offset) {
    return gIoMem[offset] | ((uint16_t)gIoMem[offset + 1] << 8);
}

const uint32_t* Port_MenuBackground_GetPixels(void) {
    static uint32_t pixels[256 * 160];
    uint32_t palette[256];
    const uint16_t display = ReadIo(0);
    const uint16_t control = ReadIo(0x0e);
    const uint16_t blend = ReadIo(0x50);
    unsigned brightness = ReadIo(0x54) & 31;
    const unsigned effect = (blend >> 6) & 3;
    int supported = gMain.task == TASK_FILE_SELECT && gMain.state != 0;

    if (gMain.task == TASK_GAME && gMain.state == GAMETASK_MAIN && gMain.substate == GAMEMAIN_SUBTASK &&
        gUI.nextToLoad == 2 && gUI.lastState == SUBTASK_PAUSEMENU &&
        gPauseMenuOptions.unk11 > 0 && gPauseMenuOptions.unk11 < 6) {
        switch (gPauseMenuOptions.screen) {
            case PauseMenuScreen_1:
            case PauseMenuScreen_2:
            case PauseMenuScreen_7:
            case PauseMenuScreen_8:
                supported = 1;
                break;
        }
    }
    /* These menus share a 4bpp, 256x256 decorative BG3 at charblock 2/screenblock 30. */
    if (!supported || (display & 0x0887) != 0x0800 || control != 0x1e0b ||
        (effect == 1 && (blend & (1 << 3))))
        return NULL;

    if (brightness > 16)
        brightness = 16;
    for (unsigned i = 0; i < 256; ++i) {
        unsigned channels[3] = { gBgPltt[i] & 31, (gBgPltt[i] >> 5) & 31, (gBgPltt[i] >> 10) & 31 };
        for (unsigned c = 0; c < 3; ++c) {
            if (blend & (1 << (i == 0 ? 5 : 3))) {
                if (effect == 2)
                    channels[c] += ((31 - channels[c]) * brightness) >> 4;
                else if (effect == 3)
                    channels[c] -= (channels[c] * brightness) >> 4;
            }
        }
        palette[i] = 0xff000000u | (channels[0] << 3) | (channels[1] << 11) | (channels[2] << 19);
    }

    const unsigned scrollX = ReadIo(0x1c) & 255;
    const unsigned scrollY = ReadIo(0x1e) & 255;
    for (unsigned y = 0; y < 160; ++y) {
        const unsigned by = (y + scrollY) & 255;
        for (unsigned x = 0; x < 256; ++x) {
            const unsigned bx = (x + scrollX) & 255;
            const unsigned map = 0xf000 + (((by >> 3) * 32 + (bx >> 3)) * 2);
            const uint16_t tile = gVram[map] | ((uint16_t)gVram[map + 1] << 8);
            const unsigned tx = (bx & 7) ^ ((tile & 0x400) ? 7 : 0);
            const unsigned ty = (by & 7) ^ ((tile & 0x800) ? 7 : 0);
            const unsigned address = 0x8000 + (tile & 0x3ff) * 32 + ty * 4 + tx / 2;
            /* GBA text BG tiles may address OBJ VRAM; the hardware treats those pixels as transparent. */
            unsigned index = address < 0x10000 ? ((gVram[address] >> ((tx & 1) * 4)) & 15) : 0;
            if (index != 0)
                index += (tile >> 12) * 16;
            pixels[y * 256 + x] = palette[index];
        }
    }
    return pixels;
}
