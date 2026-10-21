/*
 * port/port_macos_bundle.c — run from Picori.app. See port_macos_bundle.h.
 *
 * Deliberately SDL-free: it runs before SDL is initialised and before the
 * config loader touches config.json.
 */
#include "port_macos_bundle.h"

#if defined(__APPLE__) && !defined(__ANDROID__)

#include <errno.h>
#include <limits.h>
#include <mach-o/dyld.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define DATA_DIR_NAME "Picori"
#define LOG_NAME "picori.log"

static char sDataDir[PATH_MAX];

static int EndsWith(const char* s, const char* suffix) {
    size_t n = strlen(s), m = strlen(suffix);
    return n >= m && strcmp(s + n - m, suffix) == 0;
}

/* mkdir -p for an absolute path. */
static int MakeDirs(const char* path) {
    char buf[PATH_MAX];
    if (snprintf(buf, sizeof(buf), "%s", path) >= (int)sizeof(buf))
        return -1;
    for (char* p = buf + 1; *p; ++p) {
        if (*p != '/')
            continue;
        *p = '\0';
        if (mkdir(buf, 0755) != 0 && errno != EEXIST)
            return -1;
        *p = '/';
    }
    return (mkdir(buf, 0755) == 0 || errno == EEXIST) ? 0 : -1;
}

/* Contents/ of the enclosing .app, or 0 when the binary is not in one. */
static int FindBundleContents(char* out, size_t out_len) {
    uint32_t size = 0;
    _NSGetExecutablePath(NULL, &size);
    char* raw = (char*)malloc(size);
    if (!raw)
        return 0;
    char resolved[PATH_MAX];
    int ok = _NSGetExecutablePath(raw, &size) == 0 && realpath(raw, resolved) != NULL;
    free(raw);
    if (!ok)
        return 0;
    char* slash = strrchr(resolved, '/'); /* strip binary name */
    if (!slash)
        return 0;
    *slash = '\0';
    if (!EndsWith(resolved, "/Contents/MacOS"))
        return 0;
    resolved[strlen(resolved) - strlen("/MacOS")] = '\0'; /* -> .../X.app/Contents */
    char* contents = strrchr(resolved, '/');
    if (!contents)
        return 0;
    *contents = '\0';
    int is_app = EndsWith(resolved, ".app");
    *contents = '/';
    if (!is_app)
        return 0;
    return snprintf(out, out_len, "%s", resolved) < (int)out_len;
}

static const char* HomeDir(void) {
    const char* home = getenv("HOME");
    if (home && home[0])
        return home;
    struct passwd* pw = getpwuid(getuid());
    return pw ? pw->pw_dir : NULL;
}

/* Copy Resources/<rel> to ./<rel> unless the user already has one. */
static void StageDefault(const char* resources, const char* rel) {
    struct stat st;
    if (stat(rel, &st) == 0)
        return;
    char src[PATH_MAX];
    if (snprintf(src, sizeof(src), "%s/%s", resources, rel) >= (int)sizeof(src))
        return;
    FILE* in = fopen(src, "rb");
    if (!in)
        return; /* not shipped in this bundle */
    const char* slash = strrchr(rel, '/');
    if (slash) {
        char dir[PATH_MAX];
        snprintf(dir, sizeof(dir), "%s/%.*s", sDataDir, (int)(slash - rel), rel);
        MakeDirs(dir);
    }
    char tmp[PATH_MAX];
    snprintf(tmp, sizeof(tmp), "%s.tmp", rel);
    FILE* out = fopen(tmp, "wb");
    int ok = out != NULL;
    char buf[64 * 1024];
    size_t n;
    while (ok && (n = fread(buf, 1, sizeof(buf), in)) > 0)
        ok = fwrite(buf, 1, n, out) == n;
    ok = ok && !ferror(in);
    fclose(in);
    if (out && fclose(out) != 0)
        ok = 0;
    if (ok && rename(tmp, rel) == 0)
        return;
    remove(tmp); /* retry the copy on the next launch */
}

int Port_MacBundle_Init(void) {
    char contents[PATH_MAX];
    if (!FindBundleContents(contents, sizeof(contents)))
        return 0;
    const char* home = HomeDir();
    if (!home)
        return 0;
    if (snprintf(sDataDir, sizeof(sDataDir), "%s/Library/Application Support/" DATA_DIR_NAME, home) >=
            (int)sizeof(sDataDir) ||
        MakeDirs(sDataDir) != 0 || chdir(sDataDir) != 0) {
        sDataDir[0] = '\0';
        return 0;
    }

    /* Finder-launched apps have no terminal; keep the log for bug reports.
     * Launching Picori.app/Contents/MacOS/tmc_pc from a shell still logs to
     * the terminal. */
    if (!isatty(STDERR_FILENO)) {
        rename(LOG_NAME, LOG_NAME ".1");
        if (freopen(LOG_NAME, "w", stderr)) {
            setvbuf(stderr, NULL, _IOLBF, 0);
            dup2(STDERR_FILENO, STDOUT_FILENO);
            setvbuf(stdout, NULL, _IOLBF, 0);
        }
    }

    char resources[PATH_MAX];
    snprintf(resources, sizeof(resources), "%s/Resources", contents);
    StageDefault(resources, "voxel_shapes.json");
    StageDefault(resources, "assets/rando/default.logic");

    fprintf(stderr, "[macos] app bundle: data dir %s\n", sDataDir);
    return 1;
}

const char* Port_MacBundle_DataDir(void) {
    return sDataDir[0] ? sDataDir : NULL;
}

#endif /* __APPLE__ && !__ANDROID__ */
