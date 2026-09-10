#include <SDL3/SDL.h>
#include "port_level_editor.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <vector>
#include <string>

extern "C" {
#define this this_ptr
#include "global.h"
#include "room.h"
#include "area.h"
#include "entity.h"
#include "main.h"
#include "game.h"
#undef this

#define TASK_GAME 2

// Game engine functions & variables not in standard headers
void SetTile(unsigned int tileIndex, unsigned int tilePos, unsigned int layer);
void sub_0807BBE4(void);
extern unsigned char gUpdateVisibleTiles;
extern unsigned short gMapDataBottomSpecial[0x4000];
extern unsigned short gMapDataTopSpecial[0x4000];
void RenderMapLayerToSubTileMap(unsigned short* subTileMap, MapLayer* mapLayer);
void UpdateScrollVram(void);
void FillActTileForLayer(MapLayer* mapLayer);
void CreateCollisionDataBorderAroundRoom(void);
void sub_0807C460(void);
void sub_0807C5B0(void);

struct VirtuaPPUMode1GbaMemory {
    unsigned char *io_mem;
    unsigned char *vram;
    unsigned short *bg_palette;
    unsigned short *obj_palette;
    unsigned short *oam_mem;
};
void virtuappu_mode1_get_bound_gba_memory(VirtuaPPUMode1GbaMemory *memory);
unsigned short virtuappu_mode1_io_read16(unsigned short offset);
int Port_PPU_VisibleFrameWidth(void);

void SoundReq(unsigned int sound);
} // extern "C"

