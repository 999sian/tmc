package dev.picori.tmc;

import android.app.Presentation;
import android.content.Context;
import android.graphics.PixelFormat;
import android.os.Bundle;
import android.view.Display;
import android.view.MotionEvent;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.Window;
import android.view.WindowManager;
import android.widget.FrameLayout;

/**
 * Hosts the game's second-screen panel (map/inventory, later phases) on
 * Thor's secondary display. Owns a single SurfaceView whose native Surface
 * is handed to libmain.so via JNI so C++ can draw into it directly — this
 * is a separate window/surface from SDLActivity's primary SDLSurface,
 * deliberately not routed through SDL's own event/render pipeline (see
 * port/port_second_screen.c and port/port_second_screen_jni.cpp).
 */
public class SecondScreenPresentation extends Presentation {
    public SecondScreenPresentation(Context outerContext, Display display) {
        super(outerContext, display);
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        // A Presentation is a Dialog, and a Dialog's window is focusable by
        // default. Focusable is fatal here: Android gives key input to one
        // focused window, so the first touch on the bottom screen would move
        // focus to this panel and the gamepad would stop reaching SDL's
        // window on the game screen. Worse, gamepad B has a system-level
        // fallback to KEYCODE_BACK, and BACK cancels a focused dialog — the
        // bottom screen would vanish mid-game on a normal B press (both
        // shipped as user-reported bugs in the zelda3-android mod on the same
        // hardware). Non-focusable windows still receive touch, so the
        // tap-to-equip listener below keeps working. Must be set before
        // show() attaches the window; onCreate runs inside show(), which is
        // early enough.
        Window window = getWindow();
        if (window != null) {
            window.addFlags(WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE);
            // Hide the system bars on the panel: Thor's bottom display
            // otherwise reserves a ~70px navigation-bar inset that shows up
            // as a dead black band beside the surface. System-UI visibility
            // is per-window, so the game activity going immersive does
            // nothing for us — the flags have to go on this window's own
            // decor view. setSystemUiVisibility is deprecated but is the
            // mechanism that exists at minSdk 21 (WindowInsetsController
            // needs API 30).
            window.getDecorView().setSystemUiVisibility(
                    android.view.View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                    | android.view.View.SYSTEM_UI_FLAG_FULLSCREEN
                    | android.view.View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                    | android.view.View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                    | android.view.View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                    | android.view.View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION);
        }
        // Belt to FLAG_NOT_FOCUSABLE's suspenders: even if a BACK ever
        // reaches this dialog through a path the flag doesn't cover, refuse
        // to be cancelled by it.
        setCancelable(false);

        SurfaceView surfaceView = new SurfaceView(getContext());
        setContentView(surfaceView, new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT));

        // Explicit format: an unset SurfaceHolder format is device/API-level
        // dependent (can resolve to RGB_565, 2 bytes/pixel), and the native
        // side writes fixed 4-bytes/pixel ARGB — a mismatch here overflows
        // the locked buffer. Pin it so ANativeWindow_lock's buf.format is
        // always what the native writer expects.
        surfaceView.getHolder().setFormat(PixelFormat.RGBA_8888);

        // Tap-to-equip: a completed tap is forwarded to native with the
        // press duration collapsed to a boolean — short tap equips the item
        // to A, press-and-hold to B (mirrors the pause menu's two equip
        // buttons). Surface pixels == view pixels here (the SurfaceView
        // fills the display and the native side draws at surface size), so
        // event coordinates need no transform before hit-testing in C.
        surfaceView.setOnTouchListener(new android.view.View.OnTouchListener() {
            private static final long LONG_PRESS_MS = 350;
            private long mDownTime;

            @Override
            public boolean onTouch(android.view.View v, MotionEvent event) {
                switch (event.getActionMasked()) {
                    case MotionEvent.ACTION_DOWN:
                        mDownTime = event.getEventTime();
                        return true;
                    case MotionEvent.ACTION_UP:
                        boolean longPress = event.getEventTime() - mDownTime >= LONG_PRESS_MS;
                        nativeTap((int) event.getX(), (int) event.getY(), longPress);
                        v.performClick();
                        return true;
                    default:
                        return false;
                }
            }
        });

        surfaceView.getHolder().addCallback(new SurfaceHolder.Callback() {
            @Override
            public void surfaceCreated(SurfaceHolder holder) {
                // No-op: surfaceChanged always follows immediately with the
                // real dimensions, so do the native handoff there instead
                // of racing a not-yet-sized surface.
            }

            @Override
            public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
                nativeSurfaceCreated(holder.getSurface(), width, height);
            }

            @Override
            public void surfaceDestroyed(SurfaceHolder holder) {
                nativeSurfaceDestroyed();
            }
        });
    }

    private static native void nativeSurfaceCreated(Surface surface, int width, int height);
    private static native void nativeSurfaceDestroyed();
    private static native void nativeTap(int x, int y, boolean longPress);
}
