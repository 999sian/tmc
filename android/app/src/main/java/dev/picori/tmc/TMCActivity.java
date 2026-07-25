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

    // The panel tracks onStart/onStop, not onResume/onPause. onPause also
    // fires for transient interruptions — permission dialogs, system popups,
    // display config changes — where tearing the Presentation down just makes
    // the bottom screen flash to the launcher and back (a zelda3-android
    // lesson from the same hardware). onStop is the real "user left the app"
    // boundary, and it is also SDL's own: this SDLActivity glue pauses the
    // native thread from onStop/onStart when mHasMultiWindow is set (API 24+,
    // i.e. every device we target), so the panel's Surface comes and goes in
    // step with the game that feeds it. Keeping the panel up past onStop
    // would leave the native painter (a free-running ~20 Hz thread, see
    // port/port_second_screen.c) redrawing a frozen game in the background.
    @Override
    protected void onStart() {
        super.onStart();
        mSecondScreen.start();
    }

    @Override
    protected void onStop() {
        mSecondScreen.stop();
        super.onStop();
    }
}
