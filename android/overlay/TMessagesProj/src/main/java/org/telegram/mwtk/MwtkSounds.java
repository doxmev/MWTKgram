package org.telegram.mwtk;

import android.media.AudioAttributes;
import android.media.AudioFormat;
import android.media.AudioTrack;

import org.telegram.messenger.Utilities;

/** Soft synthesized interface sounds: send and toggle. */
public final class MwtkSounds {

    public static final int SEND = 0;
    public static final int TOGGLE = 1;

    private static final int RATE = 22050;
    private static short[][] cache = new short[2][];

    private MwtkSounds() {
    }

    private static short[] make(int kind) {
        int length = (int) (RATE * (kind == SEND ? 0.16 : 0.12));
        short[] data = new short[length];
        for (int i = 0; i < length; i++) {
            double t = i / (double) RATE;
            double k = i / (double) length;
            double freq = kind == SEND ? 520 + 900 * k : (k < 0.5 ? 660 : 880);
            double envelope = Math.sin(Math.PI * k) * (1 - k * 0.4);
            double value = Math.sin(2 * Math.PI * freq * t) * 0.6 + Math.sin(4 * Math.PI * freq * t) * 0.15;
            data[i] = (short) (value * envelope * 9000);
        }
        return data;
    }

    public static void play(int kind) {
        MwtkConfig.load();
        if (!MwtkConfig.effects || !MwtkConfig.flag(MwtkConfig.SOUNDS, true)) {
            return;
        }
        Utilities.globalQueue.postRunnable(() -> {
            try {
                if (cache[kind] == null) {
                    cache[kind] = make(kind);
                }
                short[] data = cache[kind];
                AudioTrack track = new AudioTrack.Builder()
                    .setAudioAttributes(new AudioAttributes.Builder()
                        .setUsage(AudioAttributes.USAGE_ASSISTANCE_SONIFICATION)
                        .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION)
                        .build())
                    .setAudioFormat(new AudioFormat.Builder()
                        .setEncoding(AudioFormat.ENCODING_PCM_16BIT)
                        .setSampleRate(RATE)
                        .setChannelMask(AudioFormat.CHANNEL_OUT_MONO)
                        .build())
                    .setBufferSizeInBytes(data.length * 2)
                    .setTransferMode(AudioTrack.MODE_STATIC)
                    .build();
                track.write(data, 0, data.length);
                track.play();
                Utilities.globalQueue.postRunnable(track::release, 600);
            } catch (Throwable ignore) {
            }
        });
    }
}
