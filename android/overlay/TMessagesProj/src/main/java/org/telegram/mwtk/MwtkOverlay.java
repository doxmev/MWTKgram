package org.telegram.mwtk;

import android.annotation.SuppressLint;
import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.LinearGradient;
import android.graphics.Paint;
import android.graphics.RectF;
import android.graphics.Shader;
import android.os.Bundle;
import android.os.SystemClock;
import android.text.TextUtils;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.widget.FrameLayout;
import android.widget.TextView;

import org.telegram.messenger.AndroidUtilities;
import org.telegram.messenger.ApplicationLoader;
import org.telegram.messenger.DialogObject;
import org.telegram.messenger.ImageReceiver;
import org.telegram.messenger.MessageObject;
import org.telegram.messenger.MessagesController;
import org.telegram.messenger.NotificationCenter;
import org.telegram.messenger.UserConfig;
import org.telegram.messenger.UserObject;
import org.telegram.tgnet.TLObject;
import org.telegram.tgnet.TLRPC;
import org.telegram.ui.ActionBar.BaseFragment;
import org.telegram.ui.ChatActivity;
import org.telegram.ui.Components.AvatarDrawable;
import org.telegram.ui.Components.BackupImageView;
import org.telegram.ui.Components.LayoutHelper;
import org.telegram.ui.DialogsActivity;
import org.telegram.ui.LaunchActivity;

import java.util.ArrayList;

/**
 * App-wide MWTK layers: the backdrop behind a transparent interface,
 * flying lyrics, in-app hologram notifications and the avatar fly-by.
 */
public final class MwtkOverlay implements NotificationCenter.NotificationCenterDelegate {

    private static MwtkOverlay instance;

    private final FrameLayout root;
    private final View content;
    private final FlybyView flyby;
    private BannerView banner;

    private MwtkOverlay(FrameLayout root, View content) {
        this.root = root;
        this.content = content;
        flyby = new FlybyView(root.getContext());
        root.addView(flyby, LayoutHelper.createFrame(LayoutHelper.MATCH_PARENT, LayoutHelper.MATCH_PARENT));
        MwtkLyrics.attach(root);
        for (int a = 0; a < UserConfig.MAX_ACCOUNT_COUNT; a++) {
            NotificationCenter.getInstance(a).addObserver(this, NotificationCenter.didReceiveNewMessages);
        }
        MwtkConfig.addListener(this::refreshBackdrop);
        refreshBackdrop();
    }

    /** The app-wide root layer, or null before the main screen exists. */
    public static FrameLayout root() {
        return instance != null ? instance.root : null;
    }

    public static void attach(FrameLayout root, View content) {
        if (instance == null && root != null) {
            instance = new MwtkOverlay(root, content);
        }
    }

    /** Outgoing message: a soft sound and a spark burst at the send button. */
    public static void onSend() {
        MwtkSounds.play(MwtkSounds.SEND);
        if (instance != null && MwtkConfig.animate() && MwtkConfig.flag(MwtkConfig.SEND_SPARKS, true)) {
            instance.flyby.burst(instance.flyby.getWidth() - AndroidUtilities.dp(36),
                instance.flyby.getHeight() - AndroidUtilities.dp(32));
        }
    }

    private void refreshBackdrop() {
        AndroidUtilities.runOnUIThread(() -> {
            MwtkTheme.refresh();
            if (content == null) {
                return;
            }
            if (MwtkTheme.active()) {
                if (!(content.getBackground() instanceof MwtkBackgroundDrawable)) {
                    content.setBackground(new MwtkBackgroundDrawable());
                }
            } else if (content.getBackground() instanceof MwtkBackgroundDrawable) {
                content.setBackground(null);
            }
            content.invalidate();
        });
    }

    @Override
    public void didReceivedNotification(int id, int account, Object... args) {
        if (id != NotificationCenter.didReceiveNewMessages || args.length < 2) {
            return;
        }
        if (args.length > 2 && args[2] instanceof Boolean && (Boolean) args[2]) {
            return; // scheduled
        }
        if (ApplicationLoader.mainInterfacePaused || !MwtkConfig.animate()) {
            return;
        }
        long dialogId = (Long) args[0];
        @SuppressWarnings("unchecked")
        ArrayList<MessageObject> messages = (ArrayList<MessageObject>) args[1];
        if (messages == null || messages.isEmpty()) {
            return;
        }
        MessageObject last = messages.get(messages.size() - 1);
        if (last.isOut() || last.messageOwner == null) {
            return;
        }
        BaseFragment fragment = LaunchActivity.getLastFragment();
        if (fragment instanceof ChatActivity && ((ChatActivity) fragment).getDialogId() == dialogId) {
            return;
        }
        MessagesController controller = MessagesController.getInstance(account);
        if (isMuted(account, dialogId, last.getTopicId())) {
            return; // muted chats stay silent: no banner, no fly-by
        }
        TLObject peer = DialogObject.isUserDialog(dialogId)
            ? controller.getUser(dialogId)
            : controller.getChat(-dialogId);
        if (peer == null) {
            return;
        }
        if (MwtkConfig.flag(MwtkConfig.FLYBY, true) && fragment instanceof DialogsActivity) {
            flyby.launch(account, peer);
        }
        if (MwtkConfig.num(MwtkConfig.NOTIFY_STYLE, 1) != 0) {
            showBanner(account, dialogId, peer, last);
        }
    }

