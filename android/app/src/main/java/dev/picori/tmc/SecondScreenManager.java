package dev.picori.tmc;

import android.content.Context;
import android.hardware.display.DisplayManager;
import android.util.Log;
import android.view.Display;

/**
 * Finds Thor's secondary panel (a real Android Display flagged
 * FLAG_PRESENTATION, normally owned by AYN's own launcher when this app
 * isn't targeting it) and shows/hides our SecondScreenPresentation on it as
 * the display attaches/detaches or the activity's own lifecycle changes.
 */
public class SecondScreenManager implements DisplayManager.DisplayListener {
    private static final String TAG = "SecondScreenManager";

    private final Context mContext;
    private final DisplayManager mDisplayManager;
    private SecondScreenPresentation mPresentation;

    public SecondScreenManager(Context context) {
        mContext = context.getApplicationContext();
        mDisplayManager = (DisplayManager) mContext.getSystemService(Context.DISPLAY_SERVICE);
    }

    public void start() {
        if (mDisplayManager == null) {
            return;
        }
        mDisplayManager.registerDisplayListener(this, null);
        for (Display display : mDisplayManager.getDisplays(DisplayManager.DISPLAY_CATEGORY_PRESENTATION)) {
            showOn(display);
        }
    }

    public void stop() {
        if (mDisplayManager == null) {
            return;
        }
        mDisplayManager.unregisterDisplayListener(this);
        dismiss();
    }

    private void showOn(Display display) {
        if (mPresentation != null) {
            return; // Thor only has the one secondary panel; already showing.
        }
        Log.i(TAG, "showing second-screen panel on display " + display.getDisplayId());
        SecondScreenPresentation presentation = new SecondScreenPresentation(mContext, display);
        presentation.setOnDismissListener(dialog -> {
            if (mPresentation == presentation) {
                mPresentation = null;
            }
        });
        try {
            presentation.show();
            mPresentation = presentation;
        } catch (Exception e) {
            // Presentation.show() can fail if the display is claimed
            // elsewhere (e.g. AYN's own Cocoon Shell launcher) — degrade
            // gracefully rather than crashing the whole app over a HUD panel.
            Log.w(TAG, "failed to show second-screen presentation", e);
        }
    }

    private void dismiss() {
        if (mPresentation != null) {
            mPresentation.dismiss();
            mPresentation = null;
        }
    }

    @Override
    public void onDisplayAdded(int displayId) {
        Display display = mDisplayManager.getDisplay(displayId);
        if (display != null && (display.getFlags() & Display.FLAG_PRESENTATION) != 0) {
            showOn(display);
        }
    }

    @Override
    public void onDisplayRemoved(int displayId) {
        if (mPresentation != null && mPresentation.getDisplay().getDisplayId() == displayId) {
            // The display (and its window) is already gone — don't call
            // dismiss() on it, just drop our reference.
            mPresentation = null;
        }
    }

    @Override
    public void onDisplayChanged(int displayId) {
        // No-op for now; revisit if the secondary panel's resolution/state
        // can change while attached (not expected on Thor's fixed hardware).
    }
}
