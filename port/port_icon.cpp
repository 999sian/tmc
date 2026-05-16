#include "port_icon.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>

/* Forward-declared C structs — we can't transitively include the project
 * headers because the C decomp uses `this` as a parameter name, which
 * C++ rejects as a reserved word. Layouts must stay in sync with
 * include/structures.h. PORT_STATIC_ASSERT_SIZE on the C side guards
 * any drift. */
extern "C" {

struct SpriteFrame {
    uint8_t  numTiles;
    uint8_t  unk_1;
    uint16_t firstTileIndex;
};

struct SpritePtr {
    void*         animations;
    SpriteFrame*  frames;
    void*         ptr;
    uint32_t      pad;
};

const SpritePtr* Port_GetSpritePtr(uint16_t sprite_idx);
extern uint32_t  gFrameObjLists[];
extern uint8_t*  gRomData;

}  // extern "C"

namespace {

constexpr uint16_t kSpriteEzloCap = 11; /* SPRITE_EZLOCAP from include/definitions.h */

struct Rgba { uint8_t r, g, b, a; };

constexpr Rgba kBgTransparent {0,   0,   0,   0};

/* ------------------------------------------------------------------- */
/* Placeholder green-hat silhouette                                    */
/* ------------------------------------------------------------------- */

constexpr int kPlaceholderW = 32;
constexpr int kPlaceholderH = 32;

constexpr Rgba kHatGreen     {66,  148, 64,  255};
constexpr Rgba kHatGreenDark {38,  98,  40,  255};
constexpr Rgba kBeakYellow   {248, 200, 64,  255};
constexpr Rgba kEyeBlack     {16,  16,  16,  255};
constexpr Rgba kCheekPink    {248, 152, 152, 255};

const char* kPlaceholderBitmap[kPlaceholderH] = {
    "................................",
    "................................",
    "..............DD................",
    ".............DGGD...............",
    "............DGGGGD..............",
    "...........DGGGGGGD.............",
    "..........DGGGGGGGGD............",
    ".........DGGGGGGGGGGD...........",
    "........DGGGGGGGGGGGGD..........",
    ".......DGGGGGGGGGGGGGGD.........",
    "......DGGGGGGGGGGGGGGGGD........",
    "......DGGGGGGGGGGGGGGGGD........",
    ".....DGGGGGGGGGGGGGGGGGGD.......",
    ".....DGGGGGGGGGGGGGGGGGGD.......",
    "....DGGGGGGGGGGGGGGGGGGGGD......",
    "....DGGGGGGGGGGGGGGGGGGGGD......",
    "....DGGGKGGGGGGGGGGGKGGGGD......",
    "....DGGGGGGGGGGGGGGGGGGGGD......",
    "....DGGGGGGGYYYYGGGGGGGGGD......",
    "....DGGGGGGYYYYYYGGGGGGGGD......",
    "....DGGGGGGGYYYYGGGGGGGGGD......",
    "....DGGPGGGGGGGGGGGGGGPGGD......",
    ".....DGGGGGGGGGGGGGGGGGGD.......",
    "......DGGGGGGGGGGGGGGGGD........",
    ".......DDGGGGGGGGGGGGDD.........",
    "........DDDDGGGGGGDDDD..........",
    "...........DDDDDDDD.............",
    "................................",
    "................................",
    "................................",
    "................................",
    "................................",
};

const Rgba& PlaceholderColor(char ch) {
    switch (ch) {
        case 'G': return kHatGreen;
        case 'D': return kHatGreenDark;
        case 'Y': return kBeakYellow;
        case 'K': return kEyeBlack;
        case 'P': return kCheekPink;
        default:  return kBgTransparent;
    }
}

SDL_Surface* MakePlaceholderIcon() {
    SDL_Surface* surf = SDL_CreateSurface(kPlaceholderW, kPlaceholderH, SDL_PIXELFORMAT_RGBA32);
    if (!surf) return nullptr;
    uint8_t* pixels = static_cast<uint8_t*>(surf->pixels);
    const int pitch = surf->pitch;
    for (int y = 0; y < kPlaceholderH; ++y) {
        Rgba* row = reinterpret_cast<Rgba*>(pixels + y * pitch);
        for (int x = 0; x < kPlaceholderW; ++x) {
            row[x] = PlaceholderColor(kPlaceholderBitmap[y][x]);
        }
    }
    return surf;
}

/* ------------------------------------------------------------------- */
/* Real ROM-extraction path                                            */
/* ------------------------------------------------------------------- */

/* Approximation of Ezlo's cap palette (alpha + 15 colors). Idx 0 is the
 * sprite's transparent index. Order chosen by eye to match the in-game
 * green/yellow Ezlo-cap silhouette; the real palette can be swapped in
 * later via gPaletteGroups[][] resolution. */
constexpr Rgba kEzloApproxPalette[16] = {
    {  0,   0,   0,   0},  /* 0: transparent */
    {248, 248, 248, 255},  /* 1: white highlight */
    { 16,  24,  16, 255},  /* 2: black outline */
    { 38,  98,  40, 255},  /* 3: dark green */
    { 66, 148,  64, 255},  /* 4: mid green */
    {120, 192,  96, 255},  /* 5: light green */
    {184, 224, 144, 255},  /* 6: very light green */
    {248, 224,  64, 255},  /* 7: beak yellow */
    {200, 152,  32, 255},  /* 8: beak yellow shadow */
    {248, 200, 160, 255},  /* 9: skin highlight */
    {200, 144, 104, 255},  /*10: skin mid */
    {136,  88,  64, 255},  /*11: skin shadow */
    {248, 152, 152, 255},  /*12: cheek pink */
    {248, 248, 200, 255},  /*13: eye white */
    { 32,  48,  96, 255},  /*14: eye blue */
    {128, 128, 128, 255},  /*15: fill */
};

/* GBA OBJ shape/size decode → pixels.
 *   shape: 0=square, 1=h-rect, 2=v-rect (3 is reserved)
 *   size:  0..3 within the shape's size table */
constexpr int kObjSizes[3][4][2] = {
    /* square */ {{ 8,  8}, {16, 16}, {32, 32}, {64, 64}},
    /* h-rect */ {{16,  8}, {32,  8}, {32, 16}, {64, 32}},
    /* v-rect */ {{ 8, 16}, { 8, 32}, {16, 32}, {32, 64}},
};

bool DecodeShape(uint8_t shape, uint8_t size, int& w, int& h) {
    if (shape > 2 || size > 3) return false;
    w = kObjSizes[shape][size][0];
    h = kObjSizes[shape][size][1];
    return true;
}

inline void Put(Rgba* row, int x, Rgba c) {
    if (c.a == 0) return; /* keep underlying transparent */
    row[x] = c;
}

/* Decode one 8x8 4bpp tile (32 bytes) at (px, py) on the surface.
 * hflip/vflip applied per piece. */
void DecodeTile(const uint8_t* tile, SDL_Surface* surf, int px, int py,
                bool hflip, bool vflip, const Rgba* pal) {
    if (!tile) return;
    uint8_t* pixels = static_cast<uint8_t*>(surf->pixels);
    const int pitch = surf->pitch;
    const int W = surf->w;
    const int H = surf->h;

    for (int y = 0; y < 8; ++y) {
        int destY = vflip ? py + (7 - y) : py + y;
        if (destY < 0 || destY >= H) continue;
        Rgba* row = reinterpret_cast<Rgba*>(pixels + destY * pitch);
        for (int xByte = 0; xByte < 4; ++xByte) {
            uint8_t b = tile[y * 4 + xByte];
            uint8_t lo = b & 0x0f;
            uint8_t hi = (b >> 4) & 0x0f;
            int x0 = px + xByte * 2 + 0;
            int x1 = px + xByte * 2 + 1;
            if (hflip) { x0 = px + 7 - (xByte * 2);  x1 = px + 7 - (xByte * 2 + 1); }
            if (x0 >= 0 && x0 < W) Put(row, x0, pal[lo]);
            if (x1 >= 0 && x1 < W) Put(row, x1, pal[hi]);
        }
    }
}

/* Get frame data for a sprite via gFrameObjLists indirection (mirror of
 * port_draw.c::LookupFrameData, kept local to avoid pulling in port_draw.c). */
const uint8_t* LookupFrameData(uint16_t spriteIndex, uint8_t frameIndex) {
    const size_t bytes = sizeof(uint32_t) * 50016; /* gFrameObjLists size */
    const uint8_t* base = reinterpret_cast<const uint8_t*>(gFrameObjLists);
    if ((size_t)spriteIndex >= bytes / sizeof(uint32_t)) return nullptr;
    uint32_t off1 = gFrameObjLists[spriteIndex];
    if ((size_t)off1 > bytes - sizeof(uint32_t)) return nullptr;
    size_t entry = (size_t)off1 + (size_t)frameIndex * sizeof(uint32_t);
    if (entry > bytes - sizeof(uint32_t)) return nullptr;
    uint32_t off2;
    memcpy(&off2, base + entry, sizeof(off2));
    if ((size_t)off2 >= bytes) return nullptr;
    return base + off2;
}

}  // namespace

