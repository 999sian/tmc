/*
 * port/port_rom_picker.c — SDL3 file-picker fallback for "no ROM
 * found." Lets the user point at any .gba file on disk without
 * having to manually rename and place it. We validate the picked
 * file is actually a Minish Cap ROM (16 MiB exactly + BZM[EPJ]
 * region code at offset 0xAC) before accepting it, then copy it
 * next to the running executable as baserom.gba so subsequent
 * launches skip the prompt entirely.
 */
#define _DEFAULT_SOURCE 1   /* readlink(2) on glibc */
#define _BSD_SOURCE 1
#define _POSIX_C_SOURCE 200809L

#include <SDL3/SDL.h>
#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_messagebox.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#define _GNU_SOURCE
#include <limits.h>
#include <unistd.h>
#include <sys/types.h>
#endif

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

/* Outcome of the async picker callback. Polled by the main thread. */
typedef enum {
    PICK_PENDING = 0,
    PICK_CANCELLED,
    PICK_SUCCESS,
    PICK_FAILED,
} PickStatus;

static volatile PickStatus sPickStatus = PICK_PENDING;
static char sPickPath[4096];

static void SDLCALL RomPickerCallback(void* userdata,
                                       const char* const* filelist,
                                       int filter)
{
    (void)userdata; (void)filter;
    if (!filelist) {
        sPickStatus = PICK_FAILED;
        return;
    }
    if (!filelist[0]) {
        sPickStatus = PICK_CANCELLED;
        return;
    }
    strncpy(sPickPath, filelist[0], sizeof(sPickPath) - 1);
    sPickPath[sizeof(sPickPath) - 1] = '\0';
    sPickStatus = PICK_SUCCESS;
}

/* Validate that `path` is a real TMC ROM. Returns "USA" / "EU" / "JP"
 * for the recognised region codes, NULL otherwise. */
static const char* ValidateRom(const char* path) {
    FILE* fp = fopen(path, "rb");
    if (!fp) return NULL;
    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return NULL; }
    long sz = ftell(fp);
    /* TMC ROMs are exactly 16 MiB. Reject anything wildly different
     * (we tolerate a small slop band in case a PC-side header was
     * appended by a flashcart). */
    if (sz < 0x00FF0000 || sz > 0x01010000) { fclose(fp); return NULL; }

    char gamecode[5] = {0};
    if (fseek(fp, 0xAC, SEEK_SET) != 0) { fclose(fp); return NULL; }
    if (fread(gamecode, 1, 4, fp) != 4) { fclose(fp); return NULL; }
    fclose(fp);

    if (strcmp(gamecode, "BZME") == 0) return "USA";
    if (strcmp(gamecode, "BZMP") == 0) return "EU";
    if (strcmp(gamecode, "BZMJ") == 0) return "JP";
    return NULL;
}

static int GetExeDir(char* out, size_t out_len) {
#ifdef _WIN32
    DWORD n = GetModuleFileNameA(NULL, out, (DWORD)out_len);
    if (n == 0 || n >= out_len) return -1;
    char* slash = strrchr(out, '\\');
    if (!slash) slash = strrchr(out, '/');
    if (!slash) return -1;
    *slash = '\0';
    return 0;
#elif defined(__APPLE__)
    uint32_t sz = (uint32_t)out_len;
    if (_NSGetExecutablePath(out, &sz) != 0) return -1;
    char* slash = strrchr(out, '/');
    if (!slash) return -1;
    *slash = '\0';
    return 0;
#else
    ssize_t n = readlink("/proc/self/exe", out, out_len - 1);
    if (n <= 0) return -1;
    out[n] = '\0';
    char* slash = strrchr(out, '/');
    if (!slash) return -1;
    *slash = '\0';
    return 0;
#endif
}

static int CopyFile_ToPath(const char* src, const char* dst) {
    FILE* in = fopen(src, "rb");
    if (!in) return -1;
    FILE* out = fopen(dst, "wb");
    if (!out) { fclose(in); return -1; }
    char buf[65536];
    size_t n;
    int rc = 0;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) { rc = -1; break; }
    }
    fclose(in);
    fclose(out);
    return rc;
}

/* Returns 0 if the user picked a valid ROM and we installed it
 * as <exe-dir>/baserom.gba (so the caller can re-run
 * Port_FindBaseRomPath and find it). Returns -1 on cancel/error. */
int Port_RomPicker_PromptAndInstall(void)
{
    /* Heads-up message box before the dialog so the user knows what
     * they're picking and why. Otherwise the file picker just appears
     * with no context. */
    SDL_MessageBoxData info = {0};
    info.flags = SDL_MESSAGEBOX_INFORMATION;
    info.window = NULL;
    info.title = "Minish Cap PC Port — ROM needed";
    info.message =
        "We couldn't find a Minish Cap ROM next to tmc_pc.\n\n"
        "Pick the .gba file from disk on the next screen — we'll\n"
        "validate it's the right ROM and copy it into place so\n"
        "you don't have to do this again.\n\n"
        "Accepted regions: USA (BZME), EU (BZMP), JP (BZMJ).";
    SDL_MessageBoxButtonData buttons[] = {
        { SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 0, "Pick ROM…" },
        { SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 1, "Cancel" },
    };
    info.buttons = buttons;
    info.numbuttons = 2;
    int btn = -1;
    if (!SDL_ShowMessageBox(&info, &btn) || btn != 0) {
        return -1;
    }

    SDL_DialogFileFilter filters[] = {
        { "Game Boy Advance ROM", "gba" },
        { "All files", "*" },
    };

    sPickStatus = PICK_PENDING;
    sPickPath[0] = '\0';
    SDL_ShowOpenFileDialog(RomPickerCallback, NULL, NULL,
                           filters, 2, NULL, false);

    /* Block until the user picks (or cancels). The picker is async on
     * every backend — pump events here so platform-native code can
     * deliver the callback. */
    while (sPickStatus == PICK_PENDING) {
        SDL_PumpEvents();
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_EVENT_QUIT) {
                sPickStatus = PICK_CANCELLED;
                break;
            }
        }
        SDL_Delay(16);
    }

    if (sPickStatus != PICK_SUCCESS) {
        return -1;
    }

    const char* region = ValidateRom(sPickPath);
    if (!region) {
        char msg[1024];
        snprintf(msg, sizeof(msg),
                 "That file doesn't look like a Minish Cap ROM:\n\n%s\n\n"
                 "Expected a 16 MiB .gba with region code BZME (USA),\n"
                 "BZMP (EU), or BZMJ (JP).",
                 sPickPath);
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,
                                 "Wrong ROM", msg, NULL);
        return -1;
    }

    /* Copy the picked file next to the exe as baserom.gba (overwrite
     * if already exists). Once that's done, Port_FindBaseRomPath
     * picks it up via the usual candidate list. */
    char exedir[4096];
    if (GetExeDir(exedir, sizeof(exedir)) != 0) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,
                                 "Install failed",
                                 "Could not locate this executable's directory.", NULL);
        return -1;
    }
    char dst[4096];
    snprintf(dst, sizeof(dst), "%s%cbaserom.gba",
             exedir,
#ifdef _WIN32
             '\\'
#else
             '/'
#endif
             );

    if (CopyFile_ToPath(sPickPath, dst) != 0) {
        char msg[1024];
        snprintf(msg, sizeof(msg),
                 "Could not write %s. Check disk permissions.", dst);
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,
                                 "Install failed", msg, NULL);
        return -1;
    }

    fprintf(stderr, "[romPicker] installed %s ROM → %s\n", region, dst);
    return 0;
}
