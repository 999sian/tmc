package dev.picori.tmc;

import android.os.Bundle;
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
    private SecondScreenManager mSecondScreen;

    @Override
    protected String[] getLibraries() {
        return new String[] { "main" };
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        mSecondScreen = new SecondScreenManager(this);
    }

    @Override
    protected void onResume() {
        super.onResume();
        mSecondScreen.start();
    }

    @Override
    protected void onPause() {
        mSecondScreen.stop();
        super.onPause();
    }
}
