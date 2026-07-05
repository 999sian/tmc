#version 450
/*
 * GPU PPU rasterizer — fragment shader.
 *
 * Part of the The Minish Cap PC port — GPL-3.0-or-later.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Faithful integer reimplementation of port/ppu/src/mode1.c, one GBA pixel per
 * fragment. Storage buffers hold the GBA memory the CPU sequential pass has
 * already prepared (VRAM, palettes, OAM, per-line IO snapshots, affine refs);
 * the fragment computes the composited ABGR8888 pixel for (x, line).
 *
 * Binding order is by feature-onset so each implementation phase declares a
 * contiguous prefix of storage buffers (keeps num_storage_buffers in sync):
 *   set=2 binding=0  bg_palette   (Phase 1)
 *   set=2 binding=1  io_per_line  (Phase 2)
 *   set=2 binding=2  vram         (Phase 2)
 *   set=2 binding=3  obj_palette  (Phase 3)
 *   set=2 binding=4  oam          (Phase 3)
 *   set=2 binding=5  affine_ref   (Phase 5)
 *
 * PHASE 1 (current): forced-blank and backdrop only. Enough to prove the
 * upload → dispatch → readback path is byte-exact against the CPU rasterizer.
 */

layout(set = 2, binding = 0, std430) readonly buffer BgPalette { uint data[]; } bgpal;

layout(set = 3, binding = 0, std140) uniform Params {
    /* geom: x=frame_width, y=height, z=mode(GBA), w=affine(0/1) */
    ivec4 geom;
    /* misc: x=frame_dispcnt (frame-start DISPCNT; forced-blank + backdrop) */
    uvec4 misc;
} params;

layout(location = 0) out vec4 oColor;

/* GBA DISPCNT bit 7 = forced blank. */
const uint DISP_FORCED_BLANK = 0x0080u;

/* Read halfword `idx` (2-byte aligned) from a 32-bit-word storage buffer. */
uint bgpal_u16(int idx) {
    uint byteAddr = uint(idx) * 2u;
    uint w = bgpal.data[byteAddr >> 2u];
    uint sh = (byteAddr & 2u) * 8u; /* 0 or 16 */
    return (w >> sh) & 0xFFFFu;
}

/* rgb555 -> normalized rgba, matching virtuappu_mode1_rgb555_to_abgr8888:
 * r=(c&0x1F)<<3, g=((c>>5)&0x1F)<<3, b=((c>>10)&0x1F)<<3, a=0xFF.
 * The <<3 values are multiples of 8; f/255 then UNORM round-trips exactly. */
vec4 rgb555_to_rgba(uint c) {
    uint r = (c & 0x1Fu) << 3u;
    uint g = ((c >> 5u) & 0x1Fu) << 3u;
    uint b = ((c >> 10u) & 0x1Fu) << 3u;
    return vec4(float(r) / 255.0, float(g) / 255.0, float(b) / 255.0, 1.0);
}

void main() {
    /* Vulkan clip Y is +down and the fullscreen quad covers the target with no
     * flip, so gl_FragCoord maps 1:1 to the CPU framebuffer (row 0 = top). */
    int x = int(gl_FragCoord.x);
    /* int line = int(gl_FragCoord.y);  // used from Phase 2 on */

    uint dispcnt = params.misc.x;

    /* Forced blank: whole frame is white (CPU memsets 0xFF). */
    if ((dispcnt & DISP_FORCED_BLANK) != 0u) {
        oColor = vec4(1.0, 1.0, 1.0, 1.0);
        return;
    }

    /* PHASE 1: no BG/OBJ layers yet — every pixel resolves to the backdrop
     * (bg_palette[0]), exactly as the CPU composite's initial top_color. */
    oColor = rgb555_to_rgba(bgpal_u16(0));
}