namespace {

bool sOpen = false;
unsigned int sSelectedTileIndex = 0x115; // default to a standard tile index
unsigned int sSelectedLayer = 1; // 1 = Bottom layer, 2 = Top layer
bool sLeftMouseDown = false;

// Mouse coordinates
float sMouseX = 0.0f;
float sMouseY = 0.0f;

// Hover status
int sHoverTileX = -1;
int sHoverTileY = -1;
int sHoverTilePos = -1;
unsigned int sHoveredTileIndex = 0;

// Toast notifications
std::string sToastMsg;
unsigned int sToastUntilTicks = 0;

std::vector<char> sCustomList1Data;
std::vector<char> sCustomList2Data;
std::vector<char> sCustomList3Data;
std::vector<char> sCustomChestData;
std::vector<char> sCustomWarpData;

const char* GetBgmName(unsigned int bgm) {
    switch (bgm) {
        case 1: return "Castle Tournament";
        case 2: return "Vaati Motif";
        case 3: return "Title Screen";
        case 4: return "Castle Motif";
        case 5: return "Element Get";
        case 6: return "Fairy Fountain";
        case 7: return "File Select";
        case 8: return "Intro Cutscene";
        case 9: return "Credits";
        case 10: return "Game Over";
        case 11: return "Saving Zelda";
        case 12: return "LTTP Title";
        case 13: return "Vaati Theme";
        case 14: return "Ezlo Theme";
        case 15: return "Story";
        case 16: return "Festival Approach";
        case 17: return "Beat Vaati";
        case 19: return "Beanstalk";
        case 20: return "House";
        case 21: return "Cucco Minigame";
        case 22: return "Syrup Theme";
        case 23: return "Dungeon";
        case 24: return "Element Theme";
        case 25: return "Hyrule Field";
        case 26: return "Hyrule Castle";
        case 27: return "Hyrule Castle (No Intro)";
        case 28: return "Minish Village";
        case 29: return "Minish Woods";
        case 30: return "Crenel Storm";
        case 31: return "Castor Wilds";
        case 32: return "Hyrule Town";
        case 33: return "Royal Valley";
        case 34: return "Cloud Tops";
        case 35: return "Dark Hyrule Castle";
        case 36: return "Secret Castle Entrance";
        case 37: return "Deepwood Shrine";
        case 38: return "Cave of Flames";
        case 39: return "Fortress of Winds";
        case 40: return "Temple of Droplets";
        case 41: return "Palace of Winds";
        case 42: return "Ezlo Story";
        case 43: return "Royal Crypt";
        case 44: return "Elemental Sanctuary";
        case 45: return "Fight Theme";
        case 46: return "Boss Theme";
        case 47: return "Vaati Reborn";
        case 48: return "Vaati Transfigured";
        case 49: return "Castle Collapse";
        case 50: return "Vaati Wrath";
        case 51: return "Fight Theme 2";
        case 52: return "Digging Cave";
        case 53: return "Swiftblade Dojo";
        case 54: return "Minish Cap";
        case 55: return "Mt. Crenel";
        case 56: return "Picori Festival";
        case 57: return "Lost Woods";
        case 58: return "Fairy Fountain 2";
        case 59: return "Wind Ruins";
        default: return "Unknown / Silence";
    }
}

void UpdateTileAndRedraw(unsigned int tileIndex, unsigned int tilePos, unsigned int layer) {
    SetTile(tileIndex, tilePos, layer);
    if (layer == 2) {
        RenderMapLayerToSubTileMap(gMapDataTopSpecial, &gMapTop);
    } else {
        RenderMapLayerToSubTileMap(gMapDataBottomSpecial, &gMapBottom);
    }
    gUpdateVisibleTiles = 1;
    UpdateScrollVram();
}

struct ColorRGBA {
    unsigned char r, g, b, a;
};

bool RenderTileToBuffer(unsigned int tileIndex, unsigned int layer, ColorRGBA* outBuffer) {
    VirtuaPPUMode1GbaMemory mem;
    virtuappu_mode1_get_bound_gba_memory(&mem);
    if (!mem.vram || !mem.bg_palette) return false;

    MapLayer* ml = (layer == 2) ? &gMapTop : &gMapBottom;
    if (tileIndex >= 2048) return false;

    const unsigned short* metatile = &ml->subTiles[tileIndex * 4];

    unsigned short bgcnt = virtuappu_mode1_io_read16(layer == 2 ? 0x0A : 0x0C);
    unsigned int char_base = ((bgcnt >> 2) & 3) * 0x4000;

    for (int sub = 0; sub < 4; ++sub) {
        unsigned short entry = metatile[sub];
        uint16_t tile_id = entry & 0x03FF;
        bool hflip = (entry >> 10) & 1;
        bool vflip = (entry >> 11) & 1;
        unsigned char palette_idx = (entry >> 12) & 0x0F;

        int subX = (sub & 1) * 8;
        int subY = (sub >> 1) * 8;

        for (int py = 0; py < 8; ++py) {
            int drawY = vflip ? (7 - py) : py;
            for (int px = 0; px < 8; ++px) {
                int drawX = hflip ? (7 - px) : px;

                unsigned char color_index = 0;
                bool bpp8 = ((bgcnt >> 7) & 1) != 0;

                if (bpp8) {
                    unsigned int addr = char_base + tile_id * 64 + drawY * 8 + drawX;
                    color_index = (addr < 0x18000) ? mem.vram[addr] : 0;
                } else {
                    unsigned int addr = char_base + tile_id * 32 + drawY * 4 + (drawX / 2);
                    unsigned char packed = (addr < 0x18000) ? mem.vram[addr] : 0;
                    color_index = (drawX & 1) ? (packed >> 4) : (packed & 0x0F);
                }

                int canvasX = subX + px;
                int canvasY = subY + py;
                int bufIdx = canvasY * 16 + canvasX;

                if (color_index == 0) {
                    outBuffer[bufIdx] = {0, 0, 0, 0};
                } else {
                    unsigned short rgb555;
                    if (bpp8) {
                        rgb555 = mem.bg_palette[color_index];
                    } else {
                        rgb555 = mem.bg_palette[palette_idx * 16 + color_index];
                    }
                    unsigned char r = (rgb555 & 0x1F) << 3;
                    unsigned char g = ((rgb555 >> 5) & 0x1F) << 3;
                    unsigned char b = ((rgb555 >> 10) & 0x1F) << 3;
                    outBuffer[bufIdx] = {r, g, b, 255};
                }
            }
        }
    }
    return true;
}

void ShowToast(const std::string& msg) {
    sToastMsg = msg;
    sToastUntilTicks = SDL_GetTicks() + 2000; // 2 seconds
}

void ComputeGbaFitRect(int w, int h, int* outX, int* outY, int* outW, int* outH) {
    const int FW = Port_PPU_VisibleFrameWidth();
    const int FH = 160;
    int rw;
    int rh;
    if (w * FH >= h * FW) {
        rh = h;
        rw = (h * FW) / FH;
    } else {
        rw = w;
        rh = (w * FH) / FW;
    }
    *outX = (w - rw) / 2;
    *outY = (h - rh) / 2;
    *outW = rw;
    *outH = rh;
}

// Convert window mouse coords to GBA world tile index
bool GetTileUnderMouse(float mouseX, float mouseY, int winW, int winH, int* outTileX, int* outTileY, int* outTilePos) {
    int rx, ry, rw, rh;
    ComputeGbaFitRect(winW, winH, &rx, &ry, &rw, &rh);

    if (rw <= 0 || rh <= 0) return false;

    const int FW = Port_PPU_VisibleFrameWidth();
    // Relative to GBA canvas
    float gbaX = (mouseX - rx) * (float)FW / rw;
    float gbaY = (mouseY - ry) * 160.0f / rh;

    if (gbaX < 0.0f || gbaX >= (float)FW || gbaY < 0.0f || gbaY >= 160.0f) {
        return false;
    }

    // World coordinates in pixels
    int worldX = static_cast<int>(gbaX) + gRoomControls.scroll_x;
    int worldY = static_cast<int>(gbaY) + gRoomControls.scroll_y;

    // Coordinates relative to the room origin
    int roomX = worldX - gRoomControls.origin_x;
    int roomY = worldY - gRoomControls.origin_y;

    if (roomX < 0 || roomY < 0) return false;

    int tileX = roomX / 16;
    int tileY = roomY / 16;

    if (tileX < 0 || tileX >= 64 || tileY < 0 || tileY >= 64) {
        return false;
    }

    *outTileX = tileX;
    *outTileY = tileY;
    *outTilePos = tileX + (tileY << 6);
    return true;
}

void SaveCurrentRoom() {
    unsigned char area = gRoomControls.area;
    unsigned char room = gRoomControls.room;

    try {
        std::filesystem::create_directories("edited_levels");
        char path[256];
        std::snprintf(path, sizeof(path), "edited_levels/area%02X_room%02X.bin", area, room);

        std::ofstream output(path, std::ios::binary);
        if (output.good()) {
            output.write(reinterpret_cast<const char*>(gMapBottom.mapData), sizeof(gMapBottom.mapData));
            output.write(reinterpret_cast<const char*>(gMapTop.mapData), sizeof(gMapTop.mapData));

            // Append level properties (BGM, Fight BGM, Light Level)
            unsigned short bgm = gArea.queued_bgm;
            unsigned short fight_bgm = gRoomVars.fight_bgm;
            short lightLevel = gRoomVars.lightLevel;
            output.write(reinterpret_cast<const char*>(&bgm), sizeof(bgm));
            output.write(reinterpret_cast<const char*>(&fight_bgm), sizeof(fight_bgm));
            output.write(reinterpret_cast<const char*>(&lightLevel), sizeof(lightLevel));

            ShowToast("Saved to " + std::string(path));
            std::fprintf(stderr, "[LEVEL EDITOR] Saved area 0x%02X room 0x%02X to %s (with properties)\n", area, room, path);
        } else {
            ShowToast("Error opening save file!");
        }
    } catch (const std::exception& e) {
        ShowToast("Save failed: " + std::string(e.what()));
    }
}

} // namespace