    /**
     * Only a mute the user set on this chat counts. Telegram's own check also
     * treats every chat as muted when notifications are switched off globally
     * in its settings, which hid all MWTK banners.
     */
    private static boolean isMuted(int account, long dialogId, long topicId) {
        android.content.SharedPreferences prefs = MessagesController.getNotificationsSettings(account);
        String key = org.telegram.messenger.NotificationsController.getSharedPrefKey(dialogId, topicId);
        int type = prefs.getInt("notify2_" + key, -1);
        if (type == -1 && topicId != 0) {
            key = org.telegram.messenger.NotificationsController.getSharedPrefKey(dialogId, 0);
            type = prefs.getInt("notify2_" + key, -1);
        }
        if (type == 2) {
            return true;
        }
        if (type == 3) {
            int until = prefs.getInt("notifyuntil_" + key, 0);
            return until >= org.telegram.tgnet.ConnectionsManager.getInstance(account).getCurrentTime();
        }
        if (type == -1 && !DialogObject.isUserDialog(dialogId)) {
            // Channels are quiet by default, groups and private chats are not.
            TLRPC.Chat chat = MessagesController.getInstance(account).getChat(-dialogId);
            return chat != null && org.telegram.messenger.ChatObject.isChannel(chat) && !chat.megagroup;
        }
        return false;
    }

    private void showBanner(int account, long dialogId, TLObject peer, MessageObject message) {
        if (banner != null) {
            root.removeView(banner);
        }
        String title = peer instanceof TLRPC.User
            ? UserObject.getUserName((TLRPC.User) peer)
            : MwtkNames.chat((TLRPC.Chat) peer);
        CharSequence text = message.messageText;
        banner = new BannerView(root.getContext(), account, dialogId, peer, title,
            text == null ? "" : text.toString(), MwtkConfig.num(MwtkConfig.NOTIFY_STYLE, 1));
        FrameLayout.LayoutParams params = LayoutHelper.createFrame(LayoutHelper.MATCH_PARENT, 76, Gravity.TOP, 10, 0, 10, 0);
        params.topMargin = AndroidUtilities.statusBarHeight + AndroidUtilities.dp(6);
        root.addView(banner, params);
        banner.show();
    }

    // ------------------------------------------------------------ banner

    @SuppressLint("ViewConstructor")
    private final class BannerView extends FrameLayout {

        private final int style;
        private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint edge = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint lines = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final RectF rect = new RectF();
        private final long started = SystemClock.elapsedRealtime();

        BannerView(Context context, int account, long dialogId, TLObject peer, String title, String text, int style) {
            super(context);
            this.style = style;
            setWillNotDraw(false);
            BackupImageView avatar = new BackupImageView(context);
            avatar.setRoundRadius(AndroidUtilities.dp(style == 2 ? 12 : 22));
            AvatarDrawable avatarDrawable = new AvatarDrawable();
            avatarDrawable.setInfo(account, peer);
            avatar.setForUserOrChat(peer, avatarDrawable);
            addView(avatar, LayoutHelper.createFrame(44, 44, Gravity.LEFT | Gravity.CENTER_VERTICAL, 14, 0, 0, 0));
            TextView name = new TextView(context);
            name.setTextSize(TypedValue.COMPLEX_UNIT_DIP, 15);
            name.setTypeface(AndroidUtilities.bold());
            name.setTextColor(0xffffffff);
            name.setSingleLine(true);
            name.setEllipsize(TextUtils.TruncateAt.END);
            name.setText(title);
            addView(name, LayoutHelper.createFrame(LayoutHelper.MATCH_PARENT, LayoutHelper.WRAP_CONTENT, Gravity.LEFT | Gravity.TOP, 70, 14, 14, 0));
            TextView body = new TextView(context);
            body.setTextSize(TypedValue.COMPLEX_UNIT_DIP, 14);
            body.setTextColor(0xd9ffffff);
            body.setSingleLine(true);
            body.setEllipsize(TextUtils.TruncateAt.END);
            body.setText(text);
            addView(body, LayoutHelper.createFrame(LayoutHelper.MATCH_PARENT, LayoutHelper.WRAP_CONTENT, Gravity.LEFT | Gravity.TOP, 70, 38, 14, 0));
            setOnClickListener(v -> {
                hide();
                BaseFragment last = LaunchActivity.getLastFragment();
                if (last != null) {
                    Bundle args = new Bundle();
                    if (DialogObject.isUserDialog(dialogId)) {
                        args.putLong("user_id", dialogId);
                    } else {
                        args.putLong("chat_id", -dialogId);
                    }
                    last.presentFragment(new ChatActivity(args));
                }
            });
        }

