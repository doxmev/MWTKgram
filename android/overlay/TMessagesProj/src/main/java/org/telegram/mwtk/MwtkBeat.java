package org.telegram.mwtk;

import android.Manifest;
import android.content.pm.PackageManager;
import android.media.audiofx.Visualizer;
import android.os.SystemClock;

import org.telegram.messenger.ApplicationLoader;
import org.telegram.messenger.MediaController;
import org.telegram.messenger.MessageObject;

/**
 * Music beat for the background. Uses the system visualizer of the output
 * mix, which needs the microphone permission (most users already granted
 * it for voice messages). Without it the background simply stays calm.
 */
public final class MwtkBeat {

    private static Visualizer visualizer;
    private static float average;
    private static float beat;
    private static long lastPoll;
    private static boolean failed;
    private static final byte[] wave = new byte[512];
    private static final byte[] fft = new byte[512];
    private static final int BANDS = 48;
    private static final float[] bands = new float[BANDS];
    private static float loudness;

    /** Smoothed spectrum bars (0..1) for the lyrics visualizer, low to high. */
    public static float[] bands() {
        level();
        return bands;
    }

    /** Current loudness 0..1, for when the spectrum is not available. */
    public static float loudness() {
        level();
        return loudness;
    }

    private MwtkBeat() {
    }

    private static boolean allowed() {
        return ApplicationLoader.applicationContext.checkSelfPermission(Manifest.permission.RECORD_AUDIO)
            == PackageManager.PERMISSION_GRANTED;
    }

    public static float level() {
        long now = SystemClock.elapsedRealtime();
        if (now - lastPoll < 30) {
            return beat;
        }
        float dt = Math.min((now - lastPoll) / 1000f, 0.2f);
        lastPoll = now;
        MessageObject playing = MediaController.getInstance().getPlayingMessageObject();
        boolean music = playing != null && playing.isMusic() && !MediaController.getInstance().isMessagePaused();
        if (!music || failed || !allowed()) {
            release();
            beat = 0;
            loudness = 0;
            java.util.Arrays.fill(bands, 0f);
            return 0;
        }
        try {
            if (visualizer == null) {
                visualizer = new Visualizer(0);
                visualizer.setCaptureSize(512);
                visualizer.setEnabled(true);
            }
            if (visualizer.getWaveForm(wave) != Visualizer.SUCCESS) {
                return beat;
            }
            float sum = 0;
            for (byte b : wave) {
                float v = ((b & 0xff) - 128) / 128f;
                sum += v * v;
            }
            float level = (float) Math.sqrt(sum / wave.length) * 2.2f;
            loudness = Math.min(1f, level);
            if (visualizer.getFft(fft) == Visualizer.SUCCESS) {
                // Log-spaced bands over the first half of the spectrum.
                int bins = fft.length / 2;
                for (int i = 0; i < BANDS; i++) {
                    int from = (int) Math.max(1, Math.pow(bins, (double) i / BANDS));
                    int to = (int) Math.max(from + 1, Math.pow(bins, (double) (i + 1) / BANDS));
                    float peak = 0;
                    for (int k = from; k < to && k < bins; k++) {
                        float re = fft[k * 2];
                        float im = fft[k * 2 + 1];
                        peak = Math.max(peak, (float) Math.hypot(re, im));
                    }
                    float value = Math.min(1f, (float) (Math.log10(1 + peak) / 2.1));
                    bands[i] = value > bands[i] ? value : bands[i] + (value - bands[i]) * Math.min(1f, dt * 6f);
                }
            }
            average += (level - average) * Math.min(dt * 2.5f, 1f);
            float hit = Math.max(0f, Math.min(1f, (level - average * 1.05f) * 5f));
            beat = Math.max(hit, beat * (float) Math.exp(-dt * 7f));
        } catch (Throwable e) {
            failed = true;
            release();
        }
        return beat;
    }

    private static void release() {
        if (visualizer != null) {
            try {
                visualizer.setEnabled(false);
                visualizer.release();
            } catch (Throwable ignore) {
            }
            visualizer = null;
        }
    }
}
