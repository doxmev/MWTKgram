package org.telegram.mwtk;

import org.telegram.ui.LauncherIconController;

/** App icon switch: classic M, blood cube, eye, MWTKgram neon. */
public final class MwtkIcons {

    public static final String[] NAMES = { "Классическая M", "Кровавый куб", "Глаз MWTK", "MWTKgram неон" };

    private MwtkIcons() {
    }

    public static void apply(int index) {
        LauncherIconController.LauncherIcon icon;
        switch (index) {
            case 1: icon = LauncherIconController.LauncherIcon.VINTAGE; break;
            case 2: icon = LauncherIconController.LauncherIcon.AQUA; break;
            case 3: icon = LauncherIconController.LauncherIcon.NOX; break;
            default: icon = LauncherIconController.LauncherIcon.DEFAULT; break;
        }
        try {
            LauncherIconController.setIcon(icon);
        } catch (Throwable ignore) {
        }
    }
}
