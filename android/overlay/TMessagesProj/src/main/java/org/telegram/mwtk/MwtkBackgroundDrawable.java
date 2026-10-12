package org.telegram.mwtk;

import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.graphics.Bitmap;
import android.graphics.BlurMaskFilter;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.ColorFilter;
import android.graphics.LinearGradient;
import android.graphics.Paint;
import android.graphics.Path;
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

import java.util.Calendar;
import java.util.Random;

/**
 * The MWTKgram backdrop: a deep gradient or a light vertical scene (forest,
 * neon, space, city, underwater) with floating "doxmev" / "MWTK" labels,
 * optional wireframe figures and seasonal snow, leaves or petals.
 */
public class MwtkBackgroundDrawable extends Drawable implements Runnable {

    public static final String[] SCENES = { "Без сцены", "Лес", "Неон", "Космос", "Город", "Под водой" };

    private static final long FRAME = 33;
    private static final int LABELS = 9;
    private static final int FIGURES = 4;
    private static final int FLAKES = 36;

    private static class Label {
        String text;
        float x, y, vx, vy, size, rotation, depth, alpha, phase;
        Bitmap bitmap;
        int bitmapKey;
    }

    private static class Figure {
        float x, y, size, ax, ay, speed, depth;
    }

    private static class Flake {
        float x, y, vx, vy, size, angle, spin, sway;
    }

