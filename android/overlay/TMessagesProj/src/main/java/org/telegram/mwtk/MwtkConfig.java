package org.telegram.mwtk;

import android.app.Activity;
import android.content.Context;
import android.content.SharedPreferences;

import org.telegram.messenger.ApplicationLoader;

import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.CopyOnWriteArrayList;

/** MWTKgram settings, kept in their own preferences file. */
public final class MwtkConfig {

    public static final String EFFECTS = "effects";
    public static final String LABELS = "labels";
    public static final String FIGURES = "figures";
    public static final String CARDS = "cards";
    public static final String GLOW = "glow";
    public static final String STRIPE = "stripe";
    public static final String CARD_GLOW = "card_glow";
    public static final String DOT = "dot";
    public static final String PULSE = "pulse";
    public static final String TILE = "tile";
    public static final String SHAKE = "shake";
    public static final String VIBRATE = "vibrate";
    public static final String VIBRATION = "vibration";
    public static final String TILT = "tilt";
    public static final String BATTERY = "battery";
    public static final String RADIUS = "radius";
    public static final String STYLE = "style";
    public static final String PRESET = "preset";

    public static final int STYLE_NEON = 0;
    public static final int STYLE_GLASS = 1;
    public static final int STYLE_PIXEL = 2;
    public static final int STYLE_COMIC = 3;

    public static final int PRESET_BLOOD = 0;
    public static final int PRESET_SYNTHWAVE = 1;
    public static final int PRESET_SPACE = 2;
    public static final int PRESET_ECONOMY = 3;

    private static final CopyOnWriteArrayList<Runnable> listeners = new CopyOnWriteArrayList<>();

    // Cached values, read on every frame.
    public static volatile boolean effects = true;
    public static volatile boolean labels = true;
    public static volatile boolean figures = false;
    public static volatile boolean cards = true;
    public static volatile boolean glow = true;
    public static volatile boolean stripe = false;
    public static volatile boolean cardGlow = false;
    public static volatile boolean dot = true;
    public static volatile boolean pulse = false;
    public static volatile boolean tile = false;
    public static volatile boolean shake = true;
    public static volatile boolean vibrate = true;
    public static volatile int vibration = 40;
    public static volatile boolean tilt = true;
    public static volatile boolean battery = true;
    public static volatile int radius = 18;
    public static volatile int style = STYLE_NEON;
    public static volatile int preset = PRESET_BLOOD;

    private static boolean loaded;

    private MwtkConfig() {
    }

    private static SharedPreferences prefs() {
        return ApplicationLoader.applicationContext.getSharedPreferences("mwtk", Activity.MODE_PRIVATE);
    }

    public static synchronized void load() {
        if (loaded) {
            return;
        }
        loaded = true;
        SharedPreferences p = prefs();
        effects = p.getBoolean(EFFECTS, true);
        labels = p.getBoolean(LABELS, true);
        figures = p.getBoolean(FIGURES, false);
        cards = p.getBoolean(CARDS, true);
        glow = p.getBoolean(GLOW, true);
        stripe = p.getBoolean(STRIPE, false);
        cardGlow = p.getBoolean(CARD_GLOW, false);
        dot = p.getBoolean(DOT, true);
        pulse = p.getBoolean(PULSE, false);
        tile = p.getBoolean(TILE, false);
        shake = p.getBoolean(SHAKE, true);
        vibrate = p.getBoolean(VIBRATE, true);
        vibration = p.getInt(VIBRATION, 40);
        tilt = p.getBoolean(TILT, true);
        battery = p.getBoolean(BATTERY, true);
        radius = p.getInt(RADIUS, 18);
        style = p.getInt(STYLE, STYLE_NEON);
        preset = p.getInt(PRESET, PRESET_BLOOD);
    }

