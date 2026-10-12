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
import org.telegram.ui.SettingsActivity;

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
        FloatingView floating = new FloatingView(contentView.getContext(), fragment, list);
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
        int action;
    }

    @SuppressLint("ViewConstructor")
    private static final class FloatingView extends View {

        private static final int ACTION_NONE = 0;
        private static final int ACTION_SETTINGS = 1;
        private static final int ACTION_CLOSE = 2;

        private final BaseFragment fragment;
        private final View list;
        private final ArrayList<Bubble> bubbles = new ArrayList<>();
        private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final TextPaint text = new TextPaint(Paint.ANTI_ALIAS_FLAG);
        private VelocityTracker tracker;
        private Bubble dragged;
        private float downX, downY;
        private long lastFrame;
        private boolean active;

        FloatingView(Context context, BaseFragment fragment, View list) {
            super(context);
            this.fragment = fragment;
            this.list = list;
            text.setTextAlign(Paint.Align.CENTER);
            text.setColor(0xffffffff);
            text.setTypeface(AndroidUtilities.bold());
        }

        void toggle() {
            active = !active;
            if (active) {
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
                    release();
                }).start();
                if (list != null) {
                    list.animate().alpha(1f).scaleX(1f).scaleY(1f).setDuration(300).start();
                }
            }
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
            ArrayList<TLRPC.Dialog> dialogs = MwtkHidden.filter(controller.getDialogs(0));
            Random random = new Random();
            int w = Math.max(getWidth(), AndroidUtilities.displaySize.x);
            int h = Math.max(getHeight(), AndroidUtilities.displaySize.y);
            int count = 0;
            for (TLRPC.Dialog dialog : dialogs) {
                if (count >= 22) {
                    break;
                }
                TLObject peer = DialogObject.isUserDialog(dialog.id)
                    ? controller.getUser(dialog.id)
                    : controller.getChat(-dialog.id);
                if (peer == null) {
                    continue;
                }
                Bubble b = new Bubble();
                b.dialogId = dialog.id;
                b.r = AndroidUtilities.dp(dialog.unread_count > 0 ? 34 : 28);
                b.x = b.r + random.nextFloat() * (w - b.r * 2);
                b.y = b.r + random.nextFloat() * (h - b.r * 2);
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
                count++;
            }
            addAction(w, h, ACTION_SETTINGS, "⚙");
            addAction(w, h, ACTION_CLOSE, "✕");
        }

        private void addAction(int w, int h, int action, String label) {
            Bubble b = new Bubble();
            b.action = action;
            b.label = label;
            b.r = AndroidUtilities.dp(24);
            b.x = w / 2f + (action == ACTION_CLOSE ? b.r * 2 : -b.r * 2);
            b.y = h - b.r * 3;
            bubbles.add(b);
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
                } else {
                    paint.setAlpha(230);
                    canvas.drawCircle(b.x, b.y, b.r, paint);
                    text.setTextSize(AndroidUtilities.dp(20));
                    canvas.drawText(b.label, b.x, b.y + AndroidUtilities.dp(7), text);
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
            float gx = MwtkMotion.tiltX * AndroidUtilities.dp(900);
            float gy = MwtkMotion.tiltY * AndroidUtilities.dp(900) + AndroidUtilities.dp(60);
            int w = getWidth();
            int h = getHeight();
            for (Bubble b : bubbles) {
                if (b == dragged) {
                    continue;
                }
                b.vx += gx * dt;
                b.vy += gy * dt;
                b.vx *= 0.995f;
                b.vy *= 0.995f;
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
                        if (a != dragged) { a.x -= nx * push; a.y -= ny * push; }
                        if (b != dragged) { b.x += nx * push; b.y += ny * push; }
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
                    if (dragged != null) {
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
            if (b.action == ACTION_CLOSE) {
                toggle();
                return;
            }
            if (b.action == ACTION_SETTINGS) {
                fragment.presentFragment(new SettingsActivity());
                return;
            }
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
