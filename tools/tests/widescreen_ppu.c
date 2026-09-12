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
    /* Native fallback must not touch padding in the wider-pitch buffer. */
    p.frame_width = 240;
    for (int i = 0; i < MODE1_GBA_WIDTH * 160; i++) virtuappu_frame_buffer[i] = 0x12345678;
    virtuappu_mode1_render_frame(&p);
    expect("native fallback padding", virtuappu_frame_buffer[240], 0x12345678);
    if (!failures) puts("widescreen CPU PPU: PASS");
    return failures != 0;
}
