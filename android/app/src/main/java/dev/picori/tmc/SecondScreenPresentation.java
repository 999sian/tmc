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
