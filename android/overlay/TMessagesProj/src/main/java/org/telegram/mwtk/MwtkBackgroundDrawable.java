package org.telegram.mwtk;

import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.ColorFilter;
import android.graphics.LinearGradient;
import android.graphics.Paint;
import android.graphics.PixelFormat;
import android.graphics.RadialGradient;
import android.graphics.Rect;
import android.graphics.Shader;
import android.graphics.Typeface;
import android.graphics.drawable.Drawable;
import android.os.BatteryManager;
import android.os.PowerManager;
import android.os.SystemClock;
import android.text.TextPaint;

import androidx.annotation.NonNull;

import org.telegram.messenger.AndroidUtilities;
import org.telegram.messenger.ApplicationLoader;
import org.telegram.ui.ActionBar.Theme;

import java.util.Random;

/**
 * The MWTKgram backdrop of the chat list: a deep gradient with slowly
 * floating "doxmev" / "MWTK" labels and optional wireframe figures.
 */
public class MwtkBackgroundDrawable extends Drawable implements Runnable {

    private static final long FRAME = 33;
    private static final int LABELS = 9;
    private static final int FIGURES = 4;

    private static class Label {
        String text;
        float x;
        float y;
        float vx;
        float vy;
        float size;
        float rotation;
        float depth;
        float alpha;
        float phase;
    }

    private static class Figure {
        float x;
        float y;
        float size;
        float ax;
        float ay;
        float speed;
        float depth;
    }

    private final Paint background = new Paint();
    private final Paint glow = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final TextPaint text = new TextPaint(Paint.ANTI_ALIAS_FLAG);
    private final Paint line = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Label[] labels = new Label[LABELS];
    private final Figure[] figures = new Figure[FIGURES];
    private final Runnable configListener = this::invalidateSelf;

    private int shaderWidth;
    private int shaderHeight;
    private int shaderPreset = -1;
    private long lastFrame;
    private long lastBatteryCheck;
    private boolean lowPower;
    private boolean scheduled;
    private float time;

    public MwtkBackgroundDrawable() {
        Random random = new Random(1337);
        for (int i = 0; i < LABELS; i++) {
            Label label = new Label();
            label.text = (i % 3 == 1) ? "MWTK" : "doxmev";
            label.x = random.nextFloat();
            label.y = random.nextFloat();
            label.depth = 0.4f + random.nextFloat() * 0.9f;
            label.vx = (random.nextFloat() - 0.5f) * 0.012f;
            label.vy = -0.006f - random.nextFloat() * 0.01f;
            label.size = 13 + random.nextFloat() * 26;
            label.rotation = (random.nextFloat() - 0.5f) * 22f;
            label.alpha = 0.10f + random.nextFloat() * 0.12f;
            label.phase = random.nextFloat() * 6.28f;
            labels[i] = label;
        }
        for (int i = 0; i < FIGURES; i++) {
            Figure figure = new Figure();
            figure.x = random.nextFloat();
            figure.y = random.nextFloat();
            figure.size = 18 + random.nextFloat() * 22;
            figure.ax = random.nextFloat() * 6.28f;
            figure.ay = random.nextFloat() * 6.28f;
            figure.speed = 0.3f + random.nextFloat() * 0.5f;
            figure.depth = 0.5f + random.nextFloat();
            figures[i] = figure;
        }
        line.setStyle(Paint.Style.STROKE);
        line.setStrokeCap(Paint.Cap.ROUND);
        MwtkConfig.addListener(configListener);
    }