extern "C" bool Port_LevelEditor_IsOpen(void) {
    return sOpen;
}

extern "C" void Port_LevelEditor_Toggle(void) {
    if (sOpen) {
        sOpen = false;
        sLeftMouseDown = false;
    } else {
        sOpen = true;
        sLeftMouseDown = false;
        // Sample tile at camera center
        int cx = gRoomControls.scroll_x + 120;
        int cy = gRoomControls.scroll_y + 80;
        int rx = cx - gRoomControls.origin_x;
        int ry = cy - gRoomControls.origin_y;
        int tx = rx / 16;
        int ty = ry / 16;
        if (tx >= 0 && tx < 64 && ty >= 0 && ty < 64) {
            MapLayer* ml = (sSelectedLayer == 2) ? &gMapTop : &gMapBottom;
            sSelectedTileIndex = ml->mapData[tx + (ty << 6)];
        }
        ShowToast("Level Editor Mode Active");
    }
}

extern "C" bool Port_LevelEditor_HandleKey(int sdlKey, int sdlScancode) {
    if (!sOpen) return false;

    if (sdlKey == SDLK_LEFTBRACKET || sdlScancode == SDL_SCANCODE_LEFTBRACKET) {
        if (SDL_GetModState() & SDL_KMOD_SHIFT) {
            sSelectedTileIndex = (sSelectedTileIndex >= 16) ? sSelectedTileIndex - 16 : 0;
        } else {
            sSelectedTileIndex = (sSelectedTileIndex > 0) ? sSelectedTileIndex - 1 : 0;
        }
        return true;
    }
    if (sdlKey == SDLK_RIGHTBRACKET || sdlScancode == SDL_SCANCODE_RIGHTBRACKET) {
        if (SDL_GetModState() & SDL_KMOD_SHIFT) {
            sSelectedTileIndex = std::min(sSelectedTileIndex + 16, 2047u);
        } else {
            sSelectedTileIndex = std::min(sSelectedTileIndex + 1, 2047u);
        }
        return true;
    }

    switch (sdlKey) {
        case SDLK_ESCAPE:
            Port_LevelEditor_Toggle();
            return true;
        case SDLK_L:
            sSelectedLayer = (sSelectedLayer == 1) ? 2 : 1;
            ShowToast("Switch to Layer " + std::to_string(sSelectedLayer));
            return true;
        case SDLK_M: {
            unsigned int bgm = gArea.queued_bgm;
            if (SDL_GetModState() & SDL_KMOD_SHIFT) {
                bgm = (bgm > 1) ? bgm - 1 : 99;
            } else {
                bgm = (bgm < 99) ? bgm + 1 : 1;
            }
            gArea.queued_bgm = bgm;
            gArea.bgm = bgm;
            SoundReq(bgm);
            ShowToast("BGM Changed: " + std::string(GetBgmName(bgm)));
            return true;
        }
        case SDLK_F: {
            unsigned int fbgm = gRoomVars.fight_bgm;
            if (SDL_GetModState() & SDL_KMOD_SHIFT) {
                fbgm = (fbgm > 1) ? fbgm - 1 : 99;
            } else {
                fbgm = (fbgm < 99) ? fbgm + 1 : 1;
            }
            gRoomVars.fight_bgm = fbgm;
            ShowToast("Fight BGM: " + std::string(GetBgmName(fbgm)));
            return true;
        }
        case SDLK_K: {
            short light = gRoomVars.lightLevel;
            if (SDL_GetModState() & SDL_KMOD_SHIFT) {
                light -= 16;
            } else {
                light += 16;
            }
            gRoomVars.lightLevel = light;
            ShowToast("Room Light Level: " + std::to_string(light));
            return true;
        }
        case SDLK_S:
            SaveCurrentRoom();
            return true;
        default:
            break;
    }
    return false;
}

extern "C" SDL_Window* Port_PPU_ActiveWindow(void);

extern "C" void Port_LevelEditor_HandleMouseButton(int button, int state, float x, float y) {
    if (!sOpen) return;

    SDL_Window* win = Port_PPU_ActiveWindow();
    if (!win) {
        win = SDL_GetKeyboardFocus();
    }

    int winW = 960, winH = 540;
    int renderW = 960, renderH = 540;
    if (win) {
        SDL_GetWindowSize(win, &winW, &winH);
        SDL_Renderer* ren = SDL_GetRenderer(win);
        if (ren) {
            SDL_GetCurrentRenderOutputSize(ren, &renderW, &renderH);
        } else {
            renderW = winW;
            renderH = winH;
        }
    }

    float scaleX = (winW > 0) ? static_cast<float>(renderW) / static_cast<float>(winW) : 1.0f;
    float scaleY = (winH > 0) ? static_cast<float>(renderH) / static_cast<float>(winH) : 1.0f;

    float renderMouseX = x * scaleX;
    float renderMouseY = y * scaleY;

    sMouseX = renderMouseX;
    sMouseY = renderMouseY;

    int tileX, tileY, tilePos;
    bool inside = GetTileUnderMouse(renderMouseX, renderMouseY, renderW, renderH, &tileX, &tileY, &tilePos);

    if (button == SDL_BUTTON_LEFT) {
        if (state == 1) { // 1 = pressed (SDL_PRESSED removed in SDL3)
            sLeftMouseDown = true;
            if (inside) {
                UpdateTileAndRedraw(sSelectedTileIndex, tilePos, sSelectedLayer);
            }
        } else {
            sLeftMouseDown = false;
        }
    } else if (button == SDL_BUTTON_RIGHT && state == 1) {
        // Eyedropper: sample tile under cursor
        if (inside) {
            MapLayer* ml = (sSelectedLayer == 2) ? &gMapTop : &gMapBottom;
            sSelectedTileIndex = ml->mapData[tilePos];
            ShowToast("Copied Tile: 0x" + std::to_string(sSelectedTileIndex));
        }
    }
}