    public static boolean getBool(String key) {
        load();
        switch (key) {
            case EFFECTS: return effects;
            case LABELS: return labels;
            case FIGURES: return figures;
            case CARDS: return cards;
            case GLOW: return glow;
            case STRIPE: return stripe;
            case CARD_GLOW: return cardGlow;
            case DOT: return dot;
            case PULSE: return pulse;
            case TILE: return tile;
            case SHAKE: return shake;
            case VIBRATE: return vibrate;
            case TILT: return tilt;
            case BATTERY: return battery;
        }
        return false;
    }

    public static void setBool(String key, boolean value) {
        load();
        prefs().edit().putBoolean(key, value).apply();
        loaded = false;
        load();
        changed();
    }

    public static int getInt(String key) {
        load();
        switch (key) {
            case VIBRATION: return vibration;
            case RADIUS: return radius;
            case STYLE: return style;
            case PRESET: return preset;
        }
        return 0;
    }

    public static void setInt(String key, int value) {
        load();
        prefs().edit().putInt(key, value).apply();
        loaded = false;
        load();
        changed();
    }

    // Generic options for the newer features, cached in memory.
    private static final ConcurrentHashMap<String, Object> cache = new ConcurrentHashMap<>();

    public static final String APP_ALPHA = "app_alpha";
    public static final String LIST_ALPHA = "list_alpha";
    public static final String APP_ICON = "app_icon";
    public static final String NOTIFY_STYLE = "notify_style";
    public static final String SCENE = "scene";
    public static final String SEASONS = "seasons";
    public static final String MUSIC_PULSE = "music_pulse";
    public static final String LYRICS = "lyrics";
    public static final String LYRICS_ONLINE = "lyrics_online";
    public static final String LYRICS_LAYOUT = "lyrics_layout";
    public static final String LYRICS_VANISH = "lyrics_vanish";
    public static final String LYRICS_STYLE = "lyrics_style";
    public static final String LYRICS_MOTION = "lyrics_motion";
    public static final String LYRICS_SIZE = "lyrics_size";
    public static final String HIDE_CHATS = "hide_chats";
    public static final String FLYBY = "flyby";
    public static final String HOLO = "holo";
    public static final String SOUNDS = "sounds";
    public static final String APPEAR = "appear";
    public static final String SEND_SPARKS = "send_sparks";
    public static final String NIGHT = "night";
    public static final String KEEP_DELETED = "keep_deleted";
    public static final String PHRASES = "phrases";
    public static final String CUSTOM_COLOR = "custom_color";
    public static final String RESPECT_ANIMATIONS = "respect_animations";
    public static final String CHAT_FX = "chat_fx";
    public static final String TABLET = "tablet";
    public static final String LYRICS_APPEAR = "lyrics_appear";
    public static final String LYRICS_DRIFT = "lyrics_drift";
    public static final String LYRICS_BEAT = "lyrics_beat";
    public static final String GIFTS_ARCHIVED = "gifts_archived";
    public static final String TOGETHER_ALLOW = "together_allow";
    public static final String FLOAT_COUNT = "float_count";
    public static final String FLOAT_SOURCE = "float_source";
    public static final String FLOAT_PICKED = "float_picked";

    public static String text(String key, String def) {
        Object value = cache.get(key);
        if (value instanceof String) {
            return (String) value;
        }
        String result = prefs().getString(key, def);
        cache.put(key, result);
        return result;
    }

    public static void setText(String key, String value) {
        prefs().edit().putString(key, value).apply();
        cache.put(key, value);
        changed();
    }

    public static boolean flag(String key, boolean def) {
        Object value = cache.get(key);
        if (value instanceof Boolean) {
            return (Boolean) value;
        }
        boolean result = prefs().getBoolean(key, def);
        cache.put(key, result);
        return result;
    }

    public static int num(String key, int def) {
        Object value = cache.get(key);
        if (value instanceof Integer) {
            return (Integer) value;
        }
        int result = prefs().getInt(key, def);
        cache.put(key, result);
        return result;
    }

    public static void setFlag(String key, boolean value) {
        prefs().edit().putBoolean(key, value).apply();
        cache.put(key, value);
        changed();
    }

