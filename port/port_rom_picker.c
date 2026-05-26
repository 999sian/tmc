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
#include <stdint.h>
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

/* ------------------------------------------------------------------
 * SHA-1 — minimal RFC 3174 reference impl, public-domain style.
 * Used to identify a picked .gba file by hash rather than filename,
 * so the user can name their ROM anything and we still recognise
 * a valid TMC dump.
 * ------------------------------------------------------------------ */
typedef struct {
    uint32_t state[5];
    uint64_t bitcount;
    uint8_t  buffer[64];
    uint8_t  buflen;
} SHA1_CTX;

static uint32_t sha1_rol(uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }

static void sha1_init(SHA1_CTX* c) {
    c->state[0] = 0x67452301u;
    c->state[1] = 0xEFCDAB89u;
    c->state[2] = 0x98BADCFEu;
    c->state[3] = 0x10325476u;
    c->state[4] = 0xC3D2E1F0u;
    c->bitcount = 0;
    c->buflen = 0;
}

static void sha1_transform(SHA1_CTX* c, const uint8_t block[64]) {
    uint32_t w[80];
    for (int i = 0; i < 16; ++i) {
        w[i] = ((uint32_t)block[i * 4] << 24) |
               ((uint32_t)block[i * 4 + 1] << 16) |
               ((uint32_t)block[i * 4 + 2] << 8) |
               ((uint32_t)block[i * 4 + 3]);
    }
    for (int i = 16; i < 80; ++i) {
        w[i] = sha1_rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    }
    uint32_t a = c->state[0], b = c->state[1], cc = c->state[2];
    uint32_t d = c->state[3], e = c->state[4];
    for (int i = 0; i < 80; ++i) {
        uint32_t f, k;
        if (i < 20)      { f = (b & cc) | ((~b) & d);              k = 0x5A827999u; }
        else if (i < 40) { f = b ^ cc ^ d;                          k = 0x6ED9EBA1u; }
        else if (i < 60) { f = (b & cc) | (b & d) | (cc & d);       k = 0x8F1BBCDCu; }
        else             { f = b ^ cc ^ d;                          k = 0xCA62C1D6u; }
        uint32_t t = sha1_rol(a, 5) + f + e + k + w[i];
        e = d; d = cc; cc = sha1_rol(b, 30); b = a; a = t;
    }
    c->state[0] += a; c->state[1] += b; c->state[2] += cc;
    c->state[3] += d; c->state[4] += e;
}

static void sha1_update(SHA1_CTX* c, const uint8_t* data, size_t len) {
    c->bitcount += (uint64_t)len * 8;
    while (len > 0) {
        size_t want = 64 - c->buflen;
        size_t take = len < want ? len : want;
        memcpy(c->buffer + c->buflen, data, take);
        c->buflen += (uint8_t)take;
        data += take;
        len  -= take;
        if (c->buflen == 64) {
            sha1_transform(c, c->buffer);
            c->buflen = 0;
        }
    }
}

static void sha1_final(SHA1_CTX* c, uint8_t out[20]) {
    uint64_t bits = c->bitcount;
    uint8_t pad = 0x80;
    sha1_update(c, &pad, 1);
    pad = 0x00;
    while (c->buflen != 56) sha1_update(c, &pad, 1);
    uint8_t lenbe[8];
    for (int i = 0; i < 8; ++i) lenbe[i] = (uint8_t)(bits >> (56 - i * 8));
    sha1_update(c, lenbe, 8);
    for (int i = 0; i < 5; ++i) {
        out[i * 4]     = (uint8_t)(c->state[i] >> 24);
        out[i * 4 + 1] = (uint8_t)(c->state[i] >> 16);
        out[i * 4 + 2] = (uint8_t)(c->state[i] >> 8);
        out[i * 4 + 3] = (uint8_t)(c->state[i]);
    }
}

static int Sha1HexOfFile(const char* path, char hex_out[41]) {
    FILE* fp = fopen(path, "rb");
    if (!fp) return -1;
    SHA1_CTX c;
    sha1_init(&c);
    uint8_t buf[1 << 16];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
        sha1_update(&c, buf, n);
    }
    fclose(fp);
    uint8_t digest[20];
    sha1_final(&c, digest);
    static const char kHex[] = "0123456789abcdef";
    for (int i = 0; i < 20; ++i) {
        hex_out[i * 2]     = kHex[digest[i] >> 4];
        hex_out[i * 2 + 1] = kHex[digest[i] & 0xF];
    }
    hex_out[40] = '\0';
    return 0;
}