extern "C" SDL_Surface* Port_ExtractEzloFromRom(void) {
    if (!gRomData) return nullptr;

    const SpritePtr* spr = Port_GetSpritePtr(kSpriteEzloCap);
    if (!spr || !spr->frames || !spr->ptr) return nullptr;

    SpriteFrame* frame0 = &spr->frames[0];
    if (frame0->numTiles == 0) return nullptr;

    const uint8_t* pieceData = LookupFrameData(kSpriteEzloCap, 0);
    if (!pieceData) return nullptr;

    uint8_t pieceCount = pieceData[0];
    if (pieceCount == 0 || pieceCount > 16) return nullptr;
    const uint8_t* pieces = pieceData + 1;

    /* Pass 1: bounding box */
    int minX = std::numeric_limits<int>::max();
    int minY = std::numeric_limits<int>::max();
    int maxX = std::numeric_limits<int>::min();
    int maxY = std::numeric_limits<int>::min();
    for (int i = 0; i < pieceCount; ++i) {
        int8_t xoff = static_cast<int8_t>(pieces[i * 5 + 0]);
        int8_t yoff = static_cast<int8_t>(pieces[i * 5 + 1]);
        uint8_t shapeInfo = pieces[i * 5 + 2];
        int w, h;
        if (!DecodeShape((shapeInfo >> 6) & 3, (shapeInfo >> 4) & 3, w, h)) return nullptr;
        minX = std::min(minX, (int)xoff);
        minY = std::min(minY, (int)yoff);
        maxX = std::max(maxX, (int)xoff + w);
        maxY = std::max(maxY, (int)yoff + h);
    }

    int surfW = maxX - minX;
    int surfH = maxY - minY;
    if (surfW <= 0 || surfH <= 0 || surfW > 256 || surfH > 256) return nullptr;

    SDL_Surface* surf = SDL_CreateSurface(surfW, surfH, SDL_PIXELFORMAT_RGBA32);
    if (!surf) return nullptr;
    /* Zero the surface (alpha=0 = transparent) */
    SDL_memset(surf->pixels, 0, static_cast<size_t>(surf->pitch) * surfH);

    /* Pass 2: walk pieces. The frame's tile bytes are contiguous starting at
     * spr->ptr + firstTileIndex*32, and pieces consume tiles in the order
     * they appear in the piece list. */
    const uint8_t* tileBase = static_cast<const uint8_t*>(spr->ptr) + (uint32_t)frame0->firstTileIndex * 32u;
    uint32_t tileCursor = 0;
    for (int i = 0; i < pieceCount; ++i) {
        int8_t xoff = static_cast<int8_t>(pieces[i * 5 + 0]);
        int8_t yoff = static_cast<int8_t>(pieces[i * 5 + 1]);
        uint8_t shapeInfo = pieces[i * 5 + 2];
        int w = 0, h = 0;
        if (!DecodeShape((shapeInfo >> 6) & 3, (shapeInfo >> 4) & 3, w, h)) {
            SDL_DestroySurface(surf);
            return nullptr;
        }
        bool hflip = (shapeInfo & 0x08) != 0;
        bool vflip = (shapeInfo & 0x04) != 0;

        int wTiles = w / 8;
        int hTiles = h / 8;
        int pieceTiles = wTiles * hTiles;
        if (tileCursor + pieceTiles > (uint32_t)frame0->numTiles) {
            /* Frame's declared tile count exceeded — stop to avoid OOB */
            break;
        }

        int basePx = xoff - minX;
        int basePy = yoff - minY;
        for (int ty = 0; ty < hTiles; ++ty) {
            for (int tx = 0; tx < wTiles; ++tx) {
                const uint8_t* tile = tileBase + tileCursor * 32u;
                int destTx = hflip ? (wTiles - 1 - tx) : tx;
                int destTy = vflip ? (hTiles - 1 - ty) : ty;
                DecodeTile(tile, surf,
                           basePx + destTx * 8,
                           basePy + destTy * 8,
                           hflip, vflip, kEzloApproxPalette);
                tileCursor++;
            }
        }
    }
    return surf;
}

extern "C" SDL_Surface* Port_CreateAppIcon(void) {
    if (SDL_Surface* rom_icon = Port_ExtractEzloFromRom()) {
        SDL_Log("[icon] using ROM-extracted EzloCap (%dx%d)", rom_icon->w, rom_icon->h);
        return rom_icon;
    }
    SDL_Log("[icon] using procedural placeholder (ROM/sprite tables not ready)");
    return MakePlaceholderIcon();
}
