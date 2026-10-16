package org.telegram.mwtk;

import android.annotation.SuppressLint;
import android.content.Context;
import android.graphics.Canvas;
import android.graphics.LinearGradient;
import android.graphics.Matrix;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RectF;
import android.graphics.Shader;
import android.graphics.SweepGradient;
import android.os.SystemClock;
import android.view.View;
import android.widget.FrameLayout;

import org.telegram.messenger.AndroidUtilities;
import org.telegram.ui.Components.BackupImageView;
import org.telegram.ui.Components.LayoutHelper;

/** Hologram over the profile picture: iridescent rim, scan lines, a light sweep. */
public final class MwtkHolo {

    private MwtkHolo() {
    }

    public static void attach(FrameLayout container, BackupImageView avatar) {
        if (container == null || avatar == null) {
            return;
        }
        container.addView(new HoloView(container.getContext(), avatar),
            LayoutHelper.createFrame(LayoutHelper.MATCH_PARENT, LayoutHelper.MATCH_PARENT));
    }

    /** The whole profile window: tint, moving scan lines, a light sweep, flicker and a frame. */
    public static void attachWindow(FrameLayout root) {
        if (root == null) {
            return;
        }
        WindowView view = new WindowView(root.getContext());
        root.addView(view, LayoutHelper.createFrame(LayoutHelper.MATCH_PARENT, LayoutHelper.MATCH_PARENT));
        // Always on top of the profile, never takes touches.
        view.setOutlineProvider(null);
        view.setTranslationZ(AndroidUtilities.dp(400));
    }

    private static final class WindowView extends View {

        private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint frame = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Matrix matrix = new Matrix();
        private final RectF rect = new RectF();
        private final long started = SystemClock.elapsedRealtime();
        private android.graphics.Bitmap lines;
        private android.graphics.BitmapShader linesShader;
        private int linesAccent;
        private SweepGradient rimShader;

        WindowView(Context context) {
            super(context);
            setWillNotDraw(false);
            setClickable(false);
            setFocusable(false);
            setImportantForAccessibility(IMPORTANT_FOR_ACCESSIBILITY_NO);
            frame.setStyle(Paint.Style.STROKE);
        }

        @Override
        public boolean onTouchEvent(android.view.MotionEvent event) {
            return false;
        }

        @Override
        protected void onDraw(Canvas canvas) {
            MwtkConfig.load();
            if (!MwtkConfig.effects || !MwtkConfig.flag(MwtkConfig.HOLO, true) || getWidth() == 0) {
                return;
            }
            float t = (SystemClock.elapsedRealtime() - started) / 1000f;
            int accent = MwtkConfig.accent();
            int w = getWidth();
            int h = getHeight();

            // Tint of the theme color, slowly breathing, with a rare flicker.
            float flicker = (t % 5.3f) < 0.08f ? 0.5f : 1f;
            paint.setShader(null);
            paint.setColor(accent);
            paint.setAlpha((int) (255 * (0.07f + 0.03f * (float) Math.sin(t * 1.3f)) * flicker));
            canvas.drawRect(0, 0, w, h, paint);

            // Scan lines moving down: one small bitmap repeated by a shader.
            int step = Math.max(AndroidUtilities.dp(3), 3);
            if (lines == null || linesAccent != accent) {
                linesAccent = accent;
                lines = android.graphics.Bitmap.createBitmap(8, step * 16, android.graphics.Bitmap.Config.ARGB_8888);
                Canvas c = new Canvas(lines);
                Paint p = new Paint();
                p.setColor(accent);
                p.setAlpha(30);
                for (int y = 0; y < lines.getHeight(); y += step) {
                    c.drawRect(0, y, 8, y + Math.max(1, AndroidUtilities.dp(0.7f)), p);
                }
                linesShader = new android.graphics.BitmapShader(lines, Shader.TileMode.REPEAT, Shader.TileMode.REPEAT);
                rimShader = new SweepGradient(w / 2f, h / 2f,
                    new int[] { accent, 0xffaa5aff, 0xff00e6ff, accent }, null);
            }
            matrix.setTranslate(0, (t * AndroidUtilities.dp(14)) % (step * 16));
            linesShader.setLocalMatrix(matrix);
            paint.setShader(linesShader);
            paint.setAlpha(255);
            canvas.drawRect(0, 0, w, h, paint);

            // A light sweep crossing the window from time to time.
            float phase = (t / 4.5f) % 1f;
            float y = -h * 0.4f + phase * h * 1.8f;
            int light = 0x22ffffff;
            paint.setShader(new LinearGradient(0, y, 0, y + h * 0.25f,
                new int[] { 0x00ffffff, light, 0x00ffffff }, null, Shader.TileMode.CLAMP));
            canvas.drawRect(0, Math.max(0, y), w, Math.min(h, y + h * 0.25f), paint);
            paint.setShader(null);

            // A shining iridescent frame.
            matrix.setRotate((t * 30f) % 360f, w / 2f, h / 2f);
            rimShader.setLocalMatrix(matrix);
            frame.setShader(rimShader);
            frame.setStrokeWidth(AndroidUtilities.dp(2));
            frame.setAlpha(210);
            rect.set(AndroidUtilities.dp(1), AndroidUtilities.dp(1), w - AndroidUtilities.dp(1), h - AndroidUtilities.dp(1));
            canvas.drawRect(rect, frame);

            postInvalidateDelayed(40); // ~25 fps is plenty for a slow sweep
        }
    }

