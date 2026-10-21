#ifndef PORT_MACOS_BUNDLE_H
#define PORT_MACOS_BUNDLE_H

/* macOS .app bundle support.
 *
 * The port keeps all of its state (config.json, saves, the extracted asset
 * cache, quicksaves, bug reports, the installed baserom.gba) relative to the
 * CWD and the executable's directory. Neither works from Picori.app: Finder
 * launches with CWD = /, and writing into the bundle breaks its code
 * signature. When running from <name>.app/Contents/MacOS, Port_MacBundle_Init
 * mirrors the Android approach: chdir into
 * ~/Library/Application Support/Picori, stage the read-only defaults shipped
 * in Contents/Resources there, and send stdout/stderr to picori.log (there is
 * no terminal to see them in).
 *
 * Outside a bundle (the plain release tarball, dev builds) nothing changes. */

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__APPLE__) && !defined(__ANDROID__)

/* Call first thing in main(), before any file is opened. Returns 1 when
 * running from an app bundle (and the data dir is now the CWD). */
int Port_MacBundle_Init(void);

/* Absolute path of the per-user data dir, or NULL when not bundled. */
const char* Port_MacBundle_DataDir(void);

#else

static inline int Port_MacBundle_Init(void) {
    return 0;
}

static inline const char* Port_MacBundle_DataDir(void) {
    return 0;
}

#endif

#ifdef __cplusplus
}
#endif

#endif /* PORT_MACOS_BUNDLE_H */
