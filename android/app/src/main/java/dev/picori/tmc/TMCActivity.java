package dev.picori.tmc;

import android.os.Bundle;
import android.view.View;

import org.libsdl.app.SDLActivity;

/**
 * Project Picori — The Minish Cap PC port, Android shell.
 *
 * The entire game (engine + PPU + audio + ImGui menus + touch overlay) lives
 * in libmain.so, built by xmake (repo root: `xmake f -p android ...`). SDL3 is
 * statically linked into it, so the only library to load is "main"; SDL's
 * Java-side glue binds to the JNI_OnLoad exported from the static SDL inside.
 */
public class TMCActivity extends SDLActivity {
    // The set SDL's own glue applies for a fullscreen window.
    private static final int IMMERSIVE_FLAGS =
            View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
            | View.SYSTEM_UI_FLAG_FULLSCREEN
            | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
            | View.SYSTEM_UI_FLAG_LAYOUT_STABLE
            | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
            | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION;

    @Override
    protected String[] getLibraries() {
        return new String[] { "main" };
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        // SDL only puts the immersive flags on the decor view when it
        // handles COMMAND_CHANGE_WINDOW_STYLE, so a window created
        // fullscreen can still show the status bar whenever there are
        // notifications. Set them here instead.
        getWindow().getDecorView().setSystemUiVisibility(IMMERSIVE_FLAGS);
        // Arms SDL's own re-hide for a bar raised mid-game (a notification
        // arriving), which causes no focus change. Safe: Android has no
        // windowed mode.
        mFullscreenModeActive = true;
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        // The system drops the flags on every focus loss (notification
        // shade, power menu) and SDL's glue does not restore them.
        if (hasFocus) {
            getWindow().getDecorView().setSystemUiVisibility(IMMERSIVE_FLAGS);
        }
    }
}
