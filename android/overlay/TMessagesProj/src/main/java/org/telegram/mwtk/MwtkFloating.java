package org.telegram.mwtk;

import android.annotation.SuppressLint;
import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.os.Bundle;
import android.text.TextPaint;
import android.view.MotionEvent;
import android.view.VelocityTracker;
import android.view.View;
import android.view.ViewGroup;
import android.widget.FrameLayout;

import org.telegram.messenger.AndroidUtilities;
import org.telegram.messenger.DialogObject;
import org.telegram.messenger.ImageReceiver;
import org.telegram.messenger.MessagesController;
import org.telegram.messenger.UserObject;
import org.telegram.tgnet.TLObject;
import org.telegram.tgnet.TLRPC;
import org.telegram.ui.ActionBar.BaseFragment;
import org.telegram.ui.ChatActivity;
import org.telegram.ui.Components.AvatarDrawable;
import org.telegram.ui.Components.LayoutHelper;

import java.util.ArrayList;
import java.util.Random;

/**
 * Floating chats on the phone: the list dissolves, chats become bubbles
 * that roll with the phone tilt and can be thrown with a finger.
 * Long press on the screen title switches the mode.
 */
public final class MwtkFloating {

    private MwtkFloating() {
    }

    public static void attach(BaseFragment fragment, FrameLayout contentView, View list) {
        if (fragment == null || contentView == null || fragment.getActionBar() == null) {
            return;
        }
        FloatingView floating = new FloatingView(contentView.getContext(), fragment, list, contentView);
        floating.setVisibility(View.GONE);
        contentView.addView(floating, LayoutHelper.createFrame(LayoutHelper.MATCH_PARENT, LayoutHelper.MATCH_PARENT));
        fragment.getActionBar().setOnLongClickListener(v -> {
            MwtkConfig.load();
            if (!MwtkConfig.effects) {
                return false;
            }
            floating.toggle();
            MwtkMotion.vibrate(30);
            return true;
        });
    }

    private static final class Bubble {
        float x, y, vx, vy, r;
        ImageReceiver image;
        String label;
        long dialogId;
    }

    /** How many chats fly: «Сколько чатов летает», 5..40. */
    public static int count() {
        return Math.max(5, Math.min(40, MwtkConfig.num(MwtkConfig.FLOAT_COUNT, 16)));
    }

    private static ArrayList<Long> picked() {
        ArrayList<Long> result = new ArrayList<>();
        for (String part : MwtkConfig.text(MwtkConfig.FLOAT_PICKED, "").split(",")) {
            try {
                if (!part.trim().isEmpty()) {
                    result.add(Long.parseLong(part.trim()));
                }
            } catch (NumberFormatException ignore) {
            }
        }
        return result;
    }

    /** The chats that fly: recent, pinned or picked by hand. */
    static ArrayList<Long> chosenDialogs(int account) {
        int limit = count();
        int source = MwtkConfig.num(MwtkConfig.FLOAT_SOURCE, 0);
        ArrayList<Long> result = new ArrayList<>();
        if (source == 2) {
            for (long id : picked()) {
                result.add(id);
                if (result.size() >= limit) {
                    break;
                }
            }
            if (!result.isEmpty()) {
                return result;
            }
        }
        ArrayList<TLRPC.Dialog> dialogs = MwtkHidden.filter(MessagesController.getInstance(account).getDialogs(0));
        for (TLRPC.Dialog dialog : dialogs) {
            if (source == 1 && !dialog.pinned) {
                continue;
            }
            result.add(dialog.id);
            if (result.size() >= limit) {
                break;
            }
        }
        return result;
    }