        void show() {
            setAlpha(0f);
            setTranslationY(-AndroidUtilities.dp(80));
            animate().alpha(1f).translationY(0).setDuration(320).start();
            postDelayed(this::hide, 3600);
            postInvalidateOnAnimation();
        }

        void hide() {
            animate().alpha(0f).translationY(-AndroidUtilities.dp(80)).setDuration(240)
                .withEndAction(() -> {
                    if (getParent() != null) {
                        ((FrameLayout) getParent()).removeView(this);
                    }
                    if (banner == this) {
                        banner = null;
                    }
                }).start();
        }

        @Override
        protected void onDraw(Canvas canvas) {
            int accent = MwtkConfig.accent();
            rect.set(AndroidUtilities.dp(2), AndroidUtilities.dp(2), getWidth() - AndroidUtilities.dp(2), getHeight() - AndroidUtilities.dp(2));
            float radius = AndroidUtilities.dp(style == 3 ? 16 : 22);
            long passed = SystemClock.elapsedRealtime() - started;
            if (style == 1) {
                fill.setShader(new LinearGradient(0, 0, getWidth(), getHeight(),
                    new int[] { Color.argb(235, 34, 6, 14), Color.argb(235, 30, 10, 52), Color.argb(235, 4, 30, 40) },
                    null, Shader.TileMode.CLAMP));
                canvas.drawRoundRect(rect, radius, radius, fill);
                lines.setColor(0x10ffffff);
                for (float y = rect.top; y < rect.bottom; y += AndroidUtilities.dp(4)) {
                    canvas.drawLine(rect.left + radius / 2, y, rect.right - radius / 2, y, lines);
                }
                float phase = (passed % 1400) / 1400f;
                if (passed < 2800) {
                    float x = rect.left - rect.width() * 0.4f + phase * rect.width() * 1.8f;
                    lines.setShader(new LinearGradient(x, 0, x + rect.width() * 0.35f, getHeight(),
                        new int[] { 0x00ffffff, 0x26ffffff, 0x00ffffff }, null, Shader.TileMode.CLAMP));
                    canvas.drawRoundRect(rect, radius, radius, lines);
                    lines.setShader(null);
                    postInvalidateOnAnimation();
                }
                edge.setStyle(Paint.Style.STROKE);
                edge.setStrokeWidth(AndroidUtilities.dp(1.4f));
                edge.setShader(new LinearGradient(0, 0, getWidth(), 0,
                    new int[] { accent, 0xffaa5aff, 0xff00e6ff }, null, Shader.TileMode.CLAMP));
                canvas.drawRoundRect(rect, radius, radius, edge);
            } else if (style == 2) {
                fill.setShader(null);
                fill.setColor(0xee16030a);
                canvas.drawRoundRect(rect, radius, radius, fill);
                edge.setShader(null);
                edge.setStyle(Paint.Style.STROKE);
                edge.setStrokeWidth(AndroidUtilities.dp(1.5f));
                edge.setColor(accent);
                edge.setShadowLayer(AndroidUtilities.dp(6), 0, 0, accent);
                canvas.drawRoundRect(rect, radius, radius, edge);
                edge.clearShadowLayer();
            } else {
                fill.setShader(new LinearGradient(0, 0, 0, getHeight(),
                    new int[] { accent, Color.rgb(Color.red(accent) / 2, Color.green(accent) / 2, Color.blue(accent) / 2) },
                    null, Shader.TileMode.CLAMP));
                rect.offset(0, AndroidUtilities.dp(2));
                fill.setAlpha(120);
                canvas.drawRoundRect(rect, radius, radius, fill);
                rect.offset(0, -AndroidUtilities.dp(2));
                fill.setAlpha(255);
                canvas.drawRoundRect(rect, radius, radius, fill);
            }
        }
    }

    // ------------------------------------------------------------ fly-by

    private static final class FlybyView extends View {

        private final ArrayList<Object[]> items = new ArrayList<>();
        private final ArrayList<float[]> sparks = new ArrayList<>();
        private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
        private long lastSparkFrame;

