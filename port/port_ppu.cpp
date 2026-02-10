#include "port_ppu.h"

#include <VirtuaPPU.hpp>

#include <cstdint>
#include <cstdio>
#include <cstring>

// Forward-declare gMain to read current task
extern "C" {
struct Main {
    uint8_t interruptFlag;
    uint8_t sleepStatus;
    uint8_t task;
    uint8_t state;
    uint8_t substate;
    uint8_t field_0x5;
    uint8_t muteAudio;
    uint8_t field_0x7;
    uint8_t pauseFrames;
    uint8_t pauseCount;
    uint8_t pauseInterval;
    uint8_t pad;
    uint16_t ticks;
};
extern struct Main gMain;
}

static SDL_Renderer* sRenderer = nullptr;
static SDL_Texture* sTexture = nullptr;
static int sFrameCount = 0;
static bool sDumpedFileSelect = false;

// Dump framebuffer to BMP
static void DumpFrameBMP(const char* filename, const uint32_t* buf = nullptr) {
    if (!buf)
        buf = frame_buffer;
    FILE* f = fopen(filename, "wb");
    if (!f)
        return;
    int w = 240, h = 160;
    int rowBytes = w * 3;
    int padRow = (4 - (rowBytes % 4)) % 4;
    int dataSize = (rowBytes + padRow) * h;
    int fileSize = 54 + dataSize;
    // BMP header
    uint8_t hdr[54] = {};
    hdr[0] = 'B';
    hdr[1] = 'M';
    hdr[2] = fileSize;
    hdr[3] = fileSize >> 8;
    hdr[4] = fileSize >> 16;
    hdr[5] = fileSize >> 24;
    hdr[10] = 54;
    hdr[14] = 40; // info header size
    hdr[18] = w;
    hdr[19] = w >> 8;
    hdr[22] = h;
    hdr[23] = h >> 8;
    hdr[26] = 1;  // planes
    hdr[28] = 24; // bpp
    fwrite(hdr, 1, 54, f);
    uint8_t pad[3] = {};
    for (int y = h - 1; y >= 0; y--) {
        for (int x = 0; x < w; x++) {
            uint32_t px = buf[y * w + x];
            uint8_t r = px & 0xFF;
            uint8_t g = (px >> 8) & 0xFF;
            uint8_t b = (px >> 16) & 0xFF;
            uint8_t bgr[3] = { b, g, r };
            fwrite(bgr, 1, 3, f);
        }
        if (padRow)
            fwrite(pad, 1, padRow, f);
    }
    fclose(f);
    fprintf(stderr, "[PPU] Dumped frame to %s\n", filename);
}

extern "C" void Port_PPU_Init(SDL_Window* window) {
    sRenderer = SDL_CreateRenderer(window, nullptr);
    if (!sRenderer) {
        printf("Port_PPU_Init: SDL_CreateRenderer failed: %s\n", SDL_GetError());
        return;
    }

    // GBA native resolution
    sTexture = SDL_CreateTexture(sRenderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, 240, 160);
    if (!sTexture) {
        printf("Port_PPU_Init: SDL_CreateTexture failed: %s\n", SDL_GetError());
        return;
    }

    SDL_SetTextureScaleMode(sTexture, SDL_SCALEMODE_NEAREST);

    // Configure ViruaPPU registers
    global_Registers.frame_width = 240;
    global_Registers.mode = 1; // default to Mode 1 (GBA Mode 0)

    printf("PPU initialized (240x160, nearest-neighbor scaling).\n");
}