extern "C" void Port_LevelEditor_HandleMouseMotion(float x, float y) {
    if (!sOpen) return;

    SDL_Window* win = Port_PPU_ActiveWindow();
    if (!win) {
        win = SDL_GetKeyboardFocus();
    }

    int winW = 960, winH = 540;
    int renderW = 960, renderH = 540;
    if (win) {
        SDL_GetWindowSize(win, &winW, &winH);
        SDL_Renderer* ren = SDL_GetRenderer(win);
        if (ren) {
            SDL_GetCurrentRenderOutputSize(ren, &renderW, &renderH);
        } else {
            renderW = winW;
            renderH = winH;
        }
    }

    float scaleX = (winW > 0) ? static_cast<float>(renderW) / static_cast<float>(winW) : 1.0f;
    float scaleY = (winH > 0) ? static_cast<float>(renderH) / static_cast<float>(winH) : 1.0f;

    float renderMouseX = x * scaleX;
    float renderMouseY = y * scaleY;

    sMouseX = renderMouseX;
    sMouseY = renderMouseY;

    bool inside = GetTileUnderMouse(renderMouseX, renderMouseY, renderW, renderH, &sHoverTileX, &sHoverTileY, &sHoverTilePos);
    if (inside) {
        MapLayer* ml = (sSelectedLayer == 2) ? &gMapTop : &gMapBottom;
        sHoveredTileIndex = ml->mapData[sHoverTilePos];

        if (sLeftMouseDown) {
            UpdateTileAndRedraw(sSelectedTileIndex, sHoverTilePos, sSelectedLayer);
        }
    } else {
        sHoverTileX = -1;
        sHoverTileY = -1;
        sHoverTilePos = -1;
    }
}