    /** «Выбрать летающие чаты»: tick the chats, the source becomes «Выбрать вручную». */
    public static void showPickDialog(BaseFragment fragment) {
        Context context = fragment.getParentActivity();
        if (context == null) {
            return;
        }
        int account = fragment.getCurrentAccount();
        MessagesController controller = MessagesController.getInstance(account);
        ArrayList<TLRPC.Dialog> dialogs = MwtkHidden.filter(controller.getDialogs(0));
        ArrayList<Long> ids = new ArrayList<>();
        ArrayList<String> names = new ArrayList<>();
        ArrayList<Long> already = picked();
        for (TLRPC.Dialog dialog : dialogs) {
            TLObject peer = DialogObject.isUserDialog(dialog.id)
                ? controller.getUser(dialog.id)
                : controller.getChat(-dialog.id);
            if (peer == null) {
                continue;
            }
            ids.add(dialog.id);
            names.add(peer instanceof TLRPC.User
                ? UserObject.getUserName((TLRPC.User) peer)
                : MwtkNames.chat((TLRPC.Chat) peer));
            if (ids.size() >= 300) {
                break;
            }
        }
        boolean[] checked = new boolean[ids.size()];
        for (int i = 0; i < ids.size(); i++) {
            checked[i] = already.contains(ids.get(i));
        }
        android.app.AlertDialog.Builder builder = new android.app.AlertDialog.Builder(context);
        builder.setTitle("Какие чаты летают");
        builder.setMultiChoiceItems(names.toArray(new String[0]), checked, (d, which, isChecked) -> checked[which] = isChecked);
        builder.setPositiveButton("Сохранить", (d, which) -> {
            StringBuilder value = new StringBuilder();
            for (int i = 0; i < ids.size(); i++) {
                if (checked[i]) {
                    if (value.length() > 0) {
                        value.append(',');
                    }
                    value.append(ids.get(i));
                }
            }
            MwtkConfig.setText(MwtkConfig.FLOAT_PICKED, value.toString());
            MwtkConfig.setNum(MwtkConfig.FLOAT_SOURCE, 2);
            MwtkUi.toast("Сохранено");
        });
        builder.setNegativeButton("Отмена", null);
        builder.show();
    }

    @SuppressLint("ViewConstructor")
    private static final class FloatingView extends View {

        private final BaseFragment fragment;
        private final View list;
        private final FrameLayout home;
        private final ArrayList<Bubble> bubbles = new ArrayList<>();
        private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final TextPaint text = new TextPaint(Paint.ANTI_ALIAS_FLAG);
        private VelocityTracker tracker;
        private Bubble dragged;
        private float downX, downY;
        private long lastFrame;
        private boolean active;

        FloatingView(Context context, BaseFragment fragment, View list, FrameLayout home) {
            super(context);
            this.fragment = fragment;
            this.list = list;
            this.home = home;
            text.setTextAlign(Paint.Align.CENTER);
            text.setColor(0xffffffff);
            text.setTypeface(AndroidUtilities.bold());
            setOutlineProvider(null); // no shadow from the raised Z
        }

        void toggle() {
            active = !active;
            if (active) {
                // Tablet mode is checked at the moment of opening: over the whole
                // screen when it is on, inside the chat list column when it is off.
                FrameLayout root = MwtkOverlay.root();
                boolean wholeScreen = AndroidUtilities.isTablet() && root != null && MwtkTablet.enabled();
                moveTo(wholeScreen ? root : home);
                // Above the chat list in drawing and touch order.
                setTranslationZ(AndroidUtilities.dp(1000));
                build();
                if (!(getBackground() instanceof MwtkBackgroundDrawable)) {
                    setBackground(new MwtkBackgroundDrawable());
                }
                setVisibility(VISIBLE);
                setAlpha(0f);
                animate().alpha(1f).setDuration(300).start();
                if (list != null) {
                    list.animate().alpha(0f).scaleX(0.92f).scaleY(0.92f).setDuration(300).start();
                }
                lastFrame = 0;
                invalidate();
            } else {
                animate().alpha(0f).setDuration(250).withEndAction(() -> {
                    setVisibility(GONE);
                    setTranslationZ(0);
                    release();
                    moveTo(home);
                }).start();
                if (list != null) {
                    list.animate().alpha(1f).scaleX(1f).scaleY(1f).setDuration(300).start();
                }
            }
        }

        private void moveTo(FrameLayout parent) {
            if (parent == null || getParent() == parent) {
                return;
            }
            if (getParent() instanceof ViewGroup) {
                ((ViewGroup) getParent()).removeView(this);
            }
            parent.addView(this, LayoutHelper.createFrame(LayoutHelper.MATCH_PARENT, LayoutHelper.MATCH_PARENT));
        }

        private void release() {
            for (Bubble b : bubbles) {
                if (b.image != null) {
                    b.image.onDetachedFromWindow();
                }
            }
            bubbles.clear();
        }

