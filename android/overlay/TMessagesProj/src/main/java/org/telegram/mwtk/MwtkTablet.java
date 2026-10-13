package org.telegram.mwtk;

import android.annotation.SuppressLint;
import android.content.Context;
import android.graphics.Rect;
import android.view.View;
import android.widget.FrameLayout;

import org.telegram.messenger.AndroidUtilities;
import org.telegram.ui.Components.LayoutHelper;

import java.lang.ref.WeakReference;

/**
 * Tablet mode: the empty right side (shown while no chat is open) becomes a
 * live MWTK 3D scene, and the song lyrics use that whole side.
 * Settings: «Режим планшета».
 */
public final class MwtkTablet {

    private static WeakReference<View> rightSide = new WeakReference<>(null);
    private static final int[] tmp = new int[2];

    private MwtkTablet() {
    }

    public static boolean enabled() {
        MwtkConfig.load();
        return MwtkConfig.effects && MwtkConfig.flag(MwtkConfig.TABLET, true);
    }

    /** Called with the tablet right-side panel of LaunchActivity. */
    public static void attach(FrameLayout panel) {
        if (panel == null) {
            return;
        }
        rightSide = new WeakReference<>(panel);
        panel.addView(new SceneView(panel.getContext()),
            LayoutHelper.createFrame(LayoutHelper.MATCH_PARENT, LayoutHelper.MATCH_PARENT));
    }

    /**
     * The free right side in the coordinates of {@code view}, or null when the
     * phone layout is used, the panel is hidden or the mode is off.
     */
    public static Rect freeArea(View view) {
        View panel = rightSide.get();
        if (panel == null || view == null || !enabled() || !AndroidUtilities.isTablet()
            || panel.getVisibility() != View.VISIBLE || !panel.isShown()
            || panel.getWidth() < AndroidUtilities.dp(300)) {
            return null;
        }
        panel.getLocationInWindow(tmp);
        int px = tmp[0], py = tmp[1];
        view.getLocationInWindow(tmp);
        int left = px - tmp[0];
        int top = py - tmp[1];
        return new Rect(left, top, left + panel.getWidth(), top + panel.getHeight());
    }

    @SuppressLint("ViewConstructor")
    private static final class SceneView extends View {

        private final Runnable configListener = this::update;
        private MwtkBackgroundDrawable drawable;

        SceneView(Context context) {
            super(context);
            setImportantForAccessibility(IMPORTANT_FOR_ACCESSIBILITY_NO);
        }

        private void update() {
            boolean on = enabled() && isAttachedToWindow();
            if (on && drawable == null) {
                drawable = new MwtkBackgroundDrawable();
                setBackground(drawable);
            } else if (!on && drawable != null) {
                setBackground(null);
                drawable.dispose();
                drawable = null;
            }
            setVisibility(on ? VISIBLE : GONE);
        }

        @Override
        protected void onAttachedToWindow() {
            super.onAttachedToWindow();
            MwtkConfig.addListener(configListener);
            update();
        }

        @Override
        protected void onDetachedFromWindow() {
            MwtkConfig.removeListener(configListener);
            if (drawable != null) {
                setBackground(null);
                drawable.dispose();
                drawable = null;
            }
            super.onDetachedFromWindow();
        }
    }
}