/* Known TMC ROM SHA-1 hashes. The picker recognises a file as a TMC
 * ROM if its hash matches any entry here — filename is irrelevant. */
static const struct {
    const char* hex;
    const char* region;
} kKnownTmcRoms[] = {
    { "b4bd50e4131b027c334547b4524e2dbbd4227130", "USA"      },
    { "cff199b36ff173fb6faf152653d1bccf87c26fb7", "EU"       },
    { "6c5404a1effb17f481f352181d0f1c61a2765c5d", "JP"       },
    { "63fcad218f9047b6a9edbb68c98bd0dec322d7a1", "USA Demo" },
    { "9cdb56fa79bba13158b81925c1f3641251326412", "JP Demo"  },
};

/* Validate that `path` is a real TMC ROM by SHA-1 hash. Returns the
 * region tag ("USA" / "EU" / "JP" / "USA Demo" / "JP Demo") or NULL.
 *
 * Filename is intentionally irrelevant — users pick whatever they
 * named their dump. A fast file-size pre-check (16 MiB ± slop) rules
 * out obvious junk before we pay for the SHA-1 of a 16 MB read. */
static const char* ValidateRom(const char* path) {
    FILE* fp = fopen(path, "rb");
    if (!fp) return NULL;
    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return NULL; }
    long sz = ftell(fp);
    fclose(fp);
    /* Most TMC dumps are exactly 16 MiB (0x01000000); the demo dumps
     * are 4 MiB. Reject everything outside a generous envelope. */
    if (sz < 0x00200000 || sz > 0x01010000) return NULL;

    char hex[41];
    if (Sha1HexOfFile(path, hex) != 0) return NULL;
    for (size_t i = 0; i < sizeof(kKnownTmcRoms) / sizeof(kKnownTmcRoms[0]); ++i) {
        if (strcmp(hex, kKnownTmcRoms[i].hex) == 0) {
            return kKnownTmcRoms[i].region;
        }
    }
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
#ifdef _WIN32
    /* Win32 CopyFileA accepts narrow strings in the active code page,
     * which matches what SDL's open-file callback hands us, and it
     * handles Unicode paths internally. Strictly more reliable than
     * fopen/fread/fwrite for the rom-copy step on Windows. */
    return CopyFileA(src, dst, FALSE) ? 0 : -1;
#else
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
#endif
}

/* Returns 0 if the user picked a valid ROM and we installed it as
 * <exe-dir>/baserom.gba (so the caller can re-run
 * Port_FindBaseRomPath and find it). Returns -1 on cancel/error.
 *
 * The prelaunch UI shows the user-facing "Pick your ROM" prompt
 * before calling this, so we skip straight to the file dialog. */
int Port_RomPicker_PromptAndInstall(void)
{
    fprintf(stderr, "[rom-picker] opening SDL file dialog...\n");

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
        /* Compute the hash again for the error message so the user
         * can compare against the expected SHA-1s. */
        char hex[41] = {0};
        if (Sha1HexOfFile(sPickPath, hex) != 0) {
            snprintf(hex, sizeof(hex), "(could not read file)");
        }
        char msg[2048];
        snprintf(msg, sizeof(msg),
                 "That file's SHA-1 doesn't match any known TMC ROM:\n\n"
                 "  Path:     %s\n"
                 "  SHA-1:    %s\n\n"
                 "Accepted hashes:\n"
                 "  USA      b4bd50e4131b027c334547b4524e2dbbd4227130\n"
                 "  EU       cff199b36ff173fb6faf152653d1bccf87c26fb7\n"
                 "  JP       6c5404a1effb17f481f352181d0f1c61a2765c5d\n"
                 "  USA Demo 63fcad218f9047b6a9edbb68c98bd0dec322d7a1\n"
                 "  JP Demo  9cdb56fa79bba13158b81925c1f3641251326412\n\n"
                 "If your dump matches one of these by hash but the picker\n"
                 "still rejects it, something's mangling the bytes on disk.",
                 sPickPath, hex);
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
