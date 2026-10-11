package com.nubia.rmcontrol;

import android.content.Context;
import android.content.om.OverlayInfo;
import android.content.om.OverlayManager;
import android.os.UserHandle;
import android.util.Log;

/**
 * Controls the overlay that enables desktop windowing on the internal display.
 * The same platform setting also enables the fullscreen app handle.
 */
final class DesktopIntegration {

    private static final String TAG = "RMControl";

    /** Mutable /product/overlay RRO that flips config_canInternalDisplayHostDesktops back to true. */
    static final String DESKTOP_OVERLAY = "com.nx809j.overlay.showapphandle";

    private DesktopIntegration() {}

    /** True when the internal display may host desktops (and therefore shows the app handle). */
    static boolean isOnDeviceDesktopEnabled(Context ctx) {
        try {
            final OverlayManager om = ctx.getSystemService(OverlayManager.class);
            if (om == null) return false;
            final OverlayInfo info = om.getOverlayInfo(DESKTOP_OVERLAY, UserHandle.SYSTEM);
            return info != null && info.isEnabled();
        } catch (Throwable t) {
            Log.w(TAG, "could not read desktop overlay state", t);
            return false;
        }
    }

    /**
     * Enable/disable on-device desktop hosting. Equivalent to
     * {@code cmd overlay enable|disable com.nx809j.overlay.showapphandle}, which is what people are
     * told to run by hand today.
     *
     * @return true if the request was accepted
     */
    static boolean setOnDeviceDesktopEnabled(Context ctx, boolean enable) {
        try {
            final OverlayManager om = ctx.getSystemService(OverlayManager.class);
            if (om == null) return false;
            om.setEnabled(DESKTOP_OVERLAY, enable, UserHandle.SYSTEM);
            Log.i(TAG, "on-device desktop " + (enable ? "enabled" : "disabled"));
            return true;
        } catch (Throwable t) {
            Log.e(TAG, "could not toggle desktop overlay", t);
            return false;
        }
    }
}
