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
import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorEventListener;
import android.hardware.SensorManager;
import android.os.SystemClock;
import android.view.View;
import android.widget.FrameLayout;

import org.telegram.messenger.AndroidUtilities;
import org.telegram.ui.ActionBar.BaseFragment;
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

    /**
     * The profile becomes a glass hologram card: what was on screen before
     * it shows through (a soft snapshot), the card floats a little smaller
     * with a shining frame, scan lines and a sheen, and it turns in 3D when
     * the phone is tilted (no touches are taken: scrolling stays as is).
     */
    public static void attachWindow(BaseFragment fragment, FrameLayout root) {
        if (root == null) {
            return;
        }
        WindowView view = new WindowView(root.getContext(), fragment, root);
        root.addView(view, LayoutHelper.createFrame(LayoutHelper.MATCH_PARENT, LayoutHelper.MATCH_PARENT));
        // Always on top of the profile, never takes touches.
        view.setOutlineProvider(null);
        view.setTranslationZ(AndroidUtilities.dp(400));
    }

    @SuppressLint("ViewConstructor")
    private static final class WindowView extends View implements SensorEventListener {

        private static final float SEE_THROUGH = 0.26f;

        private final BaseFragment fragment;
        private final FrameLayout root;
        private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.FILTER_BITMAP_FLAG);
        private final Paint frame = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Matrix matrix = new Matrix();
        private final RectF rect = new RectF();
        private final long started = SystemClock.elapsedRealtime();
        private android.graphics.Bitmap behind;
        private boolean behindTried;
        private android.graphics.Bitmap lines;
        private android.graphics.BitmapShader linesShader;
        private int linesAccent;
        private SweepGradient rimShader;
        private int rimW;
        private int rimH;

        private SensorManager sensors;
        private float[] restGravity;
        private float tiltX;
        private float tiltY;
        private float shownX;
        private float shownY;
        private boolean applied;

        WindowView(Context context, BaseFragment fragment, FrameLayout root) {
            super(context);
            this.fragment = fragment;
            this.root = root;
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

        private boolean enabled() {
            MwtkConfig.load();
            return MwtkConfig.effects && MwtkConfig.flag(MwtkConfig.HOLO, true);
        }

        @Override
        protected void onAttachedToWindow() {
            super.onAttachedToWindow();
            if (enabled()) {
                sensors = (SensorManager) getContext().getSystemService(Context.SENSOR_SERVICE);
                Sensor gravity = sensors != null ? sensors.getDefaultSensor(Sensor.TYPE_GRAVITY) : null;
                if (gravity == null && sensors != null) {
                    gravity = sensors.getDefaultSensor(Sensor.TYPE_ACCELEROMETER);
                }
                if (gravity != null) {
                    sensors.registerListener(this, gravity, SensorManager.SENSOR_DELAY_GAME);
                }
            }
        }

        @Override
        protected void onDetachedFromWindow() {
            super.onDetachedFromWindow();
            if (sensors != null) {
                sensors.unregisterListener(this);
                sensors = null;
            }
            restore();
            if (behind != null) {
                behind.recycle();
                behind = null;
            }
        }

        @Override
        public void onSensorChanged(SensorEvent event) {
            float[] g = event.values;
            if (restGravity == null) {
                restGravity = new float[] { g[0], g[1], g[2] };
            }
            // Slowly forget the rest position, so the card follows how the
            // phone is held now and only reacts to tilting.
            for (int i = 0; i < 3; i++) {
                restGravity[i] += (g[i] - restGravity[i]) * 0.01f;
            }
            float dx = g[0] - restGravity[0];
            float dy = g[1] - restGravity[1];
            tiltY = Math.max(-18f, Math.min(18f, dx * 4.5f));
            tiltX = Math.max(-14f, Math.min(14f, -dy * 4.5f));
        }

        @Override
        public void onAccuracyChanged(Sensor sensor, int accuracy) {
        }

        private void restore() {
            if (applied) {
                applied = false;
                root.setRotationX(0);
                root.setRotationY(0);
                root.setScaleX(1f);
                root.setScaleY(1f);
            }
        }

        /** What was on screen before the profile: drawn small, shown softly. */
        private void snapshot() {
            behindTried = true;
            try {
                if (fragment == null || fragment.getParentLayout() == null) {
                    return;
                }
                java.util.List<BaseFragment> stack = fragment.getParentLayout().getFragmentStack();
                int index = stack.indexOf(fragment);
                BaseFragment previous = index > 0 ? stack.get(index - 1)
                    : (index < 0 && !stack.isEmpty() ? stack.get(stack.size() - 1) : null);
                View view = previous != null ? previous.getFragmentView() : null;
                if (view == null || view.getWidth() == 0 || view.getHeight() == 0) {
                    return;
                }
                float k = 0.2f;
                int w = Math.max(1, (int) (view.getWidth() * k));
                int h = Math.max(1, (int) (view.getHeight() * k));
                android.graphics.Bitmap small = android.graphics.Bitmap.createBitmap(w, h, android.graphics.Bitmap.Config.ARGB_8888);
                Canvas c = new Canvas(small);
                c.scale(k, k);
                view.draw(c);
                behind = small;
            } catch (Throwable ignore) {
                behind = null;
            }
        }

        @Override
        protected void onDraw(Canvas canvas) {
            if (!enabled() || getWidth() == 0) {
                restore();
                return;
            }
            if (!behindTried) {
                snapshot();
            }
            float t = (SystemClock.elapsedRealtime() - started) / 1000f;
            int accent = MwtkConfig.accent();
            int w = getWidth();
            int h = getHeight();

            // The card turns with the phone; slightly smaller to float.
            shownX += (tiltX - shownX) * 0.15f;
            shownY += (tiltY - shownY) * 0.15f;
            root.setCameraDistance(8000f * getResources().getDisplayMetrics().density);
            root.setPivotX(root.getWidth() / 2f);
            root.setPivotY(root.getHeight() / 2f);
            root.setRotationX(-3f + shownX);
            root.setRotationY(5f + shownY);
            root.setScaleX(0.94f);
            root.setScaleY(0.94f);
            applied = true;

            // See-through: what was behind shows on the card.
            float flicker = (t % 6.7f) < 0.07f ? 0.6f : 1f;
            if (behind != null) {
                paint.setShader(null);
                paint.setAlpha((int) (255 * SEE_THROUGH));
                rect.set(0, 0, w, h);
                canvas.drawBitmap(behind, null, rect, paint);
            }
            paint.setShader(null);
            paint.setColor(accent);
            paint.setAlpha((int) (22 * flicker));
            canvas.drawRect(0, 0, w, h, paint);

            // Scan lines moving down: one small bitmap repeated by a shader.
            int step = Math.max(AndroidUtilities.dp(3), 3);
            if (lines == null || linesAccent != accent) {
                linesAccent = accent;
                lines = android.graphics.Bitmap.createBitmap(8, step * 16, android.graphics.Bitmap.Config.ARGB_8888);
                Canvas c = new Canvas(lines);
                Paint p = new Paint();
                p.setColor(0xffffffff);
                p.setAlpha(16);
                for (int y = 0; y < lines.getHeight(); y += step) {
                    c.drawRect(0, y, 8, y + Math.max(1, AndroidUtilities.dp(0.7f)), p);
                }
                linesShader = new android.graphics.BitmapShader(lines, Shader.TileMode.REPEAT, Shader.TileMode.REPEAT);
                rimShader = null;
            }
            matrix.setTranslate(0, (t * AndroidUtilities.dp(14)) % (step * 16));
            linesShader.setLocalMatrix(matrix);
            paint.setShader(linesShader);
            paint.setAlpha(255);
            canvas.drawRect(0, 0, w, h, paint);

            // A sheen that follows the tilt.
            float along = 0.5f - (5f + shownY) / 60f;
            float sx = w * along;
            paint.setShader(new LinearGradient(sx - w * 0.5f, 0, sx + w * 0.5f, h * 0.6f,
                new int[] { 0x00ffffff, 0x1effffff, 0x2078e6ff, (accent & 0x00ffffff) | 0x1c000000, 0x00ffffff },
                new float[] { 0f, 0.45f, 0.5f, 0.55f, 1f }, Shader.TileMode.CLAMP));
            canvas.drawRect(0, 0, w, h, paint);
            paint.setShader(null);

            // A shining iridescent frame.
            if (rimShader == null || rimW != w || rimH != h) {
                rimW = w;
                rimH = h;
                rimShader = new SweepGradient(w / 2f, h / 2f,
                    new int[] { accent, 0xff50e1ff, 0xffb06bff, accent }, null);
            }
            matrix.setRotate((t * 60f) % 360f, w / 2f, h / 2f);
            rimShader.setLocalMatrix(matrix);
            frame.setShader(rimShader);
            float inset = AndroidUtilities.dp(1.5f);
            rect.set(inset, inset, w - inset, h - inset);
            float radius = AndroidUtilities.dp(14);
            for (int i = 3; i > 0; i--) {
                frame.setStrokeWidth(AndroidUtilities.dp(1.5f + i * 2.5f));
                frame.setAlpha((int) (255 * 0.12f * (4 - i)));
                canvas.drawRoundRect(rect, radius, radius, frame);
            }
            frame.setStrokeWidth(AndroidUtilities.dp(2));
            frame.setAlpha(240);
            canvas.drawRoundRect(rect, radius, radius, frame);

            postInvalidateDelayed(33);
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