extern "C" void Port_LevelEditor_Render(void* renderer, int winW, int winH) {
    SDL_Renderer* ren = static_cast<SDL_Renderer*>(renderer);
    if (!ren) return;

    // Render toast message
    if (!sToastMsg.empty() && SDL_GetTicks() < sToastUntilTicks) {
        const int charW = SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE;
        int textW = static_cast<int>(sToastMsg.size()) * charW;
        SDL_FRect bg = { (winW - textW) * 0.5f - 6.0f, winH - 30.0f, static_cast<float>(textW) + 12.0f, 18.0f };
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 200);
        SDL_RenderFillRect(ren, &bg);
        SDL_SetRenderDrawColor(ren, 255, 240, 64, 255);
        SDL_RenderDebugText(ren, bg.x + 6.0f, bg.y + 5.0f, sToastMsg.c_str());
    }

    if (!sOpen) return;

    // 1. Draw outline box around hovered tile
    if (sHoverTileX >= 0 && sHoverTileY >= 0) {
        int rx, ry, rw, rh;
        ComputeGbaFitRect(winW, winH, &rx, &ry, &rw, &rh);

        int worldX = sHoverTileX * 16 + gRoomControls.origin_x;
        int worldY = sHoverTileY * 16 + gRoomControls.origin_y;

        int gbaX = worldX - gRoomControls.scroll_x;
        int gbaY = worldY - gRoomControls.scroll_y;

        const int FW = Port_PPU_VisibleFrameWidth();
        float screenX = rx + gbaX * static_cast<float>(rw) / (float)FW;
        float screenY = ry + gbaY * static_cast<float>(rh) / 160.0f;
        float tileW = 16.0f * static_cast<float>(rw) / (float)FW;
        float tileH = 16.0f * static_cast<float>(rh) / 160.0f;

        SDL_FRect tileRect = { screenX, screenY, tileW, tileH };
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(ren, 255, 240, 64, 180); // translucent yellow outline
        SDL_RenderRect(ren, &tileRect);

        // Highlight with translucent filling
        SDL_SetRenderDrawColor(ren, 255, 240, 64, 50);
        SDL_RenderFillRect(ren, &tileRect);

        // 1.5 Draw transparent preview of the selected/copied tile
        ColorRGBA tileBuf[256];
        if (RenderTileToBuffer(sSelectedTileIndex, sSelectedLayer, tileBuf)) {
            float pxW = tileW / 16.0f;
            float pxH = tileH / 16.0f;
            for (int cy = 0; cy < 16; ++cy) {
                for (int cx = 0; cx < 16; ++cx) {
                    ColorRGBA c = tileBuf[cy * 16 + cx];
                    if (c.a > 0) {
                        SDL_FRect r = { screenX + cx * pxW, screenY + cy * pxH, pxW, pxH };
                        SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, 160); // 160 = ~63% opacity, beautiful and transparent
                        SDL_RenderFillRect(ren, &r);
                    }
                }
            }
        }
    }

    // 2. Draw level editor controls sidebar overlay (left top)
    const int charW = SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE;
    char textBuf[128];
    std::vector<std::string> lines;
    lines.push_back("=== LEVEL EDITOR MODE ===");
    lines.push_back("");

    std::snprintf(textBuf, sizeof(textBuf), "Area: 0x%02X | Room: 0x%02X", gRoomControls.area, gRoomControls.room);
    lines.push_back(textBuf);

    std::snprintf(textBuf, sizeof(textBuf), "Layer: %s", sSelectedLayer == 2 ? "[TOP] (2)" : "[BOTTOM] (1)");
    lines.push_back(textBuf);

    std::snprintf(textBuf, sizeof(textBuf), "Selected Tile Index: 0x%03X (%d)", sSelectedTileIndex, sSelectedTileIndex);
    lines.push_back(textBuf);

    std::snprintf(textBuf, sizeof(textBuf), "Room BGM : 0x%02X (%s)", gArea.queued_bgm, GetBgmName(gArea.queued_bgm));
    lines.push_back(textBuf);

    std::snprintf(textBuf, sizeof(textBuf), "Fight BGM: 0x%02X (%s)", gRoomVars.fight_bgm, GetBgmName(gRoomVars.fight_bgm));
    lines.push_back(textBuf);

    std::snprintf(textBuf, sizeof(textBuf), "Light Lvl: %d", gRoomVars.lightLevel);
    lines.push_back(textBuf);

    if (sHoverTileX >= 0) {
        std::snprintf(textBuf, sizeof(textBuf), "Hover: (%d, %d) | Pos: 0x%03X", sHoverTileX, sHoverTileY, sHoverTilePos);
        lines.push_back(textBuf);
        std::snprintf(textBuf, sizeof(textBuf), "Hovered Tile Index: 0x%03X", sHoveredTileIndex);
        lines.push_back(textBuf);
    } else {
        lines.push_back("Hover: Outside room map");
        lines.push_back("");
    }

    lines.push_back("");
    lines.push_back("--- HOTKEYS ---");
    lines.push_back("L      : Switch layer (Top/Bottom)");
    lines.push_back("[ / ]  : Cycle selected tile index -1 / +1");
    lines.push_back("{ / }  : Cycle selected tile index -16 / +16");
    lines.push_back("M / Sh+M: Cycle Room BGM");
    lines.push_back("F / Sh+F: Cycle Fight BGM");
    lines.push_back("K / Sh+K: Change Light Level");
    lines.push_back("L Click: Paint tile (drag to draw)");
    lines.push_back("R Click: Eyedropper (clone tile)");
    lines.push_back("S      : Save room level permanently");
    lines.push_back("Esc    : Close Level Editor");

    int rows = static_cast<int>(lines.size());
    int cols = 0;
    for (const auto& line : lines) {
        cols = std::max(cols, static_cast<int>(line.size()));
    }

    float boxW = static_cast<float>(cols * charW + 16);
    float boxH = static_cast<float>(rows * (charW + 4) + 12);
    SDL_FRect box = { 10.0f, 10.0f, boxW, boxH };

    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 220); // translucent black
    SDL_RenderFillRect(ren, &box);
    SDL_SetRenderDrawColor(ren, 200, 200, 200, 255);
    SDL_RenderRect(ren, &box);

    float yPos = box.y + 8.0f;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (i == 0) {
            SDL_SetRenderDrawColor(ren, 255, 240, 64, 255); // yellow header
        } else if (lines[i].rfind("---", 0) == 0) {
            SDL_SetRenderDrawColor(ren, 200, 220, 255, 255); // light blue separator
        } else {
            SDL_SetRenderDrawColor(ren, 230, 230, 230, 255); // white text
        }
        SDL_RenderDebugText(ren, box.x + 8.0f, yPos, lines[i].c_str());
        yPos += charW + 4.0f;
    }
}

extern "C" {
    void LZ77UnCompVram(const void* src, void* dst);
    void LZ77UnCompWram(const void* src, void* dst);
    void LoadRoomTileSet(void);
    void LoadRoomGfx(void);
}

