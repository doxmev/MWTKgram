package org.telegram.mwtk;

import android.content.Context;
import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorEventListener;
import android.hardware.SensorManager;
import android.os.Build;
import android.os.SystemClock;
import android.os.VibrationEffect;
import android.os.Vibrator;
import android.widget.Toast;

import org.telegram.messenger.AndroidUtilities;
import org.telegram.messenger.ApplicationLoader;

/** Accelerometer: phone tilt for the background, shake to toggle effects. */
public final class MwtkMotion implements SensorEventListener {

    private static final float SHAKE_THRESHOLD = 14f;
    private static final long SHAKE_WINDOW = 700;
    private static final long SHAKE_COOLDOWN = 1500;

    private static MwtkMotion instance;

    public static volatile float tiltX;
    public static volatile float tiltY;

    private final float[] gravity = new float[] { 0f, 9.81f, 0f };
    private boolean registered;
    private long firstShake;
    private int shakeCount;
    private long lastToggle;

    private MwtkMotion() {
    }

    public static MwtkMotion get() {
        if (instance == null) {
            instance = new MwtkMotion();
        }
        return instance;
    }

    public void resume() {
        MwtkConfig.load();
        if (registered || (!MwtkConfig.shake && !MwtkConfig.tilt)) {
            return;
        }
        try {
            SensorManager manager = (SensorManager) ApplicationLoader.applicationContext.getSystemService(Context.SENSOR_SERVICE);
            Sensor sensor = manager != null ? manager.getDefaultSensor(Sensor.TYPE_ACCELEROMETER) : null;
            if (sensor != null) {
                manager.registerListener(this, sensor, SensorManager.SENSOR_DELAY_UI);
                registered = true;
            }
        } catch (Throwable ignore) {
        }
    }

    public void pause() {
        if (!registered) {
            return;
        }
        try {
            SensorManager manager = (SensorManager) ApplicationLoader.applicationContext.getSystemService(Context.SENSOR_SERVICE);
            if (manager != null) {
                manager.unregisterListener(this);
            }
        } catch (Throwable ignore) {
        }
        registered = false;
    }

    @Override
    public void onSensorChanged(SensorEvent event) {
        final float x = event.values[0];
        final float y = event.values[1];
        final float z = event.values[2];
        final float k = 0.85f;
        gravity[0] = k * gravity[0] + (1 - k) * x;
        gravity[1] = k * gravity[1] + (1 - k) * y;
        gravity[2] = k * gravity[2] + (1 - k) * z;

        if (MwtkConfig.tilt) {
            tiltX = clamp(-gravity[0] / 6f);
            tiltY = clamp((gravity[1] - 7f) / 6f);
        } else {
            tiltX = 0f;
            tiltY = 0f;
        }

        if (!MwtkConfig.shake) {
            return;
        }
        final float lx = x - gravity[0];
        final float ly = y - gravity[1];
        final float lz = z - gravity[2];
        final float force = (float) Math.sqrt(lx * lx + ly * ly + lz * lz);
        final long now = SystemClock.elapsedRealtime();
        if (force > SHAKE_THRESHOLD) {
            if (now - firstShake > SHAKE_WINDOW) {
                firstShake = now;
                shakeCount = 1;
            } else if (++shakeCount >= 3 && now - lastToggle > SHAKE_COOLDOWN) {
                lastToggle = now;
                shakeCount = 0;
                AndroidUtilities.runOnUIThread(MwtkMotion::onShake);
            }
        }
    }

    private static void onShake() {
        MwtkConfig.toggleEffects();
        vibrate(MwtkConfig.effects ? 60 : 30);
        try {
            Toast.makeText(
                ApplicationLoader.applicationContext,
                MwtkConfig.effects ? "MWTK: эффекты включены" : "MWTK: эффекты выключены",
                Toast.LENGTH_SHORT).show();
        } catch (Throwable ignore) {
        }
    }

    @Override
    public void onAccuracyChanged(Sensor sensor, int accuracy) {
    }

    private static float clamp(float value) {
        return Math.max(-1f, Math.min(1f, value));
    }

    /** Short vibration with the strength chosen in the settings. */
    public static void vibrate(long millis) {
        MwtkConfig.load();
        if (!MwtkConfig.vibrate || MwtkConfig.vibration <= 0) {
            return;
        }
        try {
            Vibrator vibrator = (Vibrator) ApplicationLoader.applicationContext.getSystemService(Context.VIBRATOR_SERVICE);
            if (vibrator == null || !vibrator.hasVibrator()) {
                return;
            }
            if (Build.VERSION.SDK_INT >= 26) {
                int amplitude = Math.max(1, Math.min(255, Math.round(MwtkConfig.vibration * 2.55f)));
                if (!vibrator.hasAmplitudeControl()) {
                    amplitude = VibrationEffect.DEFAULT_AMPLITUDE;
                }
                vibrator.vibrate(VibrationEffect.createOneShot(millis, amplitude));
            } else {
                vibrator.vibrate(Math.max(5, millis * MwtkConfig.vibration / 100));
            }
        } catch (Throwable ignore) {
        }
    }
}
