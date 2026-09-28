#version 450
// Voxel view (port_voxel.cpp): decodes GBA pixels straight from live VRAM,
// palette RAM and the room sub-tile maps, so every quad is a window onto the
// real game data (palette fades, tile animation, SetTile all just work).
//
// vParams.x = kind:
//   0 room map layer   uv = room pixel;   y = map row offset (0 bottom, 128 top),
//                                          z = char base, w = 1 if 8bpp
//   1 OBJ sprite       uv = sprite pixel; y = base tile, z = palette bank,
//                                          w = tiles per sprite row | 8bpp << 8 |
//                                              (width/8 - 1) << 9 | (height/8 - 1) << 12
//   2 screen layer     uv = screen pixel; y = row offset into uBg0 (0: BG0 text/HUD,
//                                          160: BG3 backdrop), both PPU-rendered on the CPU

layout(location = 0) in vec2 vUv;
layout(location = 1) flat in uvec4 vParams;
layout(location = 0) out vec4 oColor;

layout(set = 2, binding = 0) uniform usampler2D uVram; // 256 x 384 R8_UINT (96 KB VRAM)
layout(set = 2, binding = 1) uniform usampler2D uMaps; // 128 x 256 R16_UINT (bottom rows 0-127, top 128-255)
layout(set = 2, binding = 2) uniform sampler2D uPal;   // 512 x 1 RGBA8: BG 0-255, OBJ 256-511
layout(set = 2, binding = 3) uniform sampler2D uBg0;   // view width x 160 RGBA8, alpha 0 = transparent
layout(set = 2, binding = 4) uniform sampler2D uMask;  // 256 x 256 R8: prop cutout masks, 16x16 slots

uint vram8(uint a) {
    return texelFetch(uVram, ivec2(int(a & 255u), int((a >> 8) % 384u)), 0).r;
}

// Palette index of one texel of a BG map entry; 0 = transparent.
uint bgTexel(uint entry, uint charBase, bool bpp8, uint px, uint py) {
    uint tile = entry & 0x3FFu;
    if ((entry & 0x400u) != 0u) px = 7u - px;
    if ((entry & 0x800u) != 0u) py = 7u - py;
    if (bpp8)
        return vram8(charBase + tile * 64u + py * 8u + px);
    uint b = vram8(charBase + tile * 32u + py * 4u + (px >> 1));
    uint ci = (px & 1u) != 0u ? (b >> 4) : (b & 15u);
    return ci == 0u ? 0u : (entry >> 12) * 16u + ci;
}

void main() {
    ivec2 p = ivec2(floor(vUv));
    uint idx;
    if (vParams.x == 0u) {
        if (p.x < 0 || p.y < 0 || p.x >= 1024 || p.y >= 1024)
            discard;
        uint entry = texelFetch(uMaps, ivec2(p.x >> 3, (p.y >> 3) + int(vParams.y)), 0).r;
        // w: bit0 8bpp, bit1 fill-transparent, bits 8-16 prop mask slot+1, bits 20-27 fill palette index
        uint mslot = (vParams.w >> 8) & 511u;
        if (mslot != 0u) {
            mslot -= 1u;
            ivec2 mp = ivec2(int(mslot % 16u) * 16 + (p.x & 15), int(mslot / 16u) * 16 + (p.y & 15));
            if (texelFetch(uMask, mp, 0).r < 0.5)
                discard;
        }
        idx = bgTexel(entry, vParams.z, (vParams.w & 1u) != 0u, uint(p.x) & 7u, uint(p.y) & 7u);
        if (idx == 0u && (vParams.w & 2u) != 0u)
            idx = (vParams.w >> 20) & 255u;
    } else if (vParams.x == 1u) {
        bool b8 = (vParams.w & 256u) != 0u;
        // affine sprites map box points outside the sprite: clip like the PPU
        ivec2 ssz = ivec2(int((vParams.w >> 9) & 7u) + 1, int((vParams.w >> 12) & 7u) + 1) * 8;
        if (p.x < 0 || p.y < 0 || p.x >= ssz.x || p.y >= ssz.y)
            discard;
        uint px = uint(p.x) & 7u, py = uint(p.y) & 7u;
        uint tile = vParams.y + (uint(p.y) >> 3) * (vParams.w & 255u) + (uint(p.x) >> 3) * (b8 ? 2u : 1u);
        uint base = 0x10000u + (tile & 1023u) * 32u;
        uint ci;
        if (b8) {
            ci = vram8(base + py * 8u + px);
        } else {
            uint b = vram8(base + py * 4u + (px >> 1));
            ci = (px & 1u) != 0u ? (b >> 4) : (b & 15u);
        }
        if (ci == 0u)
            discard;
        idx = 256u + (b8 ? ci : vParams.z * 16u + ci);
    } else {
        vec4 c = texelFetch(uBg0, ivec2(p.x, p.y + int(vParams.y)), 0);
        if (c.a == 0.0)
            discard;
        oColor = vec4(c.rgb, 1.0);
        return;
    }
    if (idx == 0u)
        discard;
    oColor = vec4(texelFetch(uPal, ivec2(int(idx), 0), 0).rgb, 1.0);
}