    public static void setNum(String key, int value) {
        prefs().edit().putInt(key, value).apply();
        cache.put(key, value);
        changed();
    }

    /** Effects are on and the system did not ask to remove animations. */
    public static boolean animate() {
        load();
        if (!effects) {
            return false;
        }
        if (flag(RESPECT_ANIMATIONS, true)) {
            try {
                float scale = android.provider.Settings.Global.getFloat(
                    ApplicationLoader.applicationContext.getContentResolver(),
                    android.provider.Settings.Global.ANIMATOR_DURATION_SCALE,
                    1f);
                if (scale == 0f) {
                    return false;
                }
            } catch (Throwable ignore) {
            }
        }
        return true;
    }

    /** Accent color: the preset's or the custom one. */
    public static int accent() {
        int custom = num(CUSTOM_COLOR, 0);
        return custom != 0 ? custom : palette()[2];
    }

    public static void toggleEffects() {
        setBool(EFFECTS, !effects);
    }

    public static void applyPreset(int value) {
        SharedPreferences.Editor e = prefs().edit();
        e.putInt(PRESET, value);
        e.putBoolean(EFFECTS, true);
        switch (value) {
            case PRESET_SYNTHWAVE:
                e.putBoolean(LABELS, true).putBoolean(FIGURES, true).putBoolean(CARDS, true).putInt(STYLE, STYLE_NEON);
                break;
            case PRESET_SPACE:
                e.putBoolean(LABELS, true).putBoolean(FIGURES, true).putBoolean(CARDS, true).putInt(STYLE, STYLE_GLASS);
                break;
            case PRESET_ECONOMY:
                e.putBoolean(LABELS, false).putBoolean(FIGURES, false).putBoolean(PULSE, false).putBoolean(TILT, false);
                break;
            default:
                e.putBoolean(LABELS, true).putBoolean(FIGURES, false).putBoolean(CARDS, true).putInt(STYLE, STYLE_NEON);
                break;
        }
        e.apply();
        loaded = false;
        load();
        changed();
    }

    /** Colors of the current preset: background top, bottom, accent, label. */
    public static int[] palette() {
        load();
        switch (preset) {
            case PRESET_SYNTHWAVE:
                return new int[] { 0xff1a0430, 0xff07020f, 0xffff2fa6, 0xffff8ad8 };
            case PRESET_SPACE:
                return new int[] { 0xff04102a, 0xff02030a, 0xff1f8bff, 0xff7fc4ff };
            case PRESET_ECONOMY:
                return new int[] { 0xff121212, 0xff060606, 0xff8a8a8a, 0xffbdbdbd };
            default:
                return new int[] { 0xff3a0207, 0xff070103, 0xffe0141e, 0xffff4a50 };
        }
    }

    public static void addListener(Runnable listener) {
        listeners.addIfAbsent(listener);
    }

    public static void removeListener(Runnable listener) {
        listeners.remove(listener);
    }

    /** Something outside the settings changed (for example a song cover): views refresh. */
    public static void changedExternally() {
        changed();
    }

    private static void changed() {
        for (Runnable listener : listeners) {
            try {
                listener.run();
            } catch (Throwable ignore) {
            }
        }
    }

    /** First start: dark theme with the red accent ("Кровь"). */
    public static void applyFirstRunDefaults(Context context) {
        if (context == null) {
            return;
        }
        SharedPreferences mwtk = context.getSharedPreferences("mwtk", Activity.MODE_PRIVATE);
        if (mwtk.getBoolean("first_run_done", false)) {
            return;
        }
        SharedPreferences main = context.getSharedPreferences("mainconfig", Activity.MODE_PRIVATE);
        if (!main.contains("theme")) {
            main.edit().putString("theme", "Night").commit();
            context.getSharedPreferences("themeconfig", Activity.MODE_PRIVATE)
                .edit()
                .putInt("accent_current_night.attheme", 6)
                .putString("lastDayTheme", "Night")
                .commit();
        }
        mwtk.edit().putBoolean("first_run_done", true).commit();
    }
}