    @Override
    public void draw(@NonNull Canvas canvas) {
        MwtkConfig.load();
        final Rect bounds = getBounds();
        if (!MwtkConfig.effects) {
            canvas.drawColor(Theme.getColor(Theme.key_windowBackgroundWhite));
            return;
        }
        final int[] palette = MwtkConfig.palette();
        updateShader(bounds, palette);
        canvas.drawRect(bounds, background);
        canvas.drawRect(bounds, glow);

        final long now = SystemClock.elapsedRealtime();
        final float dt = lastFrame == 0 ? 0f : Math.min((now - lastFrame) / 1000f, 0.1f);
        lastFrame = now;
        time += dt;

        final float w = bounds.width();
        final float h = bounds.height();
        final float tx = MwtkMotion.tiltX * AndroidUtilities.dp(18);
        final float ty = MwtkMotion.tiltY * AndroidUtilities.dp(18);
        final int style = MwtkConfig.style;

        if (MwtkConfig.figures) {
            drawFigures(canvas, w, h, tx, ty, dt, palette[2], style);
        }
        if (MwtkConfig.labels) {
            drawLabels(canvas, w, h, tx, ty, dt, palette[3], style);
        }
        scheduleNext(now);
    }

    private void updateShader(Rect bounds, int[] palette) {
        if (shaderWidth == bounds.width() && shaderHeight == bounds.height() && shaderPreset == MwtkConfig.preset) {
            return;
        }
        shaderWidth = bounds.width();
        shaderHeight = bounds.height();
        shaderPreset = MwtkConfig.preset;
        background.setShader(new LinearGradient(0, bounds.top, 0, bounds.bottom, palette[0], palette[1], Shader.TileMode.CLAMP));
        final int accent = palette[2];
        glow.setShader(new RadialGradient(
            bounds.left + shaderWidth * 0.85f,
            bounds.top,
            Math.max(1, shaderHeight * 0.7f),
            Color.argb(70, Color.red(accent), Color.green(accent), Color.blue(accent)),
            Color.argb(0, Color.red(accent), Color.green(accent), Color.blue(accent)),
            Shader.TileMode.CLAMP));
    }

    private void drawLabels(Canvas canvas, float w, float h, float tx, float ty, float dt, int color, int style) {
        text.setTypeface(style == MwtkConfig.STYLE_PIXEL ? Typeface.MONOSPACE : Typeface.create(Typeface.DEFAULT, Typeface.BOLD));
        text.setAntiAlias(style != MwtkConfig.STYLE_PIXEL);
        final Rect bounds = getBounds();
        for (Label label : labels) {
            label.x += label.vx * dt;
            label.y += label.vy * dt;
            if (label.y < -0.1f) {
                label.y = 1.1f;
            }
            if (label.x < -0.3f) {
                label.x = 1.1f;
            } else if (label.x > 1.2f) {
                label.x = -0.2f;
            }
            final float pulse = 0.75f + 0.25f * (float) Math.sin(time * 0.8f + label.phase);
            final float x = bounds.left + label.x * w + tx * label.depth;
            final float y = bounds.top + label.y * h + ty * label.depth;
            text.setTextSize(AndroidUtilities.dp(label.size * label.depth));
            int alpha = (int) (255 * label.alpha * pulse * (style == MwtkConfig.STYLE_GLASS ? 0.7f : 1f));
            canvas.save();
            canvas.rotate(label.rotation + 3f * (float) Math.sin(time * 0.3f + label.phase), x, y);
            if (style == MwtkConfig.STYLE_COMIC) {
                text.setStyle(Paint.Style.STROKE);
                text.setStrokeWidth(AndroidUtilities.dp(2.5f));
                text.setColor(Color.argb(Math.min(255, alpha * 2), 0, 0, 0));
                canvas.drawText(label.text, x, y, text);
                text.setStyle(Paint.Style.FILL);
            }
            if (style == MwtkConfig.STYLE_NEON) {
                text.setShadowLayer(AndroidUtilities.dp(8), 0, 0, Color.argb(alpha, Color.red(color), Color.green(color), Color.blue(color)));
            } else {
                text.clearShadowLayer();
            }
            text.setColor(Color.argb(alpha, Color.red(color), Color.green(color), Color.blue(color)));
            canvas.drawText(label.text, x, y, text);
            canvas.restore();
        }
        text.clearShadowLayer();
    }

