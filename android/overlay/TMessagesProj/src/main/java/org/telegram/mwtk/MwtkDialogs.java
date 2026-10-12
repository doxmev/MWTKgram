package org.telegram.mwtk;

import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.LinearGradient;
import android.graphics.Paint;
import android.graphics.RadialGradient;
import android.graphics.RectF;
import android.graphics.Shader;
import android.os.SystemClock;
import android.view.View;

import org.telegram.messenger.AndroidUtilities;

/** Chat list look: background, translucent cards and the "online" marks. */
public final class MwtkDialogs {

    private static final Paint cardPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private static final Paint shadowPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private static final Paint borderPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private static final Paint stripePaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private static final Paint haloPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private static final Paint pulsePaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private static final RectF rect = new RectF();

    private static final int ONLINE = 0xff39ff88;

    private static int cardHeight;
    private static int cardPreset = -1;

    private MwtkDialogs() {
    }

    public static void attachBackground(View view, boolean mainList) {
        if (view == null || !mainList) {
            return;
        }
        MwtkConfig.load();
        view.setBackground(new MwtkBackgroundDrawable());
    }

    private static boolean active() {
        MwtkConfig.load();
        return MwtkConfig.effects;
    }

    /** Draws the translucent card behind a chat row. */
    public static void drawCard(Canvas canvas, int width, int height, boolean online, boolean regular) {
        if (!regular || !active() || !MwtkConfig.cards || width <= 0 || height <= 0) {
            return;
        }
        final int[] palette = MwtkConfig.palette();
        final int listAlpha = MwtkConfig.num(MwtkConfig.LIST_ALPHA, 100);
        final int cardKey = MwtkConfig.preset * 7919 + MwtkConfig.accent() + listAlpha;
        if (cardHeight != height || cardPreset != cardKey) {
            cardHeight = height;
            cardPreset = cardKey;
            final int accent = MwtkConfig.accent();
            cardPaint.setShader(new LinearGradient(0, 0, 0, height,
                Color.argb(92 * listAlpha / 100, Color.red(accent), Color.green(accent), Color.blue(accent)),
                Color.argb(70 * listAlpha / 100, Color.red(accent) / 2, Color.green(accent) / 2, Color.blue(accent) / 2),
                Shader.TileMode.CLAMP));
        }
        final float inset = AndroidUtilities.dp(8);
        final float vertical = AndroidUtilities.dp(3);
        final float radius = AndroidUtilities.dp(MwtkConfig.radius);
        rect.set(inset, vertical, width - inset, height - vertical);

        shadowPaint.setColor(0x66280004);
        rect.offset(0, AndroidUtilities.dp(2.5f));
        canvas.drawRoundRect(rect, radius, radius, shadowPaint);
        rect.offset(0, -AndroidUtilities.dp(2.5f));

        canvas.drawRoundRect(rect, radius, radius, cardPaint);

        final boolean glowing = online && MwtkConfig.cardGlow;
        borderPaint.setStyle(Paint.Style.STROKE);
        borderPaint.setStrokeWidth(AndroidUtilities.dp(glowing ? 1.4f : 1f));
        borderPaint.setColor(glowing ? 0x7339ff88 : 0x29ff5a5a);
        canvas.drawRoundRect(rect, radius, radius, borderPaint);

        if (MwtkConfig.stripe) {
            stripePaint.setColor(online ? ONLINE : 0xe6960010);
            if (online) {
                stripePaint.setShadowLayer(AndroidUtilities.dp(6), 0, 0, ONLINE);
            } else {
                stripePaint.clearShadowLayer();
            }
            final float top = rect.top + AndroidUtilities.dp(10);
            final float bottom = rect.bottom - AndroidUtilities.dp(10);
            canvas.drawRoundRect(rect.left, top, rect.left + AndroidUtilities.dp(4), bottom,
                AndroidUtilities.dp(2), AndroidUtilities.dp(2), stripePaint);
        }
    }

    private static RadialGradient haloOnline;
    private static RadialGradient haloOffline;
    private static float haloRadius;
    private static int haloAccent;

    /** Soft halo behind the avatar, not a ring (rings look like stories). */
    public static void drawHalo(Canvas canvas, RectF avatar, boolean online, boolean regular) {
        if (!regular || !active() || !MwtkConfig.glow || avatar == null || avatar.width() <= 0) {
            return;
        }
        final float r = avatar.width() / 2f;
        final float outer = r * 1.38f;
        final int accent = MwtkConfig.accent();
        // The gradients are made once and reused, they are expensive.
        if (haloRadius != outer || haloAccent != accent || haloOnline == null) {
            haloRadius = outer;
            haloAccent = accent;
            haloOnline = halo(ONLINE, 120, r, outer);
            haloOffline = halo(accent, 70, r, outer);
        }
        haloPaint.setShader(online ? haloOnline : haloOffline);
        canvas.save();
        canvas.translate(avatar.centerX(), avatar.centerY());
        canvas.drawCircle(0, 0, outer, haloPaint);
        canvas.restore();
    }

    private static RadialGradient halo(int color, int alpha, float r, float outer) {
        return new RadialGradient(0, 0, outer,
            new int[] {
                Color.argb(0, Color.red(color), Color.green(color), Color.blue(color)),
                Color.argb(0, Color.red(color), Color.green(color), Color.blue(color)),
                Color.argb(alpha, Color.red(color), Color.green(color), Color.blue(color)),
                Color.argb(0, Color.red(color), Color.green(color), Color.blue(color)),
            },
            new float[] { 0f, Math.max(0f, r / outer - 0.04f), r / outer, 1f },
            Shader.TileMode.CLAMP);
    }

    /** Avatar corner radius: a rounded tile for online users when enabled. */
    public static int avatarRadius(int round, boolean online) {
        if (online && active() && MwtkConfig.tile) {
            return AndroidUtilities.dp(14);
        }
        return round;
    }

    public static boolean showOnlineDot() {
        if (!active()) {
            return true;
        }
        return MwtkConfig.dot || MwtkConfig.pulse;
    }

    /** A breathing ring around the online dot, returns true while animating. */
    public static boolean drawPulse(Canvas canvas, float x, float y, float progress) {
        if (!active() || !MwtkConfig.pulse || progress <= 0f) {
            return false;
        }
        final float t = (SystemClock.elapsedRealtime() % 1800) / 1800f;
        pulsePaint.setStyle(Paint.Style.STROKE);
        pulsePaint.setStrokeWidth(AndroidUtilities.dp(2));
        pulsePaint.setColor(Color.argb((int) (180 * (1f - t) * progress), 57, 255, 136));
        canvas.drawCircle(x, y, AndroidUtilities.dp(5) + AndroidUtilities.dp(8) * t, pulsePaint);
        return true;
    }
}