        void burst(float x, float y) {
            java.util.Random r = new java.util.Random();
            for (int i = 0; i < 24; i++) {
                double angle = i < 16 ? -Math.PI * (0.1 + 0.8 * r.nextFloat()) : r.nextFloat() * Math.PI * 2;
                float speed = AndroidUtilities.dp(120 + 200 * r.nextFloat());
                sparks.add(new float[] { x, y, (float) Math.cos(angle) * speed, (float) Math.sin(angle) * speed,
                    0.75f + 0.25f * r.nextFloat(), AndroidUtilities.dp(1.6f + 2f * r.nextFloat()) });
            }
            lastSparkFrame = 0;
            invalidate();
        }

        FlybyView(Context context) {
            super(context);
            setWillNotDraw(false);
        }

        void launch(int account, TLObject peer) {
            if (items.size() > 3 || getWidth() == 0) {
                return;
            }
            ImageReceiver avatar = new ImageReceiver(this);
            avatar.setRoundRadius(AndroidUtilities.dp(22));
            AvatarDrawable drawable = new AvatarDrawable();
            drawable.setInfo(account, peer);
            avatar.setForUserOrChat(peer, drawable);
            avatar.onAttachedToWindow();
            float fromX = getWidth() * (0.6f + (float) Math.random() * 0.35f);
            float toY = getHeight() * (0.25f + (float) Math.random() * 0.4f);
            items.add(new Object[] { avatar, SystemClock.elapsedRealtime(), fromX, toY });
            invalidate();
        }

        @Override
        protected void onDraw(Canvas canvas) {
            long now = SystemClock.elapsedRealtime();
            int accent = MwtkConfig.accent();
            for (int i = items.size() - 1; i >= 0; i--) {
                Object[] item = items.get(i);
                float k = (now - (Long) item[1]) / 1500f;
                if (k >= 1f) {
                    ((ImageReceiver) item[0]).onDetachedFromWindow();
                    items.remove(i);
                    continue;
                }
                float ease = 1f - (1f - k) * (1f - k) * (1f - k);
                float fromX = (Float) item[2];
                float toY = (Float) item[3];
                float x = fromX + (AndroidUtilities.dp(56) - fromX) * ease;
                float y = -AndroidUtilities.dp(40) + (toY + AndroidUtilities.dp(40)) * ease
                    + (float) Math.sin(k * Math.PI) * AndroidUtilities.dp(60);
                float alpha = Math.min(1f, Math.min(k * 6f, (1f - k) * 4f));
                float size = AndroidUtilities.dp(22 + 8 * (float) Math.sin(k * Math.PI));
                paint.setColor(accent);
                for (int t = 5; t > 0; t--) {
                    float tk = Math.max(ease - t * 0.035f, 0f);
                    float tx = fromX + (AndroidUtilities.dp(56) - fromX) * tk;
                    float ty = -AndroidUtilities.dp(40) + (toY + AndroidUtilities.dp(40)) * tk;
                    paint.setAlpha((int) (255 * alpha * (0.45f - t * 0.07f)));
                    canvas.drawCircle(tx, ty, size * (0.5f - t * 0.06f), paint);
                }
                paint.setAlpha((int) (110 * alpha));
                canvas.drawCircle(x, y, size * 1.4f, paint);
                ImageReceiver avatar = (ImageReceiver) item[0];
                avatar.setImageCoords(x - size, y - size, size * 2, size * 2);
                avatar.setAlpha(alpha);
                avatar.draw(canvas);
            }
            if (!sparks.isEmpty()) {
                long frame = SystemClock.elapsedRealtime();
                float dt = lastSparkFrame == 0 ? 0 : Math.min((frame - lastSparkFrame) / 1000f, 0.05f);
                lastSparkFrame = frame;
                for (int i = sparks.size() - 1; i >= 0; i--) {
                    float[] s = sparks.get(i);
                    s[4] -= dt / 0.9f;
                    s[3] += AndroidUtilities.dp(520) * dt;
                    s[2] *= (float) Math.exp(-dt * 1.8f);
                    s[3] *= (float) Math.exp(-dt * 1.8f);
                    s[0] += s[2] * dt;
                    s[1] += s[3] * dt;
                    if (s[4] <= 0) {
                        sparks.remove(i);
                        continue;
                    }
                    paint.setColor(accent);
                    paint.setAlpha((int) (255 * Math.min(1f, s[4])));
                    canvas.drawCircle(s[0], s[1], s[5], paint);
                }
            }
            if (!items.isEmpty() || !sparks.isEmpty()) {
                postInvalidateOnAnimation();
            }
        }
    }
}
