package org.telegram.mwtk;

import android.annotation.SuppressLint;
import android.content.Context;
import android.view.View;
import android.view.ViewGroup;
import android.widget.FrameLayout;

import org.telegram.ui.Components.LayoutHelper;

/**
 * Effects inside a chat: floating labels, figures and seasons drawn over the
 * chat wallpaper and under the messages. Settings: «Эффекты в чате».
 */
public final class MwtkChatFx {

    private MwtkChatFx() {
    }

    public static void attach(FrameLayout contentView, View chatListView) {
        if (contentView == null || chatListView == null) {
            return;
        }
        int index = contentView.indexOfChild(chatListView);
        FxView view = new FxView(contentView.getContext());
        contentView.addView(view, Math.max(0, index),
            LayoutHelper.createFrame(LayoutHelper.MATCH_PARENT, LayoutHelper.MATCH_PARENT));
    }

    @SuppressLint("ViewConstructor")
    private static final class FxView extends View {

        private final Runnable configListener = this::update;
        private MwtkBackgroundDrawable drawable;

        FxView(Context context) {
            super(context);
            setClickable(false);
            setFocusable(false);
            setImportantForAccessibility(IMPORTANT_FOR_ACCESSIBILITY_NO);
        }

        private static boolean enabled() {
            MwtkConfig.load();
            return MwtkConfig.effects && MwtkConfig.num(MwtkConfig.CHAT_FX, 1) == 1;
        }

        private void update() {
            boolean on = enabled() && isAttachedToWindow();
            if (on && drawable == null) {
                drawable = new MwtkBackgroundDrawable(true);
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