    private void drawFigures(Canvas canvas, float w, float h, float tx, float ty, float dt, int color, int style) {
        final Rect bounds = getBounds();
        line.setColor(Color.argb(style == MwtkConfig.STYLE_GLASS ? 50 : 80, Color.red(color), Color.green(color), Color.blue(color)));
        line.setStrokeWidth(AndroidUtilities.dp(1.3f));
        line.setAntiAlias(style != MwtkConfig.STYLE_PIXEL);
        final float[][] v = new float[8][3];
        for (Figure figure : figures) {
            figure.ax += figure.speed * dt;
            figure.ay += figure.speed * 0.7f * dt;
            final float size = AndroidUtilities.dp(figure.size) * figure.depth;
            final float cx = bounds.left + figure.x * w + tx * figure.depth * 1.4f;
            final float cy = bounds.top + figure.y * h + ty * figure.depth * 1.4f;
            final float cxA = (float) Math.cos(figure.ax), sxA = (float) Math.sin(figure.ax);
            final float cyA = (float) Math.cos(figure.ay), syA = (float) Math.sin(figure.ay);
            for (int i = 0; i < 8; i++) {
                float x = (i & 1) != 0 ? 1 : -1;
                float y = (i & 2) != 0 ? 1 : -1;
                float z = (i & 4) != 0 ? 1 : -1;
                float y1 = y * cxA - z * sxA;
                float z1 = y * sxA + z * cxA;
                float x2 = x * cyA + z1 * syA;
                float z2 = -x * syA + z1 * cyA;
                float k = 3f / (4f + z2);
                v[i][0] = cx + x2 * size * k;
                v[i][1] = cy + y1 * size * k;
            }
            for (int i = 0; i < 8; i++) {
                for (int bit = 1; bit <= 4; bit <<= 1) {
                    int j = i | bit;
                    if (j != i) {
                        canvas.drawLine(v[i][0], v[i][1], v[j][0], v[j][1], line);
                    }
                }
            }
        }
    }

    private void scheduleNext(long now) {
        if (!(MwtkConfig.labels || MwtkConfig.figures) || !isVisible()) {
            lastFrame = 0;
            return;
        }
        if (MwtkConfig.battery && now - lastBatteryCheck > 10000) {
            lastBatteryCheck = now;
            lowPower = isLowPower();
        }
        if (lowPower) {
            lastFrame = 0;
            return;
        }
        if (!scheduled) {
            scheduled = true;
            scheduleSelf(this, SystemClock.uptimeMillis() + FRAME);
        }
    }

    @Override
    public void run() {
        scheduled = false;
        invalidateSelf();
    }

    @Override
    public boolean setVisible(boolean visible, boolean restart) {
        boolean changed = super.setVisible(visible, restart);
        if (!visible) {
            unscheduleSelf(this);
            scheduled = false;
            lastFrame = 0;
        } else {
            invalidateSelf();
        }
        return changed;
    }

    private static boolean isLowPower() {
        try {
            Context context = ApplicationLoader.applicationContext;
            PowerManager power = (PowerManager) context.getSystemService(Context.POWER_SERVICE);
            if (power != null && power.isPowerSaveMode()) {
                return true;
            }
            Intent status = context.registerReceiver(null, new IntentFilter(Intent.ACTION_BATTERY_CHANGED));
            if (status == null) {
                return false;
            }
            int plugged = status.getIntExtra(BatteryManager.EXTRA_PLUGGED, 0);
            int level = status.getIntExtra(BatteryManager.EXTRA_LEVEL, -1);
            int scale = status.getIntExtra(BatteryManager.EXTRA_SCALE, -1);
            if (plugged != 0 || level < 0 || scale <= 0) {
                return false;
            }
            return level * 100 / scale < 20;
        } catch (Throwable ignore) {
            return false;
        }
    }

    @Override
    public void setAlpha(int alpha) {
    }

    @Override
    public void setColorFilter(ColorFilter colorFilter) {
    }

    @Override
    public int getOpacity() {
        return PixelFormat.OPAQUE;
    }
}
