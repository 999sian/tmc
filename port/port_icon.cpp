#include "port_icon.h"

#include <SDL3/SDL.h>
#include <cstdint>

namespace {

constexpr int kIconW = 32;
constexpr int kIconH = 32;

/* Tiny stylised green-hat silhouette. 32x32 indexed bitmap painted
 * procedurally — no Nintendo IP, no ROM data. Replaced by
 * Port_ExtractEzloFromRom() when that helper learns to decode the real
 * sprite from the user's own gRomData. */
struct Rgba { uint8_t r, g, b, a; };

constexpr Rgba kBgTransparent {0,   0,   0,   0};
constexpr Rgba kHatGreen      {66,  148, 64,  255}; /* TMC-cap-ish green */
constexpr Rgba kHatGreenDark  {38,  98,  40,  255};
constexpr Rgba kBeakYellow    {248, 200, 64,  255};
constexpr Rgba kEyeBlack      {16,  16,  16,  255};
constexpr Rgba kCheekPink     {248, 152, 152, 255};

/* 32x32 silhouette of a Minish-cap-style pointy hat with a tiny face.
 * Each row is a 32-character string:
 *   '.' = transparent  'G' = light green  'D' = dark green outline
 *   'Y' = yellow beak  'K' = eye-dot      'P' = cheek pink                  */
const char* kIconBitmap[kIconH] = {
    "................................", /*  0 */
    "................................", /*  1 */
    "..............DD................", /*  2 */
    ".............DGGD...............", /*  3 */
    "............DGGGGD..............", /*  4 */
    "...........DGGGGGGD.............", /*  5 */
    "..........DGGGGGGGGD............", /*  6 */
    ".........DGGGGGGGGGGD...........", /*  7 */
    "........DGGGGGGGGGGGGD..........", /*  8 */
    ".......DGGGGGGGGGGGGGGD.........", /*  9 */
    "......DGGGGGGGGGGGGGGGGD........", /* 10 */
    "......DGGGGGGGGGGGGGGGGD........", /* 11 */
    ".....DGGGGGGGGGGGGGGGGGGD.......", /* 12 */
    ".....DGGGGGGGGGGGGGGGGGGD.......", /* 13 */
    "....DGGGGGGGGGGGGGGGGGGGGD......", /* 14 */
    "....DGGGGGGGGGGGGGGGGGGGGD......", /* 15 */
    "....DGGGKGGGGGGGGGGGKGGGGD......", /* 16 */
    "....DGGGGGGGGGGGGGGGGGGGGD......", /* 17 */
    "....DGGGGGGGYYYYGGGGGGGGGD......", /* 18 */
    "....DGGGGGGYYYYYYGGGGGGGGD......", /* 19 */
    "....DGGGGGGGYYYYGGGGGGGGGD......", /* 20 */
    "....DGGPGGGGGGGGGGGGGGPGGD......", /* 21 */
    ".....DGGGGGGGGGGGGGGGGGGD.......", /* 22 */
    "......DGGGGGGGGGGGGGGGGD........", /* 23 */
    ".......DDGGGGGGGGGGGGDD.........", /* 24 */
    "........DDDDGGGGGGDDDD..........", /* 25 */
    "...........DDDDDDDD.............", /* 26 */
    "................................", /* 27 */
    "................................", /* 28 */
    "................................", /* 29 */
    "................................", /* 30 */
    "................................", /* 31 */
};

const Rgba& ResolvePixel(char ch) {
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
    SDL_Surface* surf = SDL_CreateSurface(kIconW, kIconH, SDL_PIXELFORMAT_RGBA32);
    if (!surf) return nullptr;

    uint8_t* pixels = static_cast<uint8_t*>(surf->pixels);
    const int pitch = surf->pitch;
    for (int y = 0; y < kIconH; ++y) {
        Rgba* row = reinterpret_cast<Rgba*>(pixels + y * pitch);
        for (int x = 0; x < kIconW; ++x) {
            row[x] = ResolvePixel(kIconBitmap[y][x]);
        }
    }
    return surf;
}

}  // namespace

extern "C" SDL_Surface* Port_ExtractEzloFromRom(void) {
    /* TODO: decode SPRITE_EZLOCAP frame 0 via Port_GetSpritePtr() +
     * the gfx-pool / palette-group resolution that currently lives only
     * in the offline asset_extractor. Until that lands, the runtime
     * falls back to MakePlaceholderIcon() below. */
    return nullptr;
}

extern "C" SDL_Surface* Port_CreateAppIcon(void) {
    if (SDL_Surface* rom_icon = Port_ExtractEzloFromRom()) {
        return rom_icon;
    }
    return MakePlaceholderIcon();
}
