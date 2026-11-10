/* Expected-pixel regressions for the real CPU PPU; no ROM or SDL required. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "cpu/mode1.h"
#include "virtuappu.h"
uint32_t virtuappu_frame_buffer[VIRTUAPPU_FRAME_BUFFER_SIZE];
static uint8_t io[0x400], vram[0x18000];
static uint16_t bgpal[256], objpal[256], oam[512];
static int failures;
static void expect(const char* name, uint32_t got, uint32_t want) {
    if (got != want) {
        fprintf(stderr, "%s: got %08x, expected %08x\n", name, got, want);
        failures++;
    }
}
int main(void) {
    VirtuaPPUMode1GbaMemory mem = {io, vram, bgpal, objpal, oam};
    virtuappu_mode1_bind_gba_memory(&mem);
    for (int i = 0; i < 128; i++) oam[i * 4] = 0x200;
    io[1] = 0x10; /* OBJ enabled; BGs transparent. */
    bgpal[0] = 0x03e0;
    objpal[1] = 0x001f;
    memset(vram + 0x10000, 0x11, 32);
    oam[0] = 16; oam[1] = 236; oam[2] = 0;
    PPUMemory p = {0};
    p.mode = 1; p.frame_width = 284; p.frame_pitch = MODE1_GBA_WIDTH;
    virtuappu_mode1_render_frame(&p);
    expect("sprite left of seam", virtuappu_frame_buffer[16 * MODE1_GBA_WIDTH + 239], 0xff0000f8);
    expect("sprite right of seam", virtuappu_frame_buffer[16 * MODE1_GBA_WIDTH + 240], 0xff0000f8);
    expect("backdrop right of seam", virtuappu_frame_buffer[240], 0xff00f800);
    /* Window-hidden layers must still reveal the real backdrop. */
    io[1] |= 0x20; /* WIN0 enabled, zero window/outside controls. */
    virtuappu_mode1_render_frame(&p);
    expect("window masks sprite", virtuappu_frame_buffer[16 * MODE1_GBA_WIDTH + 240], 0xff00f800);
    /* WINxH X2 >= 240 means the screen edge, which is the wide frame's edge. */
    io[0x40] = 0xf0; io[0x41] = 0; /* WIN0 x 0..240 */
    io[0x44] = 160; io[0x45] = 0;  /* WIN0 y 0..160 */
    io[0x48] = 0x10;               /* OBJ inside WIN0 */
    virtuappu_mode1_render_frame(&p);
    expect("full-width window reaches frame edge", virtuappu_frame_buffer[16 * MODE1_GBA_WIDTH + 240], 0xff0000f8);
    /* Side channel: full edges published in unused I/O replace the 8-bit
     * WIN0H on wide frames. A circle centred past x=240 truncates its left
     * edge byte (310 -> 0x36); the published edges keep x=300 outside. */
    p.frame_width = 384;
    oam[1] = 300;
    io[0x40] = 0xf0; io[0x41] = 310 & 0xff;
    virtuappu_mode1_render_frame(&p);
    expect("truncated window edge without side channel", virtuappu_frame_buffer[16 * MODE1_GBA_WIDTH + 300], 0xff0000f8);
    io[0x56] = 310 & 0xff; io[0x57] = 310 >> 8; /* WIN0 left */
    io[0x58] = 340 & 0xff; io[0x59] = 340 >> 8; /* WIN0 right */
    io[0x5e] = 1;
    virtuappu_mode1_render_frame(&p);
    expect("wide window left edge", virtuappu_frame_buffer[16 * MODE1_GBA_WIDTH + 300], 0xff00f800);
    oam[1] = 330;
    virtuappu_mode1_render_frame(&p);
    expect("wide window interior", virtuappu_frame_buffer[16 * MODE1_GBA_WIDTH + 330], 0xff0000f8);
    p.frame_width = 240;
    oam[1] = 236;
    virtuappu_mode1_render_frame(&p);
    expect("native frame ignores side channel", virtuappu_frame_buffer[16 * MODE1_GBA_WIDTH + 236], 0xff0000f8);
    p.frame_width = 284;
    memset(io + 0x40, 0, 0x20);
    io[1] &= (uint8_t)~0x20;
    oam[1] = 236;
    /* Native fallback must not touch padding in the wider-pitch buffer. */
    p.frame_width = 240;
    for (int i = 0; i < MODE1_GBA_WIDTH * 160; i++) virtuappu_frame_buffer[i] = 0x12345678;
    virtuappu_mode1_render_frame(&p);
    expect("native fallback padding", virtuappu_frame_buffer[240], 0x12345678);
    /* Woods fog is a repeating screenblock, including scanline-varying HOFS.
     * Its shadow must represent every tile column, not just the camera reveal. */
    static uint16_t overlay[32 * MODE1_WS_SHADOW_COLS];
    memset(io, 0, sizeof(io));
    io[1] = 8; /* BG3 only. */
    io[0xe] = 4; io[0xf] = 0x1e; /* char base 1, screen base 30 */
    bgpal[1] = 0x001f; bgpal[2] = 0x03e0;
    memset(vram + 0x4020, 0x11, 32);
    memset(vram + 0x4040, 0x22, 32);
    uint16_t* screen = (uint16_t*)(vram + 0xf000);
    for (int row = 0; row < 32; ++row) {
        for (int col = 0; col < 32; ++col) {
            uint16_t entry = 1 + (col & 1);
            screen[row * 32 + col] = entry;
            if (col < MODE1_WS_SHADOW_COLS)
                overlay[row * MODE1_WS_SHADOW_COLS + col] = entry;
        }
    }
    virtuappu_mode1_ws_shadow[3] = overlay;
    virtuappu_mode1_ws_shadow_base_tile[3] = 0;
    p.frame_width = 384;
    for (int offset = 0; offset < 256; offset += 7) {
        io[0x1c] = offset;
        virtuappu_mode1_render_frame(&p);
        for (int x = 240; x < 384; ++x) {
            uint32_t color = (((x + offset) / 8) & 1) ? 0xff00f800 : 0xff0000f8;
            expect("repeating fog past native edge", virtuappu_frame_buffer[x], color);
            if (failures) break;
        }
        if (failures) break;
    }
    if (!failures) puts("widescreen CPU PPU: PASS");
    return failures != 0;
}
