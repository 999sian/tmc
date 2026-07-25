#ifndef PORT_SECOND_SCREEN_H
#define PORT_SECOND_SCREEN_H

/*
 * Second-screen panel (AYN Thor secondary display) — Phase 3a bring-up.
 *
 * This is a genuinely independent render target from the game's own PPU:
 * it never touches virtuappu_frame_buffer / mode1_memory / OAM (see the
 * cautionary note in port_softslots.h about an earlier native-framebuffer
 * UI attempt that corrupted pause-menu visuals). The Java side
 * (SecondScreenPresentation.java) owns a Presentation on the secondary
 * Display and hands its Surface to this module via
 * port_second_screen_jni.cpp; everything below is platform-agnostic C so
 * desktop builds compile this file too (as a no-op — there is no second
 * display to speak of there).
 */

#ifdef __cplusplus
extern "C" {
#endif

void Port_SecondScreen_Init(void);

/* Called from JNI when the Presentation's Surface becomes available (or is
 * resized). `window` is an ANativeWindow* on Android; kept as void* here so
 * this header doesn't require <android/native_window.h> to include. Takes
 * ownership of the one ANativeWindow reference the caller obtained via
 * ANativeWindow_fromSurface — releases it internally, exactly once, when
 * replaced or when the surface is lost. */
void Port_SecondScreen_OnSurfaceReady(void* window, int width, int height);

/* Called from JNI when the Presentation's Surface is destroyed (display
 * detached, app backgrounded, etc). Safe to call even if no surface is
 * currently held. */
void Port_SecondScreen_OnSurfaceLost(void);

/* Called from JNI on a completed tap on the second screen, in surface
 * pixel coordinates. longPress selects the B slot (tap = A, hold = B),
 * matching the pause menu's A/B assignment. Hit-testing happens against
 * the layout of the most recently painted frame; a tap that lands on an
 * item cell files an equip request through
 * Port_SecondScreenState_RequestEquip. */
void Port_SecondScreen_OnTap(int x, int y, int longPress);

#ifdef __cplusplus
}
#endif

#endif /* PORT_SECOND_SCREEN_H */
