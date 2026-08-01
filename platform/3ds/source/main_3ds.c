#include "platform_3ds.h"

#include "port_audio.h"
#include "port_ppu.h"
#include "port_rom.h"
#include "port_runtime_config.h"

#include <errno.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#define APP_DIR "sdmc:/3ds/The Minish Cap 3DS"

extern void AgbMain(void);

static int PrepareStorage(void) {
    mkdir("sdmc:/3ds", 0777);
    if (mkdir(APP_DIR, 0777) != 0 && errno != EEXIST) return 0;
    return chdir(APP_DIR) == 0;
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    if (!Platform3DS_Init()) return 1;
    Platform3DS_ShowSplash();

    printf("The Minish Cap 3DS v0.1.0\n\n");
    printf("Preparing storage...\n");
    if (!PrepareStorage()) {
        Platform3DS_ShowFatal("Storage error", "Could not open " APP_DIR ".");
        Platform3DS_Shutdown();
        return 1;
    }

    FILE* rom = fopen("baserom.gba", "rb");
    if (!rom) {
        Platform3DS_ShowFatal(
            "ROM not found",
            "Copy your clean USA ROM to:\n"
            APP_DIR "/baserom.gba\n\n"
            "Expected SHA-1:\n"
            "b4bd50e4131b027c334547b4524e2dbbd4227130");
        Platform3DS_Shutdown();
        return 1;
    }
    fclose(rom);

    printf("Loading ROM and tables...\n");
    Port_Config_Load("tmc3ds.ini");
    Port_LoadRom("baserom.gba");
    Port_PPU_Init(NULL);
    if (!Port_Audio_Init()) {
        printf("Warning: audio is unavailable.\n");
    }

    printf("Starting engine...\n");
    AgbMain();

    Port_PPU_Shutdown();
    Platform3DS_Shutdown();
    return 0;
}