        private void build() {
            release();
            int account = fragment.getCurrentAccount();
            MessagesController controller = MessagesController.getInstance(account);
            Random random = new Random();
            int w = Math.max(getWidth(), AndroidUtilities.displaySize.x);
            int h = Math.max(getHeight(), AndroidUtilities.displaySize.y);
            ArrayList<Long> ids = chosenDialogs(account);
            boolean many = ids.size() > 20;
            for (long id : ids) {
                TLObject peer = DialogObject.isUserDialog(id)
                    ? controller.getUser(id)
                    : controller.getChat(-id);
                if (peer == null) {
                    continue;
                }
                TLRPC.Dialog dialog = controller.dialogs_dict.get(id);
                boolean unread = dialog != null && dialog.unread_count > 0;
                Bubble b = new Bubble();
                b.dialogId = id;
                b.r = AndroidUtilities.dp((many ? 22 : 28) + (unread ? 6 : 0));
                b.x = b.r + random.nextFloat() * Math.max(1, w - b.r * 2);
                b.y = b.r + random.nextFloat() * Math.max(1, h - b.r * 2);
                b.vx = (random.nextFloat() - 0.5f) * AndroidUtilities.dp(200);
                b.vy = (random.nextFloat() - 0.5f) * AndroidUtilities.dp(200);
                b.image = new ImageReceiver(this);
                b.image.setRoundRadius((int) b.r);
                AvatarDrawable avatar = new AvatarDrawable();
                avatar.setInfo(account, peer);
                b.image.setForUserOrChat(peer, avatar);
                b.image.onAttachedToWindow();
                b.label = peer instanceof TLRPC.User
                    ? UserObject.getFirstName((TLRPC.User) peer)
                    : MwtkNames.chat((TLRPC.Chat) peer);
                bubbles.add(b);
            }
        }

        @Override
        protected void onDraw(Canvas canvas) {
            long now = System.currentTimeMillis();
            float dt = lastFrame == 0 ? 0 : Math.min((now - lastFrame) / 1000f, 0.05f);
            lastFrame = now;
            step(dt);
            int accent = MwtkConfig.accent();
            for (Bubble b : bubbles) {
                paint.setColor(accent);
                paint.setAlpha(70);
                canvas.drawCircle(b.x, b.y, b.r + AndroidUtilities.dp(5), paint);
                if (b.image != null) {
                    b.image.setImageCoords(b.x - b.r, b.y - b.r, b.r * 2, b.r * 2);
                    b.image.draw(canvas);
                    text.setTextSize(AndroidUtilities.dp(11));
                    String label = b.label == null ? "" : b.label;
                    if (label.length() > 12) {
                        label = label.substring(0, 11) + "…";
                    }
                    canvas.drawText(label, b.x, b.y + b.r + AndroidUtilities.dp(14), text);
                }
            }
            if (active) {
                postInvalidateOnAnimation();
            }
        }

