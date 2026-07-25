package dev.picori.tmc;

import android.content.Context;
import android.hardware.display.DisplayManager;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.view.Display;

/**
 * Finds Thor's secondary panel (a real Android Display flagged
 * FLAG_PRESENTATION, normally owned by AYN's own launcher when this app
 * isn't targeting it) and shows/hides our SecondScreenPresentation on it as
 * the display attaches/detaches or the activity's own lifecycle changes —
 * and re-shows it if the system ever dismisses it behind our back.
 */
public class SecondScreenManager implements DisplayManager.DisplayListener {
    private static final String TAG = "SecondScreenManager";

    private final Context mContext;
    private final DisplayManager mDisplayManager;
    private final Handler mMainHandler = new Handler(Looper.getMainLooper());
    private SecondScreenPresentation mPresentation;
    // True outside a start()..stop() window: while stopped the panel must
    // stay down no matter what messages are still in flight. Only touched on
    // the main thread — activity lifecycle, our display listener (registered
    // with a null handler = caller's looper) and Dialog's dismiss messages
    // all land there, so no locking is needed.
    private boolean mStopped = true;

    public SecondScreenManager(Context context) {
        mContext = context.getApplicationContext();
        mDisplayManager = (DisplayManager) mContext.getSystemService(Context.DISPLAY_SERVICE);
    }

    public void start() {
        if (mDisplayManager == null) {
            return;
        }
        mStopped = false;
        mDisplayManager.registerDisplayListener(this, null);
        showOnAttachedDisplays();
    }

    public void stop() {
        if (mDisplayManager == null) {
            return;
        }
        mStopped = true;
        mDisplayManager.unregisterDisplayListener(this);
        dismiss();
    }

    private void showOnAttachedDisplays() {
        for (Display display : mDisplayManager.getDisplays(DisplayManager.DISPLAY_CATEGORY_PRESENTATION)) {
            showOn(display);
        }
    }

    private void showOn(Display display) {
        if (mPresentation != null || mStopped) {
            return; // Thor only has the one secondary panel; already showing
                    // (or the activity is backgrounded and the panel is down
                    // on purpose).
        }
        Log.i(TAG, "showing second-screen panel on display " + display.getDisplayId());
        SecondScreenPresentation presentation = new SecondScreenPresentation(mContext, display);
        presentation.setOnDismissListener(dialog -> {
            // Dismiss recovery. Dialog delivers onDismiss as a *posted*
            // message, a looper pass after the dismissal itself, and every
            // path where we take the panel down on purpose (stop() ->
            // dismiss(), display removal) has already cleared mPresentation
            // by the time it arrives. So "mPresentation still points at this
            // presentation" reliably means the system dismissed the window
            // behind our back (display flicker, config churn), and without a
            // re-show here the bottom screen stays dead until an app
            // restart. Don't rework this into a "did we call dismiss()" flag
            // tested here — with the posted delivery such a flag describes
            // the wrong point in time, the ordering trap the zelda3-android
            // mod ran into before settling on this clear-the-reference-first
            // scheme.
            if (mPresentation != presentation) {
                return; // our own teardown; stay silent.
            }
            mPresentation = null;
            // Re-show from a fresh message rather than inside the dismiss
            // dispatch, re-scanning what's attached at that point: if the
            // panel's display itself is mid-flicker the scan simply finds
            // nothing, and the still-registered display listener brings the
            // panel back in onDisplayAdded once the display returns.
            mMainHandler.post(() -> {
                if (!mStopped) {
                    showOnAttachedDisplays();
                }
            });
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
            // Cleared before the onDismiss message can arrive (delivery is
            // posted, never synchronous) — that is what tells the dismiss
            // listener this teardown was intentional.
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
            // dismiss() on it, just drop our reference. The Presentation
            // also dismisses itself on display removal; its posthumous
            // onDismiss then finds the reference already cleared and the
            // recovery logic correctly stays quiet.
            mPresentation = null;
        }
    }

    @Override
    public void onDisplayChanged(int displayId) {
        // No-op for now; revisit if the secondary panel's resolution/state
        // can change while attached (not expected on Thor's fixed hardware).
    }
}
