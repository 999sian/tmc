/* Link against src/message.c with -ffunction-sections and --gc-sections. */
#include "message.h"
#include "screen.h"
#include <assert.h>
#include <string.h>

TextRender gTextRender;
Screen gScreen;
u8 gEwram[0x40000], gVram[0x18000];
u8 gIwram[0x8000], gIoMem[0x400];
u16 gBgPltt[256], gObjPltt[256], gOamMem[0x200];
u8* gRomData;
u32 gRomSize;
void Port_LogRomAccess(u32 address, const char* caller) { (void)address; (void)caller; }
extern struct { u8 unk0, active, unk2, unk3, xPos, yPos, width, height; } gCurrentWindow;

int main(void) {
    u8 pixels[208 * 32];
    int x, y, width, height;
    gCurrentWindow.active = 1;
    gCurrentWindow.xPos = 1;
    gCurrentWindow.yPos = 14;
    gCurrentWindow.width = 26;
    gCurrentWindow.height = 4;
    gTextRender._98.bytes.sizeScale = 8;
    for (int row = 15; row < 19; ++row)
        for (int col = 2; col < 28; ++col)
            gBG0Buffer[row * 32 + col] = 0xf082;
    memset(gVram, 0xff, sizeof(gVram));
    gVram[0xd040] = 0x21;
    assert(Message_CopyTextCanvas(pixels, sizeof(pixels), &x, &y, &width, &height));
    assert(x == 16 && y == 120 && width == 208 && height == 32);
    assert(pixels[0] == 1 && pixels[1] == 2 && pixels[2] == 15);
    assert(!Message_CopyTextCanvas(pixels, 1, &x, &y, &width, &height));
    gScreen.bg0.xOffset = 1;
    assert(!Message_CopyTextCanvas(pixels, sizeof(pixels), &x, &y, &width, &height));
    gScreen.bg0.xOffset = 0;
    gTextRender._98.bytes.sizeScale = 7; // Opening/closing animations retain original text.
    assert(!Message_CopyTextCanvas(pixels, sizeof(pixels), &x, &y, &width, &height));
    gTextRender._98.bytes.sizeScale = 8;
    gBG0Buffer[15 * 32 + 2] = 0xf080; // Cursor/non-dialogue content must survive.
    assert(!Message_CopyTextCanvas(pixels, sizeof(pixels), &x, &y, &width, &height));
    return 0;
}