extern "C" void Port_PPU_PresentFrame(void) {
    if (!sRenderer || !sTexture)
        return;

    // Read GBA DISPCNT to pick the right PPU mode
    uint16_t dispcnt = gIoMem[0x00] | (gIoMem[0x01] << 8);
    uint8_t gbaMode = dispcnt & 0x07;

    sFrameCount++;

    switch (gbaMode) {
        case 0:
            global_Registers.mode = 1;
            break; // GBA Mode 0 → ViruaPPU Mode 1
        case 1:
            global_Registers.mode = 2;
            break; // GBA Mode 1 → ViruaPPU Mode 2
        default:
            // Modes 2-5 not implemented yet
            break;
    }

    // Render the frame into ViruaPPU's frame_buffer
    RenderFrame();

    // Dump first file select frame to BMP
    if (gMain.task == 1 && !sDumpedFileSelect) {
        // Wait 60 frames into file select for graphics to load
        static int fileSelectFrames = 0;
        fileSelectFrames++;
        if (fileSelectFrames >= 120) {
            DumpFrameBMP("fileselect_frame.bmp");
            sDumpedFileSelect = true;

            // ---- OBJ-only BMP: render sprites against black ----
            {
                uint32_t objOnlyBuf[240 * 160];
                memset(objOnlyBuf, 0, sizeof(objOnlyBuf));
                bool obj1d_diag = (dispcnt & 0x0040) != 0;
                for (int line = 0; line < 160; ++line) {
                    uint32_t objLine[240];
                    uint8_t objPriLine[240];
                    memset(objLine, 0, sizeof(objLine));
                    memset(objPriLine, 0xFF, sizeof(objPriLine));
                    Mode1::RenderObjLine(line, obj1d_diag, objLine, objPriLine);
                    for (int x = 0; x < 240; ++x) {
                        if (objLine[x] != 0)
                            objOnlyBuf[line * 240 + x] = objLine[x];
                    }
                }
                DumpFrameBMP("fileselect_obj_only.bmp", objOnlyBuf);
            }

            // Dump detailed BG/OAM info to a file
            FILE* diag = fopen("fileselect_diag.txt", "w");
            if (diag) {
                fprintf(diag, "DISPCNT=%04X BLDCNT=%04X BLDALPHA=%04X BLDY=%04X\n", dispcnt,
                        gIoMem[0x50] | (gIoMem[0x51] << 8), gIoMem[0x52] | (gIoMem[0x53] << 8),
                        gIoMem[0x54] | (gIoMem[0x55] << 8));
                for (int bg = 0; bg < 4; bg++) {
                    uint16_t cnt = gIoMem[0x08 + bg * 2] | (gIoMem[0x09 + bg * 2] << 8);
                    int charBase = ((cnt >> 2) & 3) * 0x4000;
                    int screenBase = ((cnt >> 8) & 0x1F) * 0x800;
                    int priority = cnt & 3;
                    uint16_t hofs = gIoMem[0x10 + bg * 4] | (gIoMem[0x11 + bg * 4] << 8);
                    uint16_t vofs = gIoMem[0x12 + bg * 4] | (gIoMem[0x13 + bg * 4] << 8);
                    int nonZeroTiles = 0;
                    for (int t = 0; t < 32 * 32; t++) {
                        uint16_t entry = gVram[screenBase + t * 2] | (gVram[screenBase + t * 2 + 1] << 8);
                        if (entry != 0)
                            nonZeroTiles++;
                    }
                    int nonZeroCharData = 0;
                    for (int b = 0; b < 0x4000; b++) {
                        if (gVram[charBase + b] != 0)
                            nonZeroCharData++;
                    }
                    fprintf(diag,
                            "BG%d: CNT=%04X pri=%d charBase=0x%X screenBase=0x%X "
                            "hofs=%d vofs=%d tiles=%d/%d charData=%d/16384\n",
                            bg, cnt, priority, charBase, screenBase, hofs, vofs, nonZeroTiles, 32 * 32,
                            nonZeroCharData);
                    // First 10 non-zero tilemap entries
                    fprintf(diag, "  First non-zero tiles: ");
                    int shown = 0;
                    for (int t = 0; t < 32 * 32 && shown < 10; t++) {
                        uint16_t entry = gVram[screenBase + t * 2] | (gVram[screenBase + t * 2 + 1] << 8);
                        if (entry != 0) {
                            fprintf(diag, "[%d]=%04X ", t, entry);
                            shown++;
                        }
                    }
                    fprintf(diag, "\n");
                }
                // OAM stats
                int visibleOBJ = 0, affineOBJ = 0, hiddenOBJ = 0;
                for (int i = 0; i < 128; i++) {
                    uint16_t a0 = gOamMem[i * 4];
                    bool isAffine = (a0 >> 8) & 1;
                    bool isHidden = !isAffine && ((a0 >> 9) & 1);
                    if (isHidden)
                        hiddenOBJ++;
                    else if (isAffine)
                        affineOBJ++;
                    else
                        visibleOBJ++;
                }
                fprintf(diag, "OAM: visible=%d affine=%d hidden=%d\n", visibleOBJ, affineOBJ, hiddenOBJ);
                // List evry visible OBJs
                int shown = 0;
                for (int i = 0; i < 128; i++) {
                    uint16_t a0 = gOamMem[i * 4];
                    uint16_t a1 = gOamMem[i * 4 + 1];
                    uint16_t a2 = gOamMem[i * 4 + 2];
                    bool isAffine = (a0 >> 8) & 1;
                    bool isHidden = !isAffine && ((a0 >> 9) & 1);
                    if (!isHidden) {
                        fprintf(diag, "  OBJ[%d]: attr0=%04X attr1=%04X attr2=%04X y=%d x=%d tile=%d\n", i, a0, a1, a2,
                                a0 & 0xFF, a1 & 0x1FF, a2 & 0x3FF);
                        shown++;
                    }
                }
                // OBJ VRAM occupancy
                int objVramNonZero = 0;
                for (int i = 0x10000; i < 0x18000; i++) {
                    if (gVram[i] != 0)
                        objVramNonZero++;
                }
                fprintf(diag, "OBJ VRAM (0x10000-0x17FFF): %d/32768 non-zero bytes\n", objVramNonZero);
                // OBJ palette dump for palettes used by visible OBJs
                int usedPals[] = { 7, 10, 12, 14 };
                for (int p = 0; p < 4; p++) {
                    int pal = usedPals[p];
                    fprintf(diag, "OBJ Palette %d:", pal);
                    for (int c = 0; c < 16; c++) {
                        fprintf(diag, " %04X", gObjPltt[pal * 16 + c]);
                    }
                    fprintf(diag, "\n");
                }
                // Tile data hex dump for evry visible OBJs
                shown = 0;
                for (int i = 0; i < 128; i++) {
                    uint16_t a0 = gOamMem[i * 4];
                    uint16_t a2 = gOamMem[i * 4 + 2];
                    bool isAffine = (a0 >> 8) & 1;
                    bool isHidden = !isAffine && ((a0 >> 9) & 1);
                    if (isHidden)
                        continue;
                    uint16_t tileIdx = a2 & 0x3FF;
                    uint32_t addr = 0x10000 + tileIdx * 32;
                    int nonZ = 0;
                    for (int b = 0; b < 32 && addr + b < 0x18000; b++) {
                        if (gVram[addr + b] != 0)
                            nonZ++;
                    }
                    fprintf(diag, "  OBJ[%d] tile %d VRAM[0x%05X]: %d/32 non-zero bytes", i, tileIdx, addr, nonZ);
                    if (nonZ > 0) {
                        fprintf(diag, " first8:");
                        for (int b = 0; b < 8 && addr + b < 0x18000; b++)
                            fprintf(diag, " %02X", gVram[addr + b]);
                    }
                    fprintf(diag, "\n");
                    shown++;
                }
                // VRAM occupancy breakdown
                int regions[][2] = {
                    { 0x10000, 0x12000 }, { 0x12000, 0x14000 }, { 0x14000, 0x16000 }, { 0x16000, 0x18000 }
                };
                for (int r = 0; r < 4; r++) {
                    int nz = 0;
                    for (int a = regions[r][0]; a < regions[r][1]; a++)
                        if (gVram[a] != 0)
                            nz++;
                    fprintf(diag, "VRAM[0x%05X-0x%05X]: %d/8192 non-zero bytes\n", regions[r][0], regions[r][1] - 1,
                            nz);
                }
                // Detailed tile dump for save slot sprites
                int diagTiles[] = { 512, 513, 514, 515, 576, 577, 608, 609, 664, 665 };
                for (int t = 0; t < 10; t++) {
                    int ti = diagTiles[t];
                    uint32_t addr = 0x10000 + ti * 32;
                    int nz = 0;
                    for (int b = 0; b < 32 && addr + b < 0x18000; b++)
                        if (gVram[addr + b] != 0)
                            nz++;
                    fprintf(diag, "  Tile %d [0x%05X]: %d/32 non-zero |", ti, addr, nz);
                    for (int b = 0; b < 32 && addr + b < 0x18000; b++)
                        fprintf(diag, "%02X", gVram[addr + b]);
                    fprintf(diag, "\n");
                }
                fclose(diag);
                fprintf(stderr, "[PPU] Diagnostic written to fileselect_diag.txt\n");
            }
        }
    }

    // Upload to SDL texture
    SDL_UpdateTexture(sTexture, nullptr, frame_buffer, 240 * sizeof(uint32_t));

    SDL_RenderClear(sRenderer);
    SDL_RenderTexture(sRenderer, sTexture, nullptr, nullptr);
    SDL_RenderPresent(sRenderer);
}

extern "C" void Port_PPU_Shutdown(void) {
    if (sTexture) {
        SDL_DestroyTexture(sTexture);
        sTexture = nullptr;
    }
    if (sRenderer) {
        SDL_DestroyRenderer(sRenderer);
        sRenderer = nullptr;
    }
}