    private final Paint background = new Paint();
    private final Paint glow = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint line = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint bitmapPaint = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.FILTER_BITMAP_FLAG);
    private final Label[] labels = new Label[LABELS];
    private final Figure[] figures = new Figure[FIGURES];
    private final Flake[] flakes = new Flake[FLAKES];
    private final Random random = new Random(1337);
    private final Runnable configListener = this::invalidateSelf;
    private final Path path = new Path();
    private final float[][] vertices = new float[8][2];

    private Bitmap scene;
    private int sceneKey;
    private int shaderWidth;
    private int shaderHeight;
    private int shaderKey;
    private long lastFrame;
    private long lastBatteryCheck;
    private boolean lowPower;
    private boolean scheduled;
    private float time;
    private float pulse;

    public MwtkBackgroundDrawable() {
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
        for (int i = 0; i < FLAKES; i++) {
            Flake flake = new Flake();
            spawn(flake, true);
            flakes[i] = flake;
        }
        line.setStyle(Paint.Style.STROKE);
        line.setStrokeCap(Paint.Cap.ROUND);
        MwtkConfig.addListener(configListener);
    }

    // ------------------------------------------------------------ seasons

    /** 0 none, 1 snow, 2 leaves, 3 petals. */
    private static int season() {
        int month = Calendar.getInstance().get(Calendar.MONTH) + 1;
        if (month == 12 || month <= 2) {
            return 1;
        } else if (month >= 9) {
            return 2;
        } else if (month >= 3 && month <= 5) {
            return 3;
        }
        return 0;
    }

    private static boolean nightEvent() {
        return MwtkConfig.flag(MwtkConfig.NIGHT, true)
            && Calendar.getInstance().get(Calendar.HOUR_OF_DAY) == 3;
    }

    private void spawn(Flake flake, boolean initial) {
        flake.x = random.nextFloat();
        flake.y = initial ? random.nextFloat() : -0.03f;
        flake.vx = (random.nextFloat() - 0.4f) * 0.02f;
        flake.vy = 0.025f + random.nextFloat() * 0.035f;
        flake.size = 1.5f + random.nextFloat() * 3.5f;
        flake.angle = random.nextFloat() * 360f;
        flake.spin = (random.nextFloat() - 0.5f) * 120f;
        flake.sway = random.nextFloat() * 6.28f;
    }

    // ------------------------------------------------------------ drawing

    @Override
    public void draw(@NonNull Canvas canvas) {
        MwtkConfig.load();
        final Rect bounds = getBounds();
        if (!MwtkConfig.effects) {
            canvas.drawColor(Theme.getColor(Theme.key_windowBackgroundWhite));
            return;
        }
        final int[] palette = MwtkConfig.palette();
        final int accent = MwtkConfig.accent();
        final int sceneIndex = MwtkConfig.num(MwtkConfig.SCENE, 0);
        if (sceneIndex > 0) {
            drawScene(canvas, bounds, sceneIndex, accent);
        } else {
            updateShader(bounds, palette, accent);
            canvas.drawRect(bounds, background);
            canvas.drawRect(bounds, glow);
        }

        final boolean animate = MwtkConfig.animate();
        final long now = SystemClock.elapsedRealtime();
        final float dt = (!animate || lastFrame == 0) ? 0f : Math.min((now - lastFrame) / 1000f, 0.1f);
        lastFrame = now;
        time += dt;
        pulse = Math.max(0f, pulse - dt * 4f);
        if (MwtkConfig.flag(MwtkConfig.MUSIC_PULSE, true) && animate) {
            pulse = Math.max(pulse, MwtkBeat.level());
        }

        final float w = bounds.width();
        final float h = bounds.height();
        final float tx = MwtkMotion.tiltX * AndroidUtilities.dp(18);
        final float ty = MwtkMotion.tiltY * AndroidUtilities.dp(18);
        final int style = MwtkConfig.style;

        if (MwtkConfig.figures) {
            drawFigures(canvas, bounds, w, h, tx, ty, dt * (1f + pulse * 2.5f), accent, style);
        }
        if (MwtkConfig.labels) {
            drawLabels(canvas, bounds, w, h, tx, ty, dt, palette[3], style);
        }
        if (MwtkConfig.flag(MwtkConfig.SEASONS, true)) {
            drawSeason(canvas, bounds, w, h, dt);
        }
        if (animate) {
            scheduleNext(now);
        }
    }

    private void updateShader(Rect bounds, int[] palette, int accent) {
        int key = MwtkConfig.preset * 31 + accent;
        if (shaderWidth == bounds.width() && shaderHeight == bounds.height() && shaderKey == key) {
            return;
        }
        shaderWidth = bounds.width();
        shaderHeight = bounds.height();
        shaderKey = key;
        background.setShader(new LinearGradient(0, bounds.top, 0, bounds.bottom, palette[0], palette[1], Shader.TileMode.CLAMP));
        glow.setShader(new RadialGradient(
            bounds.left + shaderWidth * 0.85f,
            bounds.top,
            Math.max(1, shaderHeight * 0.7f),
            Color.argb(70, Color.red(accent), Color.green(accent), Color.blue(accent)),
            Color.argb(0, Color.red(accent), Color.green(accent), Color.blue(accent)),
            Shader.TileMode.CLAMP));
    }

    /** Scenes are rendered once per size into a bitmap: cheap to draw. */
    private void drawScene(Canvas canvas, Rect bounds, int index, int accent) {
        int width = Math.max(1, bounds.width() / 2);
        int height = Math.max(1, bounds.height() / 2);
        boolean night = nightEvent();
        int key = (index * 1000003 + width * 31 + height) ^ accent ^ (night ? 0x5a5a : 0);
        if (scene == null || sceneKey != key) {
            if (scene != null) {
                scene.recycle();
            }
            scene = Bitmap.createBitmap(width, height, Bitmap.Config.RGB_565);
            sceneKey = key;
            renderScene(new Canvas(scene), width, height, index, accent, night);
        }
        canvas.drawBitmap(scene, null, bounds, bitmapPaint);
    }

    private void renderScene(Canvas c, int w, int h, int index, int accent, boolean night) {
        Paint p = new Paint(Paint.ANTI_ALIAS_FLAG);
        Random r = new Random(index * 7919L);
        switch (index) {
            case 1: { // Forest at night.
                p.setShader(new LinearGradient(0, 0, 0, h, night ? 0xff1a0003 : 0xff06120f, 0xff020403, Shader.TileMode.CLAMP));
                c.drawRect(0, 0, w, h, p);
                p.setShader(null);
                p.setColor(night ? 0xffd01020 : 0xffe8e6d0);
                c.drawCircle(w * 0.75f, h * 0.16f, w * 0.07f, p);
                p.setColor(0x22ffffff);
                for (int layer = 0; layer < 3; layer++) {
                    int shade = 10 + layer * 6;
                    p.setColor(Color.rgb(shade / 2, shade, shade / 2 + 2));
                    float base = h * (0.55f + layer * 0.15f);
                    for (int i = 0; i < 9 + layer * 4; i++) {
                        float x = r.nextFloat() * w;
                        float th = h * (0.25f + r.nextFloat() * 0.2f) * (1 - layer * 0.15f);
                        float tw = th * 0.32f;
                        path.reset();
                        path.moveTo(x, base - th);
                        path.lineTo(x - tw, base);
                        path.lineTo(x + tw, base);
                        path.close();
                        c.drawPath(path, p);
                    }
                    c.drawRect(0, base, w, h, p);
                }
                if (night) {
                    p.setColor(0xffff2020);
                    for (int i = 0; i < 5; i++) {
                        float x = r.nextFloat() * w, y = h * (0.6f + r.nextFloat() * 0.3f);
                        c.drawCircle(x, y, 2, p);
                        c.drawCircle(x + 6, y, 2, p);
                    }
                }
                break;
            }
            case 2: { // Neon grid.
                p.setShader(new LinearGradient(0, 0, 0, h, 0xff12021f, 0xff050008, Shader.TileMode.CLAMP));
                c.drawRect(0, 0, w, h, p);
                p.setShader(null);
                p.setStyle(Paint.Style.STROKE);
                p.setStrokeWidth(1.5f);
                p.setColor(accent);
                float horizon = h * 0.55f;
                for (int i = 0; i < 12; i++) {
                    float y = horizon + (h - horizon) * (i * i) / 121f;
                    p.setAlpha(60 + i * 12);
                    c.drawLine(0, y, w, y, p);
                }
                for (int i = -8; i <= 8; i++) {
                    p.setAlpha(140);
                    c.drawLine(w / 2f + i * w * 0.04f, horizon, w / 2f + i * w * 0.35f, h, p);
                }
                p.setStyle(Paint.Style.FILL);
                p.setShader(new LinearGradient(0, horizon - w * 0.3f, 0, horizon, 0xffffcc33, accent, Shader.TileMode.CLAMP));
                c.drawCircle(w / 2f, horizon, w * 0.22f, p);
                p.setShader(null);
                break;
            }
            case 3: { // Space.
                c.drawColor(0xff03040c);
                p.setColor(0xffffffff);
                for (int i = 0; i < 160; i++) {
                    p.setAlpha(60 + r.nextInt(190));
                    c.drawCircle(r.nextFloat() * w, r.nextFloat() * h, r.nextFloat() * 1.4f + 0.3f, p);
                }
                p.setAlpha(255);
                p.setShader(new RadialGradient(w * 0.3f, h * 0.7f, w * 0.25f, accent, 0xff10061a, Shader.TileMode.CLAMP));
                c.drawCircle(w * 0.3f, h * 0.7f, w * 0.22f, p);
                p.setShader(null);
                p.setStyle(Paint.Style.STROKE);
                p.setStrokeWidth(3);
                p.setColor(0x88b9d0ff);
                c.drawOval(w * 0.3f - w * 0.38f, h * 0.7f - w * 0.06f, w * 0.3f + w * 0.38f, h * 0.7f + w * 0.06f, p);
                p.setStyle(Paint.Style.FILL);
                p.setColor(0xff6d5aa8);
                c.drawCircle(w * 0.8f, h * 0.22f, w * 0.06f, p);
                break;
            }
            case 4: { // Night city.
                p.setShader(new LinearGradient(0, 0, 0, h, 0xff0b0618, 0xff1d0a14, Shader.TileMode.CLAMP));
                c.drawRect(0, 0, w, h, p);
                p.setShader(null);
                for (int layer = 0; layer < 2; layer++) {
                    float x = 0;
                    while (x < w) {
                        float bw = w * (0.08f + r.nextFloat() * 0.1f);
                        float bh = h * (0.25f + r.nextFloat() * 0.35f) * (layer == 0 ? 1.1f : 0.8f);
                        p.setColor(layer == 0 ? 0xff120a1c : 0xff07040c);
                        c.drawRect(x, h - bh, x + bw, h, p);
                        p.setColor(Color.argb(170, 255, 200 - layer * 60, 120));
                        for (float wy = h - bh + 8; wy < h - 6; wy += 9) {
                            for (float wx = x + 4; wx < x + bw - 4; wx += 7) {
                                if (r.nextFloat() < 0.35f) {
                                    c.drawRect(wx, wy, wx + 3, wy + 4, p);
                                }
                            }
                        }
                        x += bw + 2;
                    }
                }
                break;
            }
            case 5: { // Underwater.
                p.setShader(new LinearGradient(0, 0, 0, h, 0xff0b4d6b, 0xff020d1a, Shader.TileMode.CLAMP));
                c.drawRect(0, 0, w, h, p);
                p.setShader(null);
                p.setColor(0x22ffffff);
                for (int i = 0; i < 6; i++) {
                    path.reset();
                    float x = r.nextFloat() * w;
                    path.moveTo(x, 0);
                    path.lineTo(x + w * 0.08f, 0);
                    path.lineTo(x + w * 0.25f, h);
                    path.lineTo(x + w * 0.15f, h);
                    path.close();
                    c.drawPath(path, p);
                }
                p.setColor(0xff0a3a2a);
                for (int i = 0; i < 14; i++) {
                    float x = r.nextFloat() * w;
                    float sh = h * (0.1f + r.nextFloat() * 0.25f);
                    c.drawRoundRect(x, h - sh, x + 5, h, 3, 3, p);
                }
                break;
            }
        }
    }

    private int labelKey(Label label, int color, int style) {
        return (int) (label.size * label.depth * 10) * 31 + color * 7 + style;
    }

    /** Labels are pre-rendered: a glow per frame is too expensive. */
    private Bitmap labelBitmap(Label label, int color, int style) {
        int key = labelKey(label, color, style);
        if (label.bitmap != null && label.bitmapKey == key) {
            return label.bitmap;
        }
        if (label.bitmap != null) {
            label.bitmap.recycle();
        }
        TextPaint text = new TextPaint(Paint.ANTI_ALIAS_FLAG);
        text.setTypeface(style == MwtkConfig.STYLE_PIXEL ? Typeface.MONOSPACE : Typeface.create(Typeface.DEFAULT, Typeface.BOLD));
        text.setAntiAlias(style != MwtkConfig.STYLE_PIXEL);
        float size = AndroidUtilities.dp(label.size * label.depth);
        text.setTextSize(size);
        int pad = (int) (size * 0.6f);
        int width = (int) text.measureText(label.text) + pad * 2;
        int height = (int) (size * 1.4f) + pad * 2;
        Bitmap bitmap = Bitmap.createBitmap(Math.max(1, width), Math.max(1, height), Bitmap.Config.ARGB_8888);
        Canvas c = new Canvas(bitmap);
        float baseY = pad + size;
        if (style == MwtkConfig.STYLE_COMIC) {
            text.setStyle(Paint.Style.STROKE);
            text.setStrokeWidth(AndroidUtilities.dp(2.5f));
            text.setColor(0xff000000);
            c.drawText(label.text, pad, baseY, text);
            text.setStyle(Paint.Style.FILL);
        }
        if (style == MwtkConfig.STYLE_NEON) {
            Paint glowPaint = new TextPaint(text);
            glowPaint.setColor(color);
            glowPaint.setMaskFilter(new BlurMaskFilter(AndroidUtilities.dp(6), BlurMaskFilter.Blur.NORMAL));
            c.drawText(label.text, pad, baseY, glowPaint);
        }
        text.setColor(color);
        c.drawText(label.text, pad, baseY, text);
        label.bitmap = bitmap;
        label.bitmapKey = key;
        return bitmap;
    }

    private void drawLabels(Canvas canvas, Rect bounds, float w, float h, float tx, float ty, float dt, int color, int style) {
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
            float beat = 1f + pulse * 0.5f;
            float alphaK = label.alpha * (0.75f + 0.25f * (float) Math.sin(time * 0.8f + label.phase)) * beat;
            if (style == MwtkConfig.STYLE_GLASS) {
                alphaK *= 0.7f;
            }
            Bitmap bitmap = labelBitmap(label, color, style);
            float x = bounds.left + label.x * w + tx * label.depth;
            float y = bounds.top + label.y * h + ty * label.depth;
            canvas.save();
            canvas.rotate(label.rotation + 3f * (float) Math.sin(time * 0.3f + label.phase), x, y);
            bitmapPaint.setAlpha((int) Math.min(255, 255 * alphaK));
            canvas.drawBitmap(bitmap, x - bitmap.getWidth() / 2f, y - bitmap.getHeight() / 2f, bitmapPaint);
            canvas.restore();
        }
        bitmapPaint.setAlpha(255);
    }

    private void drawFigures(Canvas canvas, Rect bounds, float w, float h, float tx, float ty, float dt, int color, int style) {
        line.setColor(Color.argb(style == MwtkConfig.STYLE_GLASS ? 50 : 80, Color.red(color), Color.green(color), Color.blue(color)));
        line.setStrokeWidth(AndroidUtilities.dp(1.3f));
        line.setAntiAlias(style != MwtkConfig.STYLE_PIXEL);
        for (Figure figure : figures) {
            figure.ax += figure.speed * dt;
            figure.ay += figure.speed * 0.7f * dt;
            float size = AndroidUtilities.dp(figure.size) * figure.depth;
            float cx = bounds.left + figure.x * w + tx * figure.depth * 1.4f;
            float cy = bounds.top + figure.y * h + ty * figure.depth * 1.4f;
            float cxA = (float) Math.cos(figure.ax), sxA = (float) Math.sin(figure.ax);
            float cyA = (float) Math.cos(figure.ay), syA = (float) Math.sin(figure.ay);
            for (int i = 0; i < 8; i++) {
                float x = (i & 1) != 0 ? 1 : -1;
                float y = (i & 2) != 0 ? 1 : -1;
                float z = (i & 4) != 0 ? 1 : -1;
                float y1 = y * cxA - z * sxA;
                float z1 = y * sxA + z * cxA;
                float x2 = x * cyA + z1 * syA;
                float z2 = -x * syA + z1 * cyA;
                float k = 3f / (4f + z2);
                vertices[i][0] = cx + x2 * size * k;
                vertices[i][1] = cy + y1 * size * k;
            }
            for (int i = 0; i < 8; i++) {
                for (int bit = 1; bit <= 4; bit <<= 1) {
                    int j = i | bit;
                    if (j != i) {
                        canvas.drawLine(vertices[i][0], vertices[i][1], vertices[j][0], vertices[j][1], line);
                    }
                }
            }
        }
    }

    private void drawSeason(Canvas canvas, Rect bounds, float w, float h, float dt) {
        int season = season();
        if (season == 0) {
            return;
        }
        for (Flake flake : flakes) {
            flake.y += flake.vy * dt;
            flake.x += (flake.vx + (float) Math.sin(time * 1.3f + flake.sway) * 0.01f) * dt;
            flake.angle += flake.spin * dt;
            if (flake.y > 1.05f || flake.x < -0.1f || flake.x > 1.1f) {
                spawn(flake, false);
            }
            float x = bounds.left + flake.x * w;
            float y = bounds.top + flake.y * h;
            float size = AndroidUtilities.dp(flake.size) * (season == 1 ? 0.7f : 1.4f);
            if (season == 1) {
                fill.setColor(0xccffffff);
                canvas.drawCircle(x, y, size, fill);
            } else {
                fill.setColor(season == 2
                    ? (flake.sway > 3.14f ? 0xffd65c1c : 0xffe8a028)
                    : (flake.sway > 3.14f ? 0xffffc4d6 : 0xffffe2ec));
                canvas.save();
                canvas.rotate(flake.angle, x, y);
                path.reset();
                path.moveTo(x - size, y);
                path.quadTo(x, y - size * 0.7f, x + size, y);
                path.quadTo(x, y + size * 0.7f, x - size, y);
                canvas.drawPath(path, fill);
                canvas.restore();
            }
        }
    }

    // ------------------------------------------------------------ frames

    private void scheduleNext(long now) {
        boolean moving = MwtkConfig.labels || MwtkConfig.figures
            || (MwtkConfig.flag(MwtkConfig.SEASONS, true) && season() != 0);
        if (!moving || !isVisible()) {
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
            if (android.os.Build.VERSION.SDK_INT >= 29 && power != null
                && power.getCurrentThermalStatus() >= PowerManager.THERMAL_STATUS_SEVERE) {
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