    @SuppressLint("ViewConstructor")
    private static final class HoloView extends View {

        private final BackupImageView avatar;
        private final Paint rim = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint lines = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint sweep = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Matrix matrix = new Matrix();
        private final RectF rect = new RectF();
        private final Path clip = new Path();
        private final long started = SystemClock.elapsedRealtime();

        HoloView(Context context, BackupImageView avatar) {
            super(context);
            this.avatar = avatar;
            rim.setStyle(Paint.Style.STROKE);
            lines.setColor(0x14ffffff);
            setWillNotDraw(false);
        }

        @Override
        protected void onDraw(Canvas canvas) {
            MwtkConfig.load();
            if (!MwtkConfig.effects || !MwtkConfig.flag(MwtkConfig.HOLO, true) || getWidth() == 0) {
                return;
            }
            int[] radii = avatar.getImageReceiver().getRoundRadius();
            float radius = radii != null && radii.length > 0 ? radii[0] : getWidth() / 2f;
            float t = (SystemClock.elapsedRealtime() - started) / 1000f;
            int accent = MwtkConfig.accent();
            rect.set(0, 0, getWidth(), getHeight());

            SweepGradient gradient = new SweepGradient(getWidth() / 2f, getHeight() / 2f,
                new int[] { accent, 0xffaa5aff, 0xff00e6ff, 0xff78ffaa, accent }, null);
            matrix.setRotate((t * 40f) % 360f, getWidth() / 2f, getHeight() / 2f);
            gradient.setLocalMatrix(matrix);
            rim.setShader(gradient);
            for (int i = 3; i > 0; i--) {
                rim.setStrokeWidth(AndroidUtilities.dp(1.2f + i * 1.4f));
                rim.setAlpha(40 * (4 - i));
                canvas.drawRoundRect(rect, radius, radius, rim);
            }
            rim.setAlpha(230);
            rim.setStrokeWidth(AndroidUtilities.dp(1.6f));
            canvas.drawRoundRect(rect, radius, radius, rim);

            canvas.save();
            clip.reset();
            clip.addRoundRect(rect, radius, radius, Path.Direction.CW);
            canvas.clipPath(clip);
            float step = AndroidUtilities.dp(4);
            float shift = (t * AndroidUtilities.dp(12)) % step;
            for (float y = shift; y < getHeight(); y += step) {
                canvas.drawLine(0, y, getWidth(), y, lines);
            }
            float phase = (t / 3.2f) % 1f;
            float x = -getWidth() * 0.5f + phase * getWidth() * 2f;
            sweep.setShader(new LinearGradient(x, 0, x + getWidth() * 0.4f, getHeight(),
                new int[] { 0x00ffffff, 0x2ea0e6ff, 0x00ffffff }, null, Shader.TileMode.CLAMP));
            canvas.drawRect(rect, sweep);
            canvas.restore();
            postInvalidateDelayed(40);
        }
    }
}