extern "C" void Port_LevelEditor_OnRoomLoad(void) {
    std::fprintf(stderr, "[LEVEL EDITOR LOG] OnRoomLoad called. task=%d, size=%dx%d, transitioningOut=%d, type=%d, field2d=%d\n", 
                 gMain.task, gRoomControls.width, gRoomControls.height, gRoomTransition.transitioningOut, gRoomTransition.type, gRoomTransition.field2d);
    if (gRoomTransition.field2d == 2) {
        std::fprintf(stderr, "[LEVEL EDITOR LOG] Exiting: field2d is 2 (transition scroll swap)\n");
        return;
    }
    if (gMain.task != TASK_GAME) {
        std::fprintf(stderr, "[LEVEL EDITOR LOG] Exiting: not TASK_GAME\n");
        return;
    }
    if (gRoomControls.width == 0 || gRoomControls.height == 0) {
        std::fprintf(stderr, "[LEVEL EDITOR LOG] Exiting: room size 0\n");
        return;
    }
    unsigned char area = gRoomControls.area;
    unsigned char room = gRoomControls.room;
    std::fprintf(stderr, "[LEVEL EDITOR LOG] Processing room: area=0x%02X, room=0x%02X\n", area, room);

    // Check if there are custom map assets or custom layout binary files for this area/room
    bool hasCustomAreaGfx = false;
    bool hasCustomRoomLayout = false;

    {
        char pathPal[256];
        std::snprintf(pathPal, sizeof(pathPal), "Areas/Area %02X/paletteDat.bin", area);
        std::ifstream fPal(pathPal, std::ios::binary);
        if (fPal.good()) {
            hasCustomAreaGfx = true;
        } else {
            std::snprintf(pathPal, sizeof(pathPal), "build/pc/Areas/Area %02X/paletteDat.bin", area);
            fPal.close();
            fPal.open(pathPal, std::ios::binary);
            if (fPal.good()) {
                hasCustomAreaGfx = true;
            }
        }
    }

    {
        char pathBG1[256];
        std::snprintf(pathBG1, sizeof(pathBG1), "Areas/Area %02X/bg1TileSetDat.bin", area);
        std::ifstream fBG1(pathBG1, std::ios::binary);
        if (fBG1.good()) {
            hasCustomAreaGfx = true;
        } else {
            std::snprintf(pathBG1, sizeof(pathBG1), "build/pc/Areas/Area %02X/bg1TileSetDat.bin", area);
            fBG1.close();
            fBG1.open(pathBG1, std::ios::binary);
            if (fBG1.good()) {
                hasCustomAreaGfx = true;
            }
        }
    }

    char pathLayout[256];
    std::snprintf(pathLayout, sizeof(pathLayout), "edited_levels/area%02X_room%02X.bin", area, room);
    {
        std::ifstream fLayout(pathLayout, std::ios::binary);
        if (fLayout.good()) {
            hasCustomRoomLayout = true;
        }
    }

    bool hasCustomMap = hasCustomAreaGfx || hasCustomRoomLayout;
    std::fprintf(stderr, "[LEVEL EDITOR LOG] hasCustomMap=%d (gfx=%d, layout=%d)\n", hasCustomMap, hasCustomAreaGfx, hasCustomRoomLayout);

    if (hasCustomMap) {
        // ----------------------------------------------------
        // Load Custom Area Graphics, Palettes, and Metatiles on Room Load
        // ----------------------------------------------------
        {
            // 1. Palette (uncompressed, 512 bytes)
            char pathPal[256];
            std::snprintf(pathPal, sizeof(pathPal), "Areas/Area %02X/paletteDat.bin", area);
            std::ifstream fPal(pathPal, std::ios::binary);
            if (!fPal.good()) {
                std::snprintf(pathPal, sizeof(pathPal), "build/pc/Areas/Area %02X/paletteDat.bin", area);
                fPal.close(); fPal.open(pathPal, std::ios::binary);
            }
            if (fPal.good()) {
                fPal.read(reinterpret_cast<char*>(gBgPltt), 512);
                std::fprintf(stderr, "[LEVEL EDITOR] Loaded Area %02X custom palette (512 bytes) from %s\n", area, pathPal);
            }

            // Helper lambdas to load and decompress custom LZ77 compressed assets
            auto loadCustomLZ77Vram = [](const char* filename, unsigned char areaIndex, void* destVramGbaAddr) {
                char pathTS[256];
                std::snprintf(pathTS, sizeof(pathTS), "Areas/Area %02X/%s", areaIndex, filename);
                std::ifstream fTS(pathTS, std::ios::binary);
                if (!fTS.good()) {
                    std::snprintf(pathTS, sizeof(pathTS), "build/pc/Areas/Area %02X/%s", areaIndex, filename);
                    fTS.close(); fTS.open(pathTS, std::ios::binary);
                }
                if (fTS.good()) {
                    std::vector<char> compData((std::istreambuf_iterator<char>(fTS)), std::istreambuf_iterator<char>());
                    if (compData.size() >= 4) {
                        LZ77UnCompVram(compData.data(), destVramGbaAddr);
                        std::fprintf(stderr, "[LEVEL EDITOR] Decompressed Area %02X custom asset %s (%zu compressed bytes) to VRAM at %p\n", areaIndex, filename, compData.size(), destVramGbaAddr);
                    }
                }
            };

            auto loadCustomLZ77Wram = [](const char* filename, unsigned char areaIndex, void* destWramGbaAddr) {
                char pathTS[256];
                std::snprintf(pathTS, sizeof(pathTS), "Areas/Area %02X/%s", areaIndex, filename);
                std::ifstream fTS(pathTS, std::ios::binary);
                if (!fTS.good()) {
                    std::snprintf(pathTS, sizeof(pathTS), "build/pc/Areas/Area %02X/%s", areaIndex, filename);
                    fTS.close(); fTS.open(pathTS, std::ios::binary);
                }
                if (fTS.good()) {
                    std::vector<char> compData((std::istreambuf_iterator<char>(fTS)), std::istreambuf_iterator<char>());
                    if (compData.size() >= 4) {
                        LZ77UnCompWram(compData.data(), destWramGbaAddr);
                        std::fprintf(stderr, "[LEVEL EDITOR] Decompressed Area %02X custom asset %s (%zu compressed bytes) to WRAM at %p\n", areaIndex, filename, compData.size(), destWramGbaAddr);
                    }
                }
            };

            // 2. Tilesets (BG1, Common, BG2)
            loadCustomLZ77Vram("bg1TileSetDat.bin", area, (void*)0x06000000);
            loadCustomLZ77Vram("commonTileSetDat.bin", area, (void*)0x06004000);
            loadCustomLZ77Vram("bg2TileSetDat.bin", area, (void*)0x06008000);

            // 3. Metatilesets & Metatile Types (BG1, BG2)
            // Load into native PC structured variables (outside gEwram[])
            loadCustomLZ77Wram("bg1MetaTileSetDat.bin", area, (void*)&gMapTop.subTiles);
            loadCustomLZ77Wram("bg2MetaTileSetDat.bin", area, (void*)&gMapBottom.subTiles);
            loadCustomLZ77Wram("bg1MetaTileTypeDat.bin", area, (void*)&gMapTop.tileTypes);
            loadCustomLZ77Wram("bg2MetaTileTypeDat.bin", area, (void*)&gMapBottom.tileTypes);

            // Also load into emulated GBA EWRAM addresses for absolute safety
            loadCustomLZ77Wram("bg1MetaTileSetDat.bin", area, (void*)0x02012654);
            loadCustomLZ77Wram("bg2MetaTileSetDat.bin", area, (void*)0x0202CEB4);
            loadCustomLZ77Wram("bg1MetaTileTypeDat.bin", area, (void*)0x02010654);
            loadCustomLZ77Wram("bg2MetaTileTypeDat.bin", area, (void*)0x0202AEB4);
        }

        // 3. Rebuild the tileIndices helper maps for gMapBottom and gMapTop
        {
            unsigned short* tileTypes = gMapBottom.tileTypes;
            unsigned short* tileIndices = gMapBottom.tileIndices;
            std::memset(tileIndices, 0xff, 2048 * sizeof(unsigned short));
            for (int index = 0; index < 0x800; index++, tileTypes++) {
                if ((*tileTypes < 0x800) && (tileIndices[*tileTypes] == 0xffff)) {
                    tileIndices[*tileTypes] = index;
                }
            }

            tileTypes = gMapTop.tileTypes;
            tileIndices = gMapTop.tileIndices;
            std::memset(tileIndices, 0xff, 2048 * sizeof(unsigned short));
            for (int index = 0; index < 0x800; index++, tileTypes++) {
                if ((*tileTypes < 0x800) && (tileIndices[*tileTypes] == 0xffff)) {
                    tileIndices[*tileTypes] = index;
                }
            }
        }

        // 4. Load the custom room layout from the edited room binary if it exists
        if (hasCustomRoomLayout) {
            std::ifstream input(pathLayout, std::ios::binary);
            if (input.good()) {
                // Determine the actual number of elements needed based on the room height.
                // Read the full 4096 elements for both layers. Since we exit early if in
                // transition (field2d == 2), we can safely overwrite the entire buffer.
                input.read(reinterpret_cast<char*>(gMapBottom.mapData), 4096 * sizeof(unsigned short));
                input.read(reinterpret_cast<char*>(gMapTop.mapData), 4096 * sizeof(unsigned short));

                // Align mapDataOriginal so that restore and gameplay systems operate properly
                std::memcpy(gMapBottom.mapDataOriginal, gMapBottom.mapData, 4096 * sizeof(unsigned short));
                std::memcpy(gMapTop.mapDataOriginal, gMapTop.mapData, 4096 * sizeof(unsigned short));



                // Rebuild room collision data completely
                sub_0807BBE4();
                sub_0807C460();

                // Merge top layer collisions into bottom layer if the area is indoor/house
                switch (gRoomControls.area) {
                    case 0x21: // AREA_HOUSE_INTERIORS_1
                    case 0x22: // AREA_HOUSE_INTERIORS_2
                    case 0x23: // AREA_HOUSE_INTERIORS_3
                    case 0x24: // AREA_TREE_INTERIORS
                    case 0x25: // AREA_DOJOS
                    case 0x27: // AREA_MINISH_CRACKS
                    case 0x28: // AREA_HOUSE_INTERIORS_4
                    case 0x30: // AREA_WIND_TRIBE_TOWER
                    case 0x38: // AREA_EZLO_CUTSCENE
                        sub_0807C5B0();
                        break;
                }

                CreateCollisionDataBorderAroundRoom();
                FillActTileForLayer(&gMapBottom);
                FillActTileForLayer(&gMapTop);



                // Check for appended metadata (BGM, Fight BGM, Light Level)
                // We must seek to the absolute end of the top layer (16384 bytes) since we seekg'd earlier.
                input.seekg(16384, std::ios::beg);
                if (input.peek() != EOF) {
                    unsigned short bgm = 0;
                    unsigned short fight_bgm = 0;
                    short lightLevel = 0;
                    input.read(reinterpret_cast<char*>(&bgm), sizeof(bgm));
                    input.read(reinterpret_cast<char*>(&fight_bgm), sizeof(fight_bgm));
                    input.read(reinterpret_cast<char*>(&lightLevel), sizeof(lightLevel));
                    if (input.gcount() == sizeof(lightLevel)) {
                        if (bgm < 60 && fight_bgm < 60) {
                            gArea.queued_bgm = bgm;
                            gArea.bgm = bgm;
                            gRoomVars.fight_bgm = fight_bgm;
                            gRoomVars.lightLevel = lightLevel;
                            SoundReq(bgm);
                            std::fprintf(stderr, "[LEVEL EDITOR] Loaded persistent properties: bgm=%u, fight_bgm=%u, lightLevel=%d\n", bgm, fight_bgm, lightLevel);
                        } else {
                            std::fprintf(stderr, "[LEVEL EDITOR WARNING] Discarding corrupted properties from file: bgm=%u, fight_bgm=%u, lightLevel=%d (using vanilla instead)\n", bgm, fight_bgm, lightLevel);
                        }
                    }
                }
                std::fprintf(stderr, "[LEVEL EDITOR] Loaded persistent edited level for area 0x%02X room 0x%02X\n", area, room);
            }
        }

        // 5. Compile and render the map layers to subTileMap (gMapDataBottomSpecial / gMapDataTopSpecial)
        RenderMapLayerToSubTileMap(gMapDataBottomSpecial, &gMapBottom);
        RenderMapLayerToSubTileMap(gMapDataTopSpecial, &gMapTop);

        // 6. Force rendering of the loaded map tiles into visual backgrounds BG1 & BG2
        // gUpdateVisibleTiles = 1;
        // UpdateScrollVram();
    }

    // Load custom entities from Minish Maker project files if they exist
    
    // Clear old data first
    std::fprintf(stderr, "[LEVEL EDITOR LOG] Clearing custom entity vectors...\n");
    sCustomList1Data.clear();
    sCustomList2Data.clear();
    sCustomList3Data.clear();
    sCustomChestData.clear();
    sCustomWarpData.clear();
    std::fprintf(stderr, "[LEVEL EDITOR LOG] Custom entity vectors cleared. Probing entity files...\n");

    bool hasCustomEntities = false;
    char path1[256], path2[256], path3[256], pathChest[256], pathWarp[256];
    
    // Probing paths
    std::snprintf(path1, sizeof(path1), "Areas/Area %02X/Room %02X/list1DataDat.bin", area, room);
    std::snprintf(path2, sizeof(path2), "Areas/Area %02X/Room %02X/list2DataDat.bin", area, room);
    std::snprintf(path3, sizeof(path3), "Areas/Area %02X/Room %02X/list3DataDat.bin", area, room);
    std::snprintf(pathChest, sizeof(pathChest), "Areas/Area %02X/Room %02X/chestDataDat.bin", area, room);
    std::snprintf(pathWarp, sizeof(pathWarp), "Areas/Area %02X/Room %02X/warpDataDat.bin", area, room);
    
    std::ifstream f1(path1, std::ios::binary);
    std::ifstream f2(path2, std::ios::binary);
    std::ifstream f3(path3, std::ios::binary);
    std::ifstream fc(pathChest, std::ios::binary);
    std::ifstream fw(pathWarp, std::ios::binary);
    
    if (f1.good() || f2.good() || f3.good() || fc.good() || fw.good()) {
        hasCustomEntities = true;
    } else {
        // Try fallback build/pc/ path
        std::snprintf(path1, sizeof(path1), "build/pc/Areas/Area %02X/Room %02X/list1DataDat.bin", area, room);
        std::snprintf(path2, sizeof(path2), "build/pc/Areas/Area %02X/Room %02X/list2DataDat.bin", area, room);
        std::snprintf(path3, sizeof(path3), "build/pc/Areas/Area %02X/Room %02X/list3DataDat.bin", area, room);
        std::snprintf(pathChest, sizeof(pathChest), "build/pc/Areas/Area %02X/Room %02X/chestDataDat.bin", area, room);
        std::snprintf(pathWarp, sizeof(pathWarp), "build/pc/Areas/Area %02X/Room %02X/warpDataDat.bin", area, room);
        
        f1.close(); f1.open(path1, std::ios::binary);
        f2.close(); f2.open(path2, std::ios::binary);
        f3.close(); f3.open(path3, std::ios::binary);
        fc.close(); fc.open(pathChest, std::ios::binary);
        fw.close(); fw.open(pathWarp, std::ios::binary);
        
        if (f1.good() || f2.good() || f3.good() || fc.good() || fw.good()) {
            hasCustomEntities = true;
        }
    }

    if (hasCustomEntities) {
        static const char sEmptyList[] = { (char)0xFF };
        static const char sEmptyChest[] = { 0, 0, 0, 0, 0, 0, 0, 0 };

        // List 1
        if (f1.good()) {
            sCustomList1Data.assign(std::istreambuf_iterator<char>(f1), std::istreambuf_iterator<char>());
            gRoomVars.properties[1] = sCustomList1Data.data();
            std::fprintf(stderr, "[LEVEL EDITOR] Loaded list1Data (%zu bytes) from %s\n", sCustomList1Data.size(), path1);
        } else {
            gRoomVars.properties[1] = (void*)sEmptyList;
            std::fprintf(stderr, "[LEVEL EDITOR] Cleared list1Data (using empty fallback)\n");
        }

        // List 2
        if (f2.good()) {
            sCustomList2Data.assign(std::istreambuf_iterator<char>(f2), std::istreambuf_iterator<char>());
            gRoomVars.properties[0] = sCustomList2Data.data();
            std::fprintf(stderr, "[LEVEL EDITOR] Loaded list2Data (%zu bytes) from %s\n", sCustomList2Data.size(), path2);
        } else {
            gRoomVars.properties[0] = (void*)sEmptyList;
            std::fprintf(stderr, "[LEVEL EDITOR] Cleared list2Data (using empty fallback)\n");
        }

        // List 3
        if (f3.good()) {
            sCustomList3Data.assign(std::istreambuf_iterator<char>(f3), std::istreambuf_iterator<char>());
            gRoomVars.properties[2] = sCustomList3Data.data();
            std::fprintf(stderr, "[LEVEL EDITOR] Loaded list3Data (%zu bytes) from %s\n", sCustomList3Data.size(), path3);
        } else {
            gRoomVars.properties[2] = (void*)sEmptyList;
            std::fprintf(stderr, "[LEVEL EDITOR] Cleared list3Data (using empty fallback)\n");
        }

        // Chest Data
        if (fc.good()) {
            sCustomChestData.assign(std::istreambuf_iterator<char>(fc), std::istreambuf_iterator<char>());
            gRoomVars.properties[3] = sCustomChestData.data();
            std::fprintf(stderr, "[LEVEL EDITOR] Loaded chestData (%zu bytes) from %s\n", sCustomChestData.size(), pathChest);
        } else {
            gRoomVars.properties[3] = (void*)sEmptyChest;
            std::fprintf(stderr, "[LEVEL EDITOR] Cleared chestData (using empty fallback)\n");
        }

        // Warp Data (Exits)
        if (fw.good()) {
            sCustomWarpData.assign(std::istreambuf_iterator<char>(fw), std::istreambuf_iterator<char>());
            RoomResInfo* info = GetCurrentRoomInfo();
            if (info != nullptr) {
                info->exits = reinterpret_cast<const Transition*>(sCustomWarpData.data());
                std::fprintf(stderr, "[LEVEL EDITOR] Loaded warpData (%zu bytes) from %s\n", sCustomWarpData.size(), pathWarp);
            }
        }
    }
}
