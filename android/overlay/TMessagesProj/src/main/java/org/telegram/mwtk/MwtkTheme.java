package org.telegram.mwtk;

import android.graphics.Color;

import org.telegram.ui.ActionBar.Theme;

/** "Whole app" transparency: backgrounds let the MWTK backdrop show. */
public final class MwtkTheme {

    private static volatile int alpha = -1;
    private static volatile boolean active;

    static {
        MwtkConfig.addListener(MwtkTheme::refresh);
    }

    private MwtkTheme() {
    }

    public static void refresh() {
        MwtkConfig.load();
        int value = MwtkConfig.num(MwtkConfig.APP_ALPHA, 100);
        alpha = Math.max(15, Math.min(100, value));
        active = MwtkConfig.effects && alpha < 100;
    }

    public static boolean active() {
        if (alpha < 0) {
            refresh();
        }
        return active;
    }

    public static int adjust(int key, int color) {
        if (alpha < 0) {
            refresh();
        }
        if (!active) {
            return color;
        }
        if (key == Theme.key_windowBackgroundWhite
            || key == Theme.key_windowBackgroundGray
            || key == Theme.key_actionBarDefault
            || key == Theme.key_actionBarDefaultArchived
            || key == Theme.key_chats_menuBackground
            || key == Theme.key_windowBackgroundGrayShadow
            || key == Theme.key_graySection) {
            int a = Color.alpha(color) * alpha / 100;
            return (color & 0x00ffffff) | (a << 24);
        }
        return color;
    }
}