        private void step(float dt) {
            if (dt <= 0) {
                return;
            }
            // No tilt and no gravity: on some phones the sensor pinned every
            // bubble to the bottom. Bubbles drift and bounce on their own.
            float minSpeed = AndroidUtilities.dp(18);
            float maxSpeed = AndroidUtilities.dp(1400);
            int w = getWidth();
            int h = getHeight();
            if (w <= 0 || h <= 0) {
                return; // just moved to another parent, not laid out yet
            }
            for (Bubble b : bubbles) {
                if (b == dragged) {
                    continue;
                }
                b.vx *= 0.992f;
                b.vy *= 0.992f;
                float speed = (float) Math.hypot(b.vx, b.vy);
                if (speed < minSpeed) {
                    double angle = speed < 1f ? Math.random() * 6.28 : Math.atan2(b.vy, b.vx);
                    b.vx = (float) Math.cos(angle) * minSpeed;
                    b.vy = (float) Math.sin(angle) * minSpeed;
                } else if (speed > maxSpeed) {
                    b.vx *= maxSpeed / speed;
                    b.vy *= maxSpeed / speed;
                }
                b.x += b.vx * dt;
                b.y += b.vy * dt;
                if (b.x < b.r) { b.x = b.r; b.vx = Math.abs(b.vx) * 0.7f; }
                if (b.x > w - b.r) { b.x = w - b.r; b.vx = -Math.abs(b.vx) * 0.7f; }
                if (b.y < b.r) { b.y = b.r; b.vy = Math.abs(b.vy) * 0.7f; }
                if (b.y > h - b.r) { b.y = h - b.r; b.vy = -Math.abs(b.vy) * 0.7f; }
            }
            for (int i = 0; i < bubbles.size(); i++) {
                Bubble a = bubbles.get(i);
                for (int j = i + 1; j < bubbles.size(); j++) {
                    Bubble b = bubbles.get(j);
                    float dx = b.x - a.x;
                    float dy = b.y - a.y;
                    float min = a.r + b.r + AndroidUtilities.dp(6);
                    float d2 = dx * dx + dy * dy;
                    if (d2 < min * min && d2 > 0.01f) {
                        float d = (float) Math.sqrt(d2);
                        float push = (min - d) / 2f;
                        float nx = dx / d, ny = dy / d;
                        boolean aFixed = a == dragged;
                        boolean bFixed = b == dragged;
                        if (aFixed && bFixed) {
                            continue;
                        }
                        float pa = bFixed ? push * 2 : (aFixed ? 0 : push);
                        float pb = aFixed ? push * 2 : (bFixed ? 0 : push);
                        a.x -= nx * pa; a.y -= ny * pa;
                        b.x += nx * pb; b.y += ny * pb;
                        float rel = (b.vx - a.vx) * nx + (b.vy - a.vy) * ny;
                        if (rel < 0) {
                            a.vx += nx * rel * 0.5f; a.vy += ny * rel * 0.5f;
                            b.vx -= nx * rel * 0.5f; b.vy -= ny * rel * 0.5f;
                        }
                    }
                }
            }
        }

        private Bubble at(float x, float y) {
            for (int i = bubbles.size() - 1; i >= 0; i--) {
                Bubble b = bubbles.get(i);
                float dx = x - b.x, dy = y - b.y;
                if (dx * dx + dy * dy <= (b.r + AndroidUtilities.dp(8)) * (b.r + AndroidUtilities.dp(8))) {
                    return b;
                }
            }
            return null;
        }

        @SuppressLint("ClickableViewAccessibility")
        @Override
        public boolean onTouchEvent(MotionEvent event) {
            if (!active) {
                return false;
            }
            switch (event.getActionMasked()) {
                case MotionEvent.ACTION_DOWN:
                    dragged = at(event.getX(), event.getY());
                    downX = event.getX();
                    downY = event.getY();
                    tracker = VelocityTracker.obtain();
                    tracker.addMovement(event);
                    return true;
                case MotionEvent.ACTION_MOVE:
                    if (tracker != null) {
                        tracker.addMovement(event);
                    }
                    if (dragged != null) {
                        dragged.x = event.getX();
                        dragged.y = event.getY();
                    }
                    return true;
                case MotionEvent.ACTION_UP:
                case MotionEvent.ACTION_CANCEL:
                    boolean tap = Math.abs(event.getX() - downX) < AndroidUtilities.dp(10)
                        && Math.abs(event.getY() - downY) < AndroidUtilities.dp(10);
                    if (dragged == null && tap && event.getActionMasked() == MotionEvent.ACTION_UP) {
                        toggle(); // a tap on the empty space closes the mode
                    } else if (dragged != null) {
                        if (tap && event.getActionMasked() == MotionEvent.ACTION_UP) {
                            open(dragged);
                        } else if (tracker != null) {
                            tracker.addMovement(event);
                            tracker.computeCurrentVelocity(1000);
                            dragged.vx = tracker.getXVelocity();
                            dragged.vy = tracker.getYVelocity();
                        }
                    }
                    dragged = null;
                    if (tracker != null) {
                        tracker.recycle();
                        tracker = null;
                    }
                    return true;
            }
            return true;
        }

        private void open(Bubble b) {
            // Open the chat and leave the floating mode, so the bubbles are
            // not still there when you come back from the chat.
            toggle();
            Bundle args = new Bundle();
            if (DialogObject.isUserDialog(b.dialogId)) {
                args.putLong("user_id", b.dialogId);
            } else {
                args.putLong("chat_id", -b.dialogId);
            }
            fragment.presentFragment(new ChatActivity(args));
        }
    }
}
