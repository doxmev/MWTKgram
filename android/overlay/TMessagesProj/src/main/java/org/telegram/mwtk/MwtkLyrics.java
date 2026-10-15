package org.telegram.mwtk;

import android.annotation.SuppressLint;
import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.BlurMaskFilter;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Typeface;
import android.text.TextPaint;
import android.view.MotionEvent;
import android.view.View;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.TextView;

import org.json.JSONArray;
import org.json.JSONObject;
import org.telegram.messenger.AndroidUtilities;
import org.telegram.messenger.FileLoader;
import org.telegram.messenger.ImageLocation;
import org.telegram.messenger.ImageReceiver;
import org.telegram.messenger.MediaController;
import org.telegram.messenger.MessageObject;
import org.telegram.messenger.Utilities;
import org.telegram.tgnet.TLRPC;
import org.telegram.ui.ActionBar.AlertDialog;
import org.telegram.ui.ActionBar.BaseFragment;
import org.telegram.ui.ActionBar.Theme;
import org.telegram.ui.Components.LayoutHelper;

import java.io.BufferedReader;
import java.io.File;
import java.io.InputStreamReader;
import java.net.HttpURLConnection;
import java.net.URL;
import java.net.URLEncoder;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Random;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * Flying synced lyrics. Timed text comes from a .lrc file in our cache
 * (made in the sync editor or downloaded before) or from LRCLIB.
 */
public final class MwtkLyrics {

    public static final class Line {
        public long at;
        public long end;
        public String text;
        /** Start time of each word (enhanced LRC "<m:ss>word"), or null. */
        public long[] words;
    }

    private static final long NOT_FOUND_RETRY = 7L * 24 * 3600 * 1000;

    private static String currentKey;
    private static ArrayList<Line> lines = new ArrayList<>();
    private static int generation;
    private static LyricsView view;

    private MwtkLyrics() {
    }

    // ------------------------------------------------------------ parsing

    private static final Pattern TAG = Pattern.compile("^\\[(\\d{1,3}):(\\d{1,2})(?:[.:](\\d{1,3}))?\\]");
    private static final Pattern OFFSET = Pattern.compile("\\[offset:\\s*([+-]?\\d+)\\s*\\]", Pattern.CASE_INSENSITIVE);
    private static final Pattern WORD_TAG = Pattern.compile("<(\\d{1,3}):(\\d{1,2})(?:[.:](\\d{1,3}))?>");

    private static long toMs(Matcher m) {
        long minutes = Long.parseLong(m.group(1));
        long seconds = Long.parseLong(m.group(2));
        String fraction = m.group(3);
        long ms = 0;
        if (fraction != null && !fraction.isEmpty()) {
            StringBuilder f = new StringBuilder(fraction);
            while (f.length() < 3) {
                f.append('0');
            }
            ms = Long.parseLong(f.substring(0, 3));
        }
        return (minutes * 60 + seconds) * 1000 + ms;
    }

    public static ArrayList<Line> parse(String text) {
        ArrayList<Line> result = new ArrayList<>();
        if (text == null) {
            return result;
        }
        long offset = 0;
        for (String raw : text.split("\n")) {
            String line = raw.trim();
            Matcher off = OFFSET.matcher(line);
            if (off.find()) {
                try {
                    offset = Long.parseLong(off.group(1));
                } catch (Throwable ignore) {
                }
                continue;
            }
            ArrayList<Long> times = new ArrayList<>();
            String rest = line;
            while (true) {
                Matcher m = TAG.matcher(rest);
                if (!m.find()) {
                    break;
                }
                times.add(toMs(m));
                rest = rest.substring(m.end());
            }
            if (times.isEmpty()) {
                continue;
            }
            // Word times: every word after a <m:ss> tag starts at that time.
            ArrayList<Long> wordTimes = new ArrayList<>();
            StringBuilder cleanBuilder = new StringBuilder();
            Matcher wm = WORD_TAG.matcher(rest);
            int last = 0;
            Long pending = null;
            boolean tagged = false;
            while (wm.find()) {
                tagged = true;
                String chunk = rest.substring(last, wm.start());
                if (pending != null) {
                    for (String w : chunk.trim().split("\\s+")) {
                        if (!w.isEmpty()) {
                            wordTimes.add(pending);
                        }
                    }
                }
                cleanBuilder.append(chunk).append(' ');
                pending = toMs(wm);
                last = wm.end();
            }
            String tail = rest.substring(last);
            if (pending != null) {
                for (String w : tail.trim().split("\\s+")) {
                    if (!w.isEmpty()) {
                        wordTimes.add(pending);
                    }
                }
            }
            cleanBuilder.append(tail);
            String clean = cleanBuilder.toString().trim().replaceAll("\\s+", " ");
            int wordCount = clean.isEmpty() ? 0 : clean.split(" ").length;
            long[] words = null;
            if (tagged && wordTimes.size() == wordCount) {
                words = new long[wordCount];
                for (int i = 0; i < wordCount; i++) {
                    words[i] = Math.max(0, wordTimes.get(i) - offset);
                }
            }
            for (long at : times) {
                Line l = new Line();
                l.at = Math.max(0, at - offset);
                l.text = clean;
                l.words = words;
                result.add(l);
            }
        }
        Collections.sort(result, (a, b) -> Long.compare(a.at, b.at));
        for (int i = 0; i < result.size(); i++) {
            long next = i + 1 < result.size() ? result.get(i + 1).at : result.get(i).at + 5000;
            result.get(i).end = Math.max(result.get(i).at + 300, Math.min(next - 150, result.get(i).at + 9000));
        }
        return result;
    }

    public static String serialize(ArrayList<Line> list) {
        StringBuilder b = new StringBuilder();
        for (Line l : list) {
            b.append('[').append(formatTime(l.at)).append(']').append(textWithWordTimes(l, true)).append('\n');
        }
        return b.toString();
    }

    static String formatTime(long ms) {
        ms = Math.max(0, ms);
        return String.format(java.util.Locale.US, "%d:%02d.%02d", ms / 60000, (ms / 1000) % 60, (ms % 1000) / 10);
    }

    private static String textWithWordTimes(Line l) {
        return textWithWordTimes(l, false);
    }

    private static String textWithWordTimes(Line l, boolean firstToo) {
        String[] parts = l.text.split(" ");
        if (l.words == null || l.words.length != parts.length) {
            return l.text;
        }
        StringBuilder b = new StringBuilder();
        for (int i = 0; i < parts.length; i++) {
            if (i > 0) {
                b.append(' ');
            }
            if (i > 0 || firstToo) {
                b.append('<').append(formatTime(l.words[i])).append('>');
            }
            b.append(parts[i]);
        }
        return b.toString();
    }

    private static final Pattern HUMAN_HEAD = Pattern.compile("^\\[?(\\d{1,3}):(\\d{1,2})(?:[.,](\\d{1,3}))?\\]?\\s*(.*)$");
    private static final Pattern HUMAN_COMMA = Pattern.compile("<(\\d{1,3}):(\\d{1,2}),(\\d{1,3})>");

    /** "0:12 text" lines (word times as "<0:13>word") to LRC; null when nothing is timed. */
    static String lrcFromHuman(String text) {
        StringBuilder lrc = new StringBuilder();
        int count = 0;
        for (String raw : text.split("\n")) {
            String line = raw.trim();
            if (line.isEmpty()) {
                continue;
            }
            Matcher m = HUMAN_HEAD.matcher(line);
            if (!m.matches() || m.group(4).trim().isEmpty()) {
                continue;
            }
            String seconds = m.group(2).length() == 1 ? "0" + m.group(2) : m.group(2);
            String time = m.group(1) + ":" + seconds + (m.group(3) == null ? "" : "." + m.group(3));
            String rest = HUMAN_COMMA.matcher(m.group(4).trim()).replaceAll("<$1:$2.$3>");
            if (rest.contains("<") && !rest.startsWith("<")) {
                rest = "<" + time + ">" + rest;
            }
            lrc.append('[').append(time).append(']').append(rest).append('\n');
            count++;
        }
        return count == 0 ? null : lrc.toString();
    }

    static String humanFromLines(ArrayList<Line> list) {
        StringBuilder b = new StringBuilder();
        for (Line l : list) {
            b.append(formatTime(l.at)).append(' ').append(textWithWordTimes(l)).append('\n');
        }
        return b.toString();
    }

    // ------------------------------------------------------------ loading

    /** Lines of the song that is playing now (empty while loading or when there are none). */
    public static ArrayList<Line> linesFor(MessageObject message) {
        if (message == null || currentKey == null || !currentKey.equals(keyFor(message))) {
            return new ArrayList<>();
        }
        return lines;
    }

    /** Playback position with the user's offset for this song applied. */
    public static long positionFor(MessageObject message) {
        long position = MediaController.getInstance().getCurrentPosition();
        return message == null ? position : position + offsetFor(keyFor(message));
    }

    /** Whether lyrics for this song are saved on the device. */
    public static boolean hasCached(MessageObject message) {
        return message != null && MwtkStore.file(cacheName(keyFor(message), ".lrc")).exists();
    }

    private static String keyFor(MessageObject message) {
        String title = message.getMusicTitle();
        String performer = message.getMusicAuthor();
        return (performer == null ? "" : performer.toLowerCase()) + " | "
            + (title == null ? "" : title.toLowerCase()) + " | "
            + (int) message.getDuration();
    }

    private static String hash(String key) {
        try {
            MessageDigest digest = MessageDigest.getInstance("SHA-1");
            byte[] bytes = digest.digest(key.getBytes(StandardCharsets.UTF_8));
            StringBuilder b = new StringBuilder();
            for (byte v : bytes) {
                b.append(String.format("%02x", v));
            }
            return b.toString();
        } catch (Throwable e) {
            return String.valueOf(key.hashCode());
        }
    }

    private static String cacheName(String key, String ext) {
        return "lyrics_" + hash(key) + ext;
    }

    private static long offsetFor(String key) {
        return MwtkStore.readJson("lyrics_offsets.json").optLong(key, 0);
    }

    private static void setOffset(String key, long value) {
        JSONObject object = MwtkStore.readJson("lyrics_offsets.json");
        try {
            if (value == 0) {
                object.remove(key);
            } else {
                object.put(key, value);
            }
        } catch (Throwable ignore) {
        }
        MwtkStore.writeJson("lyrics_offsets.json", object);
    }

    private static void track(MessageObject message) {
        String key = message == null ? null : keyFor(message);
        if (key == null ? currentKey == null : key.equals(currentKey)) {
            return;
        }
        currentKey = key;
        lines = new ArrayList<>();
        final int gen = ++generation;
        if (message == null) {
            return;
        }
        final String title = message.getMusicTitle();
        final String performer = message.getMusicAuthor();
        final int duration = (int) message.getDuration();
        Utilities.globalQueue.postRunnable(() -> {
            String cached = MwtkStore.readText(cacheName(key, ".lrc"));
            if (!cached.isEmpty()) {
                deliver(gen, parse(cached));
                return;
            }
            File none = MwtkStore.file(cacheName(key, ".none2"));
            if (none.exists() && System.currentTimeMillis() - none.lastModified() < NOT_FOUND_RETRY) {
                return;
            }
            if (!MwtkConfig.flag(MwtkConfig.LYRICS_ONLINE, true) || title == null || title.isEmpty()) {
                return;
            }
            String lrc = fetch(title, performer, duration);
            if (NETWORK_ERROR.equals(lrc)) {
                return; // try again next time the track is played
            }
            if (lrc == null || lrc.isEmpty()) {
                MwtkStore.writeText(cacheName(key, ".none2"), "");
                return;
            }
            MwtkStore.writeText(cacheName(key, ".lrc"), lrc);
            deliver(gen, parse(lrc));
        });
    }

    private static void deliver(int gen, ArrayList<Line> result) {
        AndroidUtilities.runOnUIThread(() -> {
            if (gen == generation) {
                lines = result;
                if (view != null) {
                    view.reset();
                }
            }
        });
    }

    private static String get(String url) {
        HttpURLConnection connection = null;
        try {
            connection = (HttpURLConnection) new URL(url).openConnection();
            connection.setConnectTimeout(10000);
            connection.setReadTimeout(15000);
            connection.setRequestProperty("User-Agent", "MWTKgram (https://github.com/doxmev/MWTKgram)");
            if (connection.getResponseCode() != 200) {
                return null;
            }
            StringBuilder b = new StringBuilder();
            try (BufferedReader reader = new BufferedReader(new InputStreamReader(connection.getInputStream(), StandardCharsets.UTF_8))) {
                String line;
                while ((line = reader.readLine()) != null) {
                    b.append(line).append('\n');
                }
            }
            return b.toString();
        } catch (Throwable e) {
            return null;
        } finally {
            if (connection != null) {
                connection.disconnect();
            }
        }
    }

    /** Network failed (as opposed to "the site has no lyrics"): do not remember as not found. */
    private static final String NETWORK_ERROR = "\u0000net";

    private static final Pattern VARIANT = Pattern.compile(
        "\\s*[(\\[][^)\\]]*(sped|speed|slow|nightcore|remix|ver|version|edit|prod|feat|ft\\.|lyric|audio|official|cover|instrumental|mood|tiktok|reverb|bass|8d)[^)\\]]*[)\\]]",
        Pattern.CASE_INSENSITIVE);

    /** 1 = sped up, -1 = slowed, 0 = normal. */
    private static int speedOf(String title) {
        String t = title.toLowerCase();
        if (t.contains("sped") || t.contains("speed up") || t.contains("nightcore") || t.contains("ускор")) {
            return 1;
        }
        if (t.contains("slow") || t.contains("замедл")) {
            return -1;
        }
        return 0;
    }

    private static String cleanTitle(String title) {
        String t = VARIANT.matcher(title).replaceAll("");
        t = t.replaceAll("(?i)\\s+(feat\\.?|ft\\.?)\\s.*$", "");
        t = t.replaceAll("(?i)\\s*-\\s*(sped up|slowed|nightcore).*$", "");
        return t.trim().isEmpty() ? title.trim() : t.trim();
    }

    private static String scale(String lrc, double factor) {
        if (Math.abs(factor - 1) < 0.01) {
            return lrc;
        }
        ArrayList<Line> list = parse(lrc);
        for (Line l : list) {
            l.at = Math.round(l.at * factor);
        }
        return serialize(list);
    }

    private static String fetch(String title, String performer, int duration) {
        try {
            int speed = speedOf(title);
            String clean = cleanTitle(title);
            String artist = performer == null ? "" : performer.trim();
            String t = URLEncoder.encode(clean, "UTF-8");
            String a = URLEncoder.encode(artist, "UTF-8");
            boolean reached = false;
            if (speed == 0) {
                String body = get("https://lrclib.net/api/get?track_name=" + t + "&artist_name=" + a
                    + (duration > 0 ? "&duration=" + duration : ""));
                if (body != null) {
                    reached = true;
                    String synced = new JSONObject(body).optString("syncedLyrics", "");
                    if (!synced.isEmpty() && !"null".equals(synced)) {
                        return synced;
                    }
                }
            }
            JSONArray all = new JSONArray();
            String[] queries = {
                "https://lrclib.net/api/search?track_name=" + t + (artist.isEmpty() ? "" : "&artist_name=" + a),
                "https://lrclib.net/api/search?q=" + URLEncoder.encode((artist + " " + clean).trim(), "UTF-8"),
                "https://lrclib.net/api/search?q=" + t,
            };
            for (String q : queries) {
                String list = get(q);
                if (list == null) {
                    continue;
                }
                reached = true;
                JSONArray array = new JSONArray(list);
                for (int i = 0; i < array.length(); i++) {
                    all.put(array.get(i));
                }
                if (all.length() > 0) {
                    break;
                }
            }
            if (!reached) {
                return NETWORK_ERROR;
            }
            String best = null;
            double bestScore = Double.MAX_VALUE;
            double bestFactor = 1;
            String artistLower = artist.toLowerCase();
            String titleLower = clean.toLowerCase();
            for (int i = 0; i < all.length(); i++) {
                JSONObject item = all.optJSONObject(i);
                if (item == null) {
                    continue;
                }
                String synced = item.optString("syncedLyrics", "");
                if (synced.isEmpty() || "null".equals(synced)) {
                    continue;
                }
                String itemTitle = item.optString("trackName", "").toLowerCase();
                String itemArtist = item.optString("artistName", "").toLowerCase();
                if (!itemTitle.contains(titleLower) && !titleLower.contains(itemTitle)) {
                    continue;
                }
                double penalty = artistLower.isEmpty() || itemArtist.contains(artistLower) || artistLower.contains(itemArtist) ? 0 : 50;
                double d = item.optDouble("duration", 0);
                double factor = 1;
                double score;
                if (duration <= 0 || d <= 0) {
                    score = 30;
                } else if (Math.abs(d - duration) <= 3) {
                    score = Math.abs(d - duration);
                } else if (speed != 0) {
                    // A sped up / slowed edit: same words, timeline stretched.
                    double ratio = d / duration;
                    boolean fits = speed > 0 ? (ratio > 1.05 && ratio < 1.7) : (ratio > 0.55 && ratio < 0.95);
                    if (!fits) {
                        continue;
                    }
                    factor = duration / d;
                    score = 5 + Math.abs(ratio - (speed > 0 ? 1.25 : 0.8)) * 20;
                } else if (Math.abs(d - duration) <= 9) {
                    score = 10 + Math.abs(d - duration);
                } else {
                    continue;
                }
                score += penalty;
                if (score < bestScore) {
                    bestScore = score;
                    best = synced;
                    bestFactor = factor;
                }
            }
            return best == null ? null : scale(best, bestFactor);
        } catch (Throwable e) {
            return NETWORK_ERROR;
        }
    }

    // ------------------------------------------------------------ view

    public static void attach(FrameLayout root) {
        if (root == null || view != null) {
            return;
        }
        view = new LyricsView(root.getContext());
        root.addView(view, LayoutHelper.createFrame(LayoutHelper.MATCH_PARENT, LayoutHelper.MATCH_PARENT));
        // Above everything in the window: on phones the screens use elevation
        // and covered the lyrics drawn at Z = 0.
        view.setOutlineProvider(null);
        view.setTranslationZ(AndroidUtilities.dp(900));
        view.start();
    }

    private static final class Word {
        String text;
        float x;
        float y;
        float fromX;
        float fromY;
        float rotation;
        float phase;
        long start;
        long end;
        long out;
        Bitmap dim;
        Bitmap lit;
        boolean gone;
    }

    private static final class Particle {
        float x, y, vx, vy, life, size;
    }

    @SuppressLint("ViewConstructor")
    private static final class LyricsView extends View {

        private final ArrayList<ArrayList<Word>> layouts = new ArrayList<>();
        private final ArrayList<Integer> layoutIndex = new ArrayList<>();
        private final ArrayList<Particle> particles = new ArrayList<>();
        private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.FILTER_BITMAP_FLAG);
        private final TextPaint hintPaint = new TextPaint(Paint.ANTI_ALIAS_FLAG);
        private final MwtkBackgroundDrawable backdrop = new MwtkBackgroundDrawable();
        private final Runnable poll = this::poll;
        private boolean running;
        private boolean hideMode;
        private long lastFrame;
        private final ImageReceiver cover = new ImageReceiver(this);
        private int coverMessageId;
        private long coverDialogId;
        private final android.graphics.RectF coverRect = new android.graphics.RectF();
        private final android.graphics.RectF tmpRect = new android.graphics.RectF();
        private float beat;

        LyricsView(Context context) {
            super(context);
            setWillNotDraw(false);
            setClickable(false);
            cover.setRoundRadius(AndroidUtilities.dp(18));
            hintPaint.setTextSize(AndroidUtilities.dp(13));
            hintPaint.setColor(0x99ffffff);
            hintPaint.setTextAlign(Paint.Align.CENTER);
            backdrop.setCallback(this);
        }

        @Override
        protected boolean verifyDrawable(android.graphics.drawable.Drawable who) {
            return who == backdrop || super.verifyDrawable(who);
        }

        @Override
        protected void onAttachedToWindow() {
            super.onAttachedToWindow();
            cover.onAttachedToWindow();
        }

        @Override
        protected void onDetachedFromWindow() {
            cover.onDetachedFromWindow();
            MwtkOverlay.setContentHidden(false);
            super.onDetachedFromWindow();
        }

        // ---------------------------------------------- where things go

        /** The cover side for a line: it changes every two lines. */
        private int coverMode(int index) {
            int seed = (currentKey == null ? 0 : currentKey.hashCode()) + (index / 2) * 7919;
            if (getWidth() > getHeight()) {
                return Math.floorMod(seed, 3); // 0 top, 1 left, 2 right
            }
            return Math.floorMod(seed, 2) == 0 ? 0 : 3; // 0 top, 3 bottom
        }

        private void coverTarget(int mode, android.graphics.RectF out) {
            float w = getWidth();
            float h = getHeight();
            float side = Math.min(w, h) * (mode == 1 || mode == 2 ? 0.36f : 0.40f);
            float cx = mode == 1 ? w * 0.25f : mode == 2 ? w * 0.75f : w / 2f;
            float cy = mode == 0 ? h * 0.27f : mode == 3 ? h * 0.71f : h * 0.48f;
            out.set(cx - side / 2, cy - side / 2, cx + side / 2, cy + side / 2);
        }

        /** Where the words of a line may go. */
        private android.graphics.RectF textArea(int index) {
            float w = getWidth();
            float h = getHeight();
            if (hideMode) {
                switch (coverMode(index)) {
                    case 1: return new android.graphics.RectF(w * 0.47f, h * 0.10f, w * 0.97f, h * 0.88f);
                    case 2: return new android.graphics.RectF(w * 0.03f, h * 0.10f, w * 0.53f, h * 0.88f);
                    case 3: return new android.graphics.RectF(w * 0.05f, h * 0.10f, w * 0.95f, h * 0.46f);
                    default: return new android.graphics.RectF(w * 0.05f, h * 0.53f, w * 0.95f, h * 0.88f);
                }
            }
            // Tablet mode: the free right side.
            android.graphics.Rect free = MwtkTablet.freeArea(this);
            if (free != null) {
                return new android.graphics.RectF(free);
            }
            // Tablet mode off on a big screen: a phone-like column in the middle.
            float column = Math.min(w, AndroidUtilities.dp(560));
            return new android.graphics.RectF((w - column) / 2f, 0, (w + column) / 2f, h);
        }

        void start() {
            if (!running) {
                running = true;
                post(poll);
            }
        }

        void reset() {
            for (ArrayList<Word> words : layouts) {
                recycle(words);
            }
            layouts.clear();
            layoutIndex.clear();
            invalidate();
        }

        private void recycle(ArrayList<Word> words) {
            for (Word w : words) {
                if (w.dim != null) {
                    w.dim.recycle();
                }
                if (w.lit != null) {
                    w.lit.recycle();
                }
            }
        }

        private boolean enabled() {
            MwtkConfig.load();
            return MwtkConfig.effects && MwtkConfig.flag(MwtkConfig.LYRICS, true);
        }

        private void poll() {
            MessageObject playing = MediaController.getInstance().getPlayingMessageObject();
            boolean music = playing != null && playing.isMusic();
            track(music && enabled() ? playing : null);
            boolean active = music && enabled() && !lines.isEmpty();
            boolean playingNow = active && !MediaController.getInstance().isMessagePaused();
            boolean hide = active && MwtkConfig.flag(MwtkConfig.HIDE_CHATS, false);
            if (hide != hideMode) {
                hideMode = hide;
                setClickable(hide);
                // Really hide the chats, do not just paint over them.
                MwtkOverlay.setContentHidden(hide);
                reset();
            }
            setVisibility(active || !particles.isEmpty() ? VISIBLE : GONE);
            if (playingNow || !particles.isEmpty()) {
                invalidate();
                postDelayed(poll, 16);
            } else {
                postDelayed(poll, 500);
            }
        }

        @Override
        public boolean onTouchEvent(MotionEvent event) {
            if (hideMode && event.getAction() == MotionEvent.ACTION_UP) {
                MwtkConfig.setFlag(MwtkConfig.HIDE_CHATS, false);
                return true;
            }
            return hideMode || super.onTouchEvent(event);
        }

        private long position() {
            long position = MediaController.getInstance().getCurrentPosition();
            return position + (currentKey != null ? offsetFor(currentKey) : 0);
        }

        private Bitmap render(String text, float size, boolean lit, int style, int accent) {
            TextPaint p = new TextPaint(Paint.ANTI_ALIAS_FLAG);
            p.setTextSize(size);
            p.setTypeface(style == 2 ? Typeface.MONOSPACE : Typeface.create(Typeface.DEFAULT, Typeface.BOLD));
            p.setAntiAlias(style != 2);
            int pad = (int) (size / 2);
            int w = (int) p.measureText(text) + pad * 2;
            int h = (int) (size * 1.3f) + pad * 2;
            Bitmap bitmap = Bitmap.createBitmap(Math.max(w, 1), Math.max(h, 1), Bitmap.Config.ARGB_8888);
            Canvas c = new Canvas(bitmap);
            float baseY = pad + size;
            if (style == 1) {
                p.setColor(lit ? Color.argb(230, Color.red(accent) / 3, Color.green(accent) / 3, Color.blue(accent) / 3) : 0x55220004);
                int depth = Math.max(2, (int) (size / 12));
                for (int d = depth; d > 0; d--) {
                    c.drawText(text, pad + d, baseY + d, p);
                }
            }
            if (lit && style != 2) {
                Paint glow = new TextPaint(p);
                glow.setColor(accent);
                glow.setMaskFilter(new BlurMaskFilter(size / 4, BlurMaskFilter.Blur.NORMAL));
                c.drawText(text, pad, baseY, glow);
                c.drawText(text, pad, baseY, glow);
            }
            p.setMaskFilter(null);
            p.setColor(lit ? 0xffffe1e6 : 0x50ffe9ec);
            c.drawText(text, pad, baseY, p);
            return bitmap;
        }

        private ArrayList<Word> layoutLine(int index) {
            Line line = lines.get(index);
            ArrayList<Word> result = new ArrayList<>();
            String[] texts = line.text.trim().split("\\s+");
            if (line.text.trim().isEmpty() || texts.length == 0) {
                return result;
            }
            int layout = MwtkConfig.num(MwtkConfig.LYRICS_LAYOUT, 0);
            int style = MwtkConfig.num(MwtkConfig.LYRICS_STYLE, 0);
            int appear = MwtkConfig.num(MwtkConfig.LYRICS_APPEAR, 0);
            float sizeK = MwtkConfig.num(MwtkConfig.LYRICS_SIZE, 100) / 100f;
            int accent = MwtkConfig.accent();
            Random r = new Random(currentKey == null ? index : currentKey.hashCode() * 31L + index);
            android.graphics.RectF area = textArea(index);
            float areaW = area.width();
            float areaH = area.height();
            boolean wide = MwtkTablet.enabled() && areaW > AndroidUtilities.dp(420);
            float base = Math.max(AndroidUtilities.dp(22), Math.min(areaW / (wide ? 14f : 9f), AndroidUtilities.dp(wide ? 56 : 40))) * sizeK;
            int n = texts.length;
            int total = 0;
            for (String t : texts) {
                total += t.length() + 1;
            }
            double span = Math.max(line.end - line.at, 400) * 0.8;
            double at = line.at;
            float[] mul = new float[n];
            int[] want = new int[n];
            int perRow = layout == 1 ? 1 : (wide ? 6 : 3);
            for (int i = 0; i < n; i++) {
                Word w = new Word();
                w.text = texts[i];
                if (line.words != null && line.words.length == n) {
                    w.start = line.words[i];
                    w.end = i + 1 < n ? line.words[i + 1] : line.end;
                } else {
                    w.start = (long) at;
                    at += span * (texts[i].length() + 1) / total;
                    w.end = (long) at;
                }
                w.out = line.end + (layout == 2 ? 0 : (long) (350f * i / n));
                w.phase = r.nextFloat() * 6.28f;
                w.rotation = layout == 0 ? (r.nextFloat() - 0.5f) * 12f : layout == 1 ? -3f : 0;
                mul[i] = layout == 2 ? 1f : 0.8f + r.nextFloat() * 0.45f;
                want[i] = layout == 0 ? 1 + r.nextInt(perRow) : (layout == 2 ? 1000 : perRow);
                result.add(w);
            }
            float jitterX = (r.nextFloat() - 0.5f);
            float jitterY = (r.nextFloat() - 0.5f);

            // Rows that fit the area; the text shrinks when a line is too long.
            TextPaint measure = new TextPaint(Paint.ANTI_ALIAS_FLAG);
            measure.setTypeface(style == 2 ? Typeface.MONOSPACE : Typeface.create(Typeface.DEFAULT, Typeface.BOLD));
            float[] widths = new float[n];
            ArrayList<ArrayList<Integer>> rows = new ArrayList<>();
            for (int attempt = 0; attempt < 5; attempt++) {
                rows.clear();
                ArrayList<Integer> row = new ArrayList<>();
                float rowWidth = 0;
                float gap = base * 0.3f;
                float widest = 0;
                for (int i = 0; i < n; i++) {
                    measure.setTextSize(base * mul[i]);
                    widths[i] = measure.measureText(texts[i]);
                    if (!row.isEmpty() && (row.size() >= want[i] || rowWidth + gap + widths[i] > areaW * 0.9f)) {
                        rows.add(row);
                        row = new ArrayList<>();
                        rowWidth = 0;
                    }
                    rowWidth += (row.isEmpty() ? 0 : gap) + widths[i];
                    widest = Math.max(widest, rowWidth);
                    row.add(i);
                }
                if (!row.isEmpty()) {
                    rows.add(row);
                }
                float tall = rows.size() * base * 1.3f + (layout == 1 ? rows.size() * base * 0.1f : 0);
                float fit = Math.min(areaW * 0.94f / Math.max(widest, 1), areaH * 0.9f / Math.max(tall, 1));
                if (fit >= 0.999f || base <= AndroidUtilities.dp(12)) {
                    break;
                }
                base = Math.max(AndroidUtilities.dp(12), base * fit * 0.98f);
            }

            float cx = area.centerX() + (layout == 2 ? 0 : jitterX * areaW * 0.15f);
            float cy = area.top + (layout == 2 ? areaH * 0.45f : areaH * (0.42f + jitterY * 0.12f));
            float rowH = base * 1.3f;
            float gap = base * 0.3f;
            for (int ri = 0; ri < rows.size(); ri++) {
                ArrayList<Integer> row = rows.get(ri);
                float rowTotal = 0;
                for (int i : row) {
                    rowTotal += widths[i];
                }
                rowTotal += gap * (row.size() - 1);
                float x = cx - rowTotal / 2;
                if (layout == 1) {
                    x = cx - areaW * 0.22f + ri * base * 0.6f;
                }
                for (int i : row) {
                    Word w = result.get(i);
                    w.x = x + widths[i] / 2;
                    w.y = cy + (ri - (rows.size() - 1) / 2f) * rowH;
                    x += widths[i] + gap;
                }
            }
            // Keep the whole line inside the area.
            float minX = Float.MAX_VALUE, maxX = -Float.MAX_VALUE, minY = Float.MAX_VALUE, maxY = -Float.MAX_VALUE;
            for (int i = 0; i < n; i++) {
                Word w = result.get(i);
                float hh = base * mul[i] * 0.7f;
                minX = Math.min(minX, w.x - widths[i] / 2);
                maxX = Math.max(maxX, w.x + widths[i] / 2);
                minY = Math.min(minY, w.y - hh);
                maxY = Math.max(maxY, w.y + hh);
            }
            float shiftX = minX < area.left ? area.left - minX : (maxX > area.right ? area.right - maxX : 0);
            float shiftY = minY < area.top ? area.top - minY : (maxY > area.bottom ? area.bottom - maxY : 0);

            // Where the words fly in from.
            android.graphics.RectF coverBox = new android.graphics.RectF();
            coverTarget(coverMode(index), coverBox);
            for (int i = 0; i < n; i++) {
                Word w = result.get(i);
                w.x += shiftX;
                w.y += shiftY;
                switch (appear) {
                    case 1:
                        w.fromX = area.centerX() - w.x;
                        w.fromY = area.centerY() - w.y;
                        break;
                    case 2:
                        w.fromX = w.x < getWidth() / 2f ? -w.x - base * 2 : getWidth() - w.x + base * 2;
                        w.fromY = (r.nextFloat() - 0.5f) * base;
                        break;
                    case 3:
                        w.fromX = (r.nextFloat() - 0.5f) * base * 2;
                        w.fromY = getHeight() - w.y + base * 2;
                        break;
                    case 4:
                        w.fromX = (hideMode ? coverBox.centerX() : area.centerX()) - w.x;
                        w.fromY = (hideMode ? coverBox.centerY() : area.centerY()) - w.y;
                        break;
                    default: {
                        double angle = r.nextFloat() * 6.28;
                        w.fromX = (float) Math.cos(angle) * base * 3;
                        w.fromY = (float) Math.sin(angle) * base * 2;
                    }
                }
                float size = base * mul[i];
                w.dim = render(w.text, size, false, style, accent);
                w.lit = render(w.text, size, true, style, accent);
            }
            return result;
        }

        private void shatter(Word w, float x, float y) {
            if (particles.size() > 500 || w.lit == null) {
                return;
            }
            int step = Math.max(4, AndroidUtilities.dp(3));
            Random r = new Random();
            for (int py = 0; py < w.lit.getHeight(); py += step) {
                for (int px = 0; px < w.lit.getWidth(); px += step) {
                    if (Color.alpha(w.lit.getPixel(px, py)) < 150 || r.nextFloat() > 0.5f) {
                        continue;
                    }
                    Particle p = new Particle();
                    p.x = x - w.lit.getWidth() / 2f + px;
                    p.y = y - w.lit.getHeight() / 2f + py;
                    p.vx = (r.nextFloat() - 0.5f) * AndroidUtilities.dp(120);
                    p.vy = -r.nextFloat() * AndroidUtilities.dp(90) - AndroidUtilities.dp(10);
                    p.life = 0.7f + 0.3f * r.nextFloat();
                    p.size = AndroidUtilities.dp(1.6f + r.nextFloat() * 1.2f);
                    particles.add(p);
                }
            }
        }

        private int currentLine(long now) {
            int current = 0;
            for (int i = 0; i < lines.size(); i++) {
                if (lines.get(i).at <= now) {
                    current = i;
                }
            }
            return current;
        }

        private float[] spectrum(float seconds, int count) {
            float[] real = MwtkBeat.bands();
            float[] result = new float[count];
            boolean silent = true;
            for (float v : real) {
                if (v > 0.01f) {
                    silent = false;
                    break;
                }
            }
            boolean playing = !MediaController.getInstance().isMessagePaused();
            for (int i = 0; i < count; i++) {
                if (!silent) {
                    // Low notes in the middle, mirrored to both sides.
                    int mirrored = Math.abs(i - count / 2) * 2 * real.length / Math.max(count, 1);
                    result[i] = real[Math.min(real.length - 1, mirrored)];
                } else if (playing) {
                    // No microphone permission: a calm made-up motion.
                    result[i] = 0.15f + 0.25f * (0.5f + 0.5f * (float) Math.sin(seconds * (3 + i % 5) + i * 1.7f));
                }
            }
            return result;
        }

        private void drawCover(Canvas canvas, long now, float dt, long frame) {
            MessageObject playing = MediaController.getInstance().getPlayingMessageObject();
            if (playing == null) {
                return;
            }
            if (playing.getId() != coverMessageId || playing.getDialogId() != coverDialogId) {
                coverMessageId = playing.getId();
                coverDialogId = playing.getDialogId();
                TLRPC.Document document = playing.getDocument();
                TLRPC.PhotoSize thumb = document != null ? FileLoader.getClosestPhotoSizeWithSize(document.thumbs, 320) : null;
                if (thumb != null) {
                    cover.setImage(ImageLocation.getForDocument(thumb, document), "320_320", null, null, playing, 0);
                } else {
                    cover.setImageBitmap((android.graphics.drawable.Drawable) null);
                }
            }
            float seconds = frame / 1000f;
            coverTarget(coverMode(currentLine(now)), tmpRect);
            // The cover keeps drifting a little around its place.
            tmpRect.offset(
                (float) (Math.sin(seconds * 0.23f) * getWidth() * 0.03f + Math.sin(seconds * 0.61f) * getWidth() * 0.01f),
                (float) (Math.cos(seconds * 0.31f) * getHeight() * 0.025f));
            if (coverRect.isEmpty() || dt <= 0) {
                coverRect.set(tmpRect);
            } else {
                float k = Math.min(1f, dt * 2.2f);
                coverRect.set(
                    coverRect.left + (tmpRect.left - coverRect.left) * k,
                    coverRect.top + (tmpRect.top - coverRect.top) * k,
                    coverRect.right + (tmpRect.right - coverRect.right) * k,
                    coverRect.bottom + (tmpRect.bottom - coverRect.bottom) * k);
            }
            float half = coverRect.width() / 2f;
            float cx = coverRect.centerX();
            float cy = coverRect.centerY() + (float) Math.sin(seconds * 1.1f) * half * 0.06f;
            float zoom = 1f + 0.10f * beat;
            int accent = MwtkConfig.accent();

            // A ring of bars around the cover jumping with the music.
            float[] values = spectrum(seconds, 48);
            paint.setStyle(Paint.Style.STROKE);
            paint.setStrokeCap(Paint.Cap.ROUND);
            paint.setStrokeWidth(Math.max(AndroidUtilities.dp(2.5f), half * 0.035f));
            float inner = half * 1.48f * zoom;
            float longest = half * 0.45f;
            for (int i = 0; i < values.length; i++) {
                double a = i * 2 * Math.PI / values.length - Math.PI / 2;
                float v = Math.min(1f, 0.06f + values[i] + beat * 0.25f);
                paint.setColor(accent);
                paint.setAlpha((int) (120 + 120 * v));
                float c = (float) Math.cos(a);
                float sn = (float) Math.sin(a);
                canvas.drawLine(cx + c * inner, cy + sn * inner, cx + c * (inner + longest * v), cy + sn * (inner + longest * v), paint);
            }
            paint.setStyle(Paint.Style.FILL);
            paint.setStrokeCap(Paint.Cap.BUTT);

            canvas.save();
            canvas.translate(cx, cy);
            canvas.rotate((float) (Math.sin(seconds * 0.8f) * 5 + Math.sin(seconds * 0.37f) * 3));
            canvas.scale(zoom, zoom);
            paint.setColor(accent);
            for (int i = 4; i > 0; i--) {
                paint.setAlpha((int) ((18 + 50 * beat) / i));
                float grow = half * 0.05f * i;
                tmpRect.set(-half - grow, -half - grow, half + grow, half + grow);
                canvas.drawRoundRect(tmpRect, AndroidUtilities.dp(18) + grow, AndroidUtilities.dp(18) + grow, paint);
            }
            if (cover.hasImageSet()) {
                cover.setImageCoords(-half, -half, half * 2, half * 2);
                cover.draw(canvas);
            } else {
                paint.setColor(Color.rgb(Color.red(accent) / 3, Color.green(accent) / 3, Color.blue(accent) / 3));
                paint.setAlpha(255);
                tmpRect.set(-half, -half, half, half);
                canvas.drawRoundRect(tmpRect, AndroidUtilities.dp(18), AndroidUtilities.dp(18), paint);
                hintPaint.setTextSize(half * 0.9f);
                canvas.drawText("♪", 0, half * 0.3f, hintPaint);
                hintPaint.setTextSize(AndroidUtilities.dp(13));
            }
            canvas.restore();
            paint.setAlpha(255);

            // Equalizer bars under the text.
            android.graphics.RectF area = textArea(currentLine(now));
            float[] bars = spectrum(seconds + 0.5f, 28);
            float full = Math.min(area.width() * 0.8f, AndroidUtilities.dp(320));
            float step = full / bars.length;
            float tallest = Math.max(area.height() * 0.08f, AndroidUtilities.dp(14));
            float bottom = area.bottom - AndroidUtilities.dp(4);
            float left = area.centerX() - full / 2;
            for (int i = 0; i < bars.length; i++) {
                float v = Math.max(0.05f, Math.min(1f, bars[i] + beat * 0.3f));
                paint.setColor(accent);
                paint.setAlpha((int) (70 + 130 * v));
                float bh = tallest * v;
                tmpRect.set(left + i * step + step * 0.2f, bottom - bh, left + i * step + step * 0.8f, bottom);
                canvas.drawRoundRect(tmpRect, step * 0.3f, step * 0.3f, paint);
            }
            paint.setAlpha(255);
        }

        @Override
        protected void onDraw(Canvas canvas) {
            long frame = System.currentTimeMillis();
            float dt = lastFrame == 0 ? 0 : Math.min((frame - lastFrame) / 1000f, 0.1f);
            lastFrame = frame;
            beat = MwtkBeat.level();
            long now = position();
            if (hideMode) {
                backdrop.setBounds(0, 0, getWidth(), getHeight());
                backdrop.draw(canvas);
                canvas.drawText("Нажми, чтобы вернуть чаты", getWidth() / 2f, getHeight() - AndroidUtilities.dp(24), hintPaint);
                drawCover(canvas, now, dt, frame);
            }
            if (enabled() && !lines.isEmpty() && getWidth() > 0) {
                for (int i = 0; i < lines.size(); i++) {
                    Line l = lines.get(i);
                    boolean visible = now >= l.at - 450 && now < l.end + 1100;
                    int existing = layoutIndex.indexOf(i);
                    if (visible && existing < 0) {
                        layouts.add(layoutLine(i));
                        layoutIndex.add(i);
                    } else if (!visible && existing >= 0) {
                        recycle(layouts.remove(existing));
                        layoutIndex.remove(existing);
                    }
                }
                float motion = MwtkConfig.num(MwtkConfig.LYRICS_MOTION, 60) / 100f;
                int vanish = MwtkConfig.num(MwtkConfig.LYRICS_VANISH, 0);
                int drift = MwtkConfig.num(MwtkConfig.LYRICS_DRIFT, 0);
                float seconds = frame / 1000f;
                for (ArrayList<Word> words : layouts) {
                    for (Word w : words) {
                        if (w.gone || w.dim == null || now < w.start - 450) {
                            continue;
                        }
                        float k = Math.min(1f, Math.max(0f, (now - (w.start - 450)) / 550f));
                        float ease = 1f - (1f - k) * (1f - k) * (1f - k);
                        float onScreen = Math.max(0f, (now - w.start) / 1000f);
                        float dx;
                        float dy;
                        switch (drift) {
                            case 1: // wave
                                dx = (float) Math.sin(seconds * 0.7f + w.phase) * AndroidUtilities.dp(3) * motion;
                                dy = (float) Math.sin(seconds * 2.4f - w.x / AndroidUtilities.dp(40)) * AndroidUtilities.dp(9) * motion;
                                break;
                            case 2: { // spiral
                                float radius = (AndroidUtilities.dp(4) + onScreen * AndroidUtilities.dp(5)) * motion;
                                float angle = seconds * 1.7f + w.phase;
                                dx = (float) Math.cos(angle) * radius;
                                dy = (float) Math.sin(angle) * radius;
                                break;
                            }
                            case 3: // shake on the beat
                                dx = (float) Math.sin(seconds * 37f + w.phase * 5f) * AndroidUtilities.dp(6) * beat
                                    + (float) Math.sin(seconds * 0.9f + w.phase) * AndroidUtilities.dp(2) * motion;
                                dy = (float) Math.cos(seconds * 41f + w.phase * 3f) * AndroidUtilities.dp(5) * beat
                                    + (float) Math.cos(seconds * 1.1f + w.phase) * AndroidUtilities.dp(2) * motion;
                                break;
                            default: // float
                                dx = (float) Math.sin(seconds * 0.9f + w.phase) * AndroidUtilities.dp(4) * motion
                                    + onScreen * AndroidUtilities.dp(2) * motion * (float) Math.cos(w.phase);
                                dy = (float) Math.cos(seconds * 1.1f + w.phase) * AndroidUtilities.dp(3) * motion
                                    - onScreen * AndroidUtilities.dp(1.5f) * motion;
                        }
                        float x = w.x + w.fromX * (1 - ease) + dx;
                        float y = w.y + w.fromY * (1 - ease) + dy;
                        float alpha = ease;
                        boolean singing = now >= w.start && now < w.out;
                        float scale = (0.3f + 0.7f * ease) * (1f + (singing ? 0.14f : 0.06f) * beat);
                        if (now >= w.out) {
                            if (vanish == 0) {
                                shatter(w, x, y);
                                w.gone = true;
                                continue;
                            }
                            float v = Math.min(1f, (now - w.out) / 600f);
                            alpha *= 1 - v;
                            if (vanish == 1) {
                                scale *= 1 + 0.18f * v;
                            } else {
                                y -= v * v * AndroidUtilities.dp(80);
                            }
                            if (v >= 1) {
                                w.gone = true;
                                continue;
                            }
                        }
                        float lit = Math.min(1f, Math.max(0f, (now - w.start) / (float) Math.max(w.end - w.start, 50)));
                        canvas.save();
                        canvas.translate(x, y);
                        canvas.rotate(w.rotation);
                        canvas.scale(scale, scale);
                        paint.setAlpha((int) (255 * alpha));
                        float left = -w.dim.getWidth() / 2f;
                        float top = -w.dim.getHeight() / 2f;
                        canvas.drawBitmap(w.dim, left, top, paint);
                        if (lit > 0) {
                            float pad = w.dim.getHeight() / 4f;
                            canvas.clipRect(left, top, left + pad + (w.dim.getWidth() - pad * 2) * lit + (lit >= 1 ? pad : 0), top + w.dim.getHeight());
                            canvas.drawBitmap(w.lit, left, top, paint);
                        }
                        canvas.restore();
                    }
                }
            }
            if (!particles.isEmpty()) {
                Paint p = paint;
                for (int i = particles.size() - 1; i >= 0; i--) {
                    Particle pt = particles.get(i);
                    pt.life -= dt / 1.1f;
                    pt.vy += AndroidUtilities.dp(160) * dt;
                    pt.x += pt.vx * dt;
                    pt.y += pt.vy * dt;
                    if (pt.life <= 0) {
                        particles.remove(i);
                        continue;
                    }
                    p.setColor(Color.argb((int) (255 * pt.life), 255, (int) (60 + 120 * pt.life), (int) (70 + 80 * pt.life)));
                    canvas.drawRect(pt.x, pt.y, pt.x + pt.size, pt.y + pt.size, p);
                }
                p.setColor(0xffffffff);
            }
        }
    }

    // ------------------------------------------------------------ boxes

    public static void showOffsetBox(BaseFragment fragment, MessageObject message) {
        Context context = fragment.getParentActivity();
        if (context == null || message == null) {
            return;
        }
        final String key = keyFor(message);
        final long[] value = { offsetFor(key) };
        LinearLayout box = new LinearLayout(context);
        box.setOrientation(LinearLayout.VERTICAL);
        TextView label = new TextView(context);
        label.setTextColor(Theme.getColor(Theme.key_windowBackgroundWhiteBlackText));
        label.setTextSize(16);
        Runnable update = () -> label.setText(String.format(java.util.Locale.US,
            "Текст идёт %s на %.1f с", value[0] >= 0 ? "раньше" : "позже", Math.abs(value[0]) / 1000f));
        update.run();
        box.addView(label, LayoutHelper.createLinear(LayoutHelper.MATCH_PARENT, LayoutHelper.WRAP_CONTENT, 24, 4, 24, 4));
        AlertDialog.Builder builder = new AlertDialog.Builder(context);
        builder.setTitle("Сдвиг текста песни");
        builder.setView(box);
        builder.setPositiveButton("+0.5 с", null);
        builder.setNegativeButton("−0.5 с", null);
        builder.setNeutralButton("Сброс", null);
        AlertDialog dialog = builder.create();
        fragment.showDialog(dialog);
        View plus = dialog.getButton(AlertDialog.BUTTON_POSITIVE);
        View minus = dialog.getButton(AlertDialog.BUTTON_NEGATIVE);
        View reset = dialog.getButton(AlertDialog.BUTTON_NEUTRAL);
        if (plus != null) {
            plus.setOnClickListener(v -> { value[0] += 500; setOffset(key, value[0]); update.run(); });
        }
        if (minus != null) {
            minus.setOnClickListener(v -> { value[0] -= 500; setOffset(key, value[0]); update.run(); });
        }
        if (reset != null) {
            reset.setOnClickListener(v -> { value[0] = 0; setOffset(key, 0); update.run(); });
        }
    }

    public static void showSyncEditor(BaseFragment fragment, MessageObject message) {
        Context context = fragment.getParentActivity();
        if (context == null || message == null) {
            return;
        }
        final String key = keyFor(message);
        LinearLayout box = new LinearLayout(context);
        box.setOrientation(LinearLayout.VERTICAL);
        TextView hint = new TextView(context);
        hint.setTextColor(Theme.getColor(Theme.key_windowBackgroundWhiteGrayText));
        hint.setTextSize(14);
        hint.setText("Каждая строчка: время, потом текст.\n"
            + "0:12 Пообещай мне\n"
            + "0:15.5 Что ты будешь рядом\n"
            + "Время отдельного слова: 0:20 Я <0:21>тебя <0:22>жду\n"
            + "«Время сейчас» вставляет текущую секунду трека туда, где курсор.");
        box.addView(hint, LayoutHelper.createLinear(LayoutHelper.MATCH_PARENT, LayoutHelper.WRAP_CONTENT, 24, 0, 24, 8));
        String existing = "";
        if (key.equals(currentKey) && !lines.isEmpty()) {
            existing = humanFromLines(lines);
        } else {
            String cached = MwtkStore.readText(cacheName(key, ".lrc"));
            if (!cached.isEmpty()) {
                existing = humanFromLines(parse(cached));
            }
        }
        EditText field = MwtkUi.field(context, existing, "0:12 Первая строчка", true, false);
        field.setMinLines(6);
        field.setMaxLines(14);
        field.setVerticalScrollBarEnabled(true);
        box.addView(field, LayoutHelper.createLinear(LayoutHelper.MATCH_PARENT, LayoutHelper.WRAP_CONTENT, 24, 0, 24, 8));
        TextView status = new TextView(context);
        status.setTextColor(Theme.getColor(Theme.key_text_RedRegular));
        status.setTextSize(14);
        box.addView(status, LayoutHelper.createLinear(LayoutHelper.MATCH_PARENT, LayoutHelper.WRAP_CONTENT, 24, 0, 24, 4));
        AlertDialog.Builder builder = new AlertDialog.Builder(context);
        builder.setTitle("Текст песни");
        builder.setView(box);
        builder.setPositiveButton("Сохранить", null);
        builder.setNeutralButton("Время сейчас", null);
        builder.setNegativeButton("Отмена", null);
        AlertDialog dialog = builder.create();
        fragment.showDialog(dialog);
        View saveButton = dialog.getButton(AlertDialog.BUTTON_POSITIVE);
        View nowButton = dialog.getButton(AlertDialog.BUTTON_NEUTRAL);
        if (nowButton != null) {
            nowButton.setOnClickListener(v -> {
                String stamp = formatTime(MediaController.getInstance().getCurrentPosition()) + " ";
                int start = Math.max(field.getSelectionStart(), 0);
                int end = Math.max(field.getSelectionEnd(), 0);
                field.getText().replace(Math.min(start, end), Math.max(start, end), stamp);
                field.requestFocus();
            });
        }
        if (saveButton != null) {
            saveButton.setOnClickListener(v -> {
                String lrc = lrcFromHuman(field.getText().toString());
                if (lrc == null) {
                    status.setText("Нет ни одной строчки со временем. Пример: 0:12 Текст");
                    return;
                }
                MwtkStore.writeText(cacheName(key, ".lrc"), lrc);
                MwtkStore.file(cacheName(key, ".none2")).delete();
                if (key.equals(currentKey)) {
                    currentKey = null;
                }
                dialog.dismiss();
                MwtkUi.toast("Текст сохранён");
            });
        }
    }

    /** Manual search on LRCLIB: the user picks the right version from a list. */
    public static void showSearch(BaseFragment fragment, MessageObject message) {
        Context context = fragment.getParentActivity();
        if (context == null || message == null) {
            return;
        }
        final String key = keyFor(message);
        final int duration = (int) message.getDuration();
        String title = message.getMusicTitle() == null ? "" : cleanTitle(message.getMusicTitle());
        String performer = message.getMusicAuthor() == null ? "" : message.getMusicAuthor();
        LinearLayout box = new LinearLayout(context);
        box.setOrientation(LinearLayout.VERTICAL);
        EditText field = MwtkUi.field(context, (performer + " " + title).trim(), "Исполнитель и название", false, false);
        box.addView(field, LayoutHelper.createLinear(LayoutHelper.MATCH_PARENT, LayoutHelper.WRAP_CONTENT, 24, 0, 24, 8));
        TextView status = new TextView(context);
        status.setTextColor(Theme.getColor(Theme.key_windowBackgroundWhiteGrayText));
        status.setTextSize(14);
        status.setText("Нажми «Искать» и выбери подходящий вариант.");
        box.addView(status, LayoutHelper.createLinear(LayoutHelper.MATCH_PARENT, LayoutHelper.WRAP_CONTENT, 24, 0, 24, 6));
        // Lyricsify has no API: it opens in the browser, and the copied .lrc
        // text is pasted back here.
        TextView lyricsify = new TextView(context);
        lyricsify.setTextSize(15);
        lyricsify.setTextColor(Theme.getColor(Theme.key_windowBackgroundWhiteBlueText4));
        lyricsify.setText("Открыть Lyricsify в браузере");
        lyricsify.setPadding(0, AndroidUtilities.dp(6), 0, AndroidUtilities.dp(6));
        box.addView(lyricsify, LayoutHelper.createLinear(LayoutHelper.MATCH_PARENT, LayoutHelper.WRAP_CONTENT, 24, 0, 24, 0));
        TextView paste = new TextView(context);
        paste.setTextSize(15);
        paste.setTextColor(Theme.getColor(Theme.key_windowBackgroundWhiteBlueText4));
        paste.setText("Вставить скопированный .lrc текст");
        paste.setPadding(0, AndroidUtilities.dp(6), 0, AndroidUtilities.dp(6));
        box.addView(paste, LayoutHelper.createLinear(LayoutHelper.MATCH_PARENT, LayoutHelper.WRAP_CONTENT, 24, 0, 24, 6));
        android.widget.ScrollView scroll = new android.widget.ScrollView(context);
        LinearLayout list = new LinearLayout(context);
        list.setOrientation(LinearLayout.VERTICAL);
        scroll.addView(list, new android.widget.FrameLayout.LayoutParams(LayoutHelper.MATCH_PARENT, LayoutHelper.WRAP_CONTENT));
        box.addView(scroll, LayoutHelper.createLinear(LayoutHelper.MATCH_PARENT, 260, 16, 0, 16, 0));
        AlertDialog.Builder builder = new AlertDialog.Builder(context);
        builder.setTitle("Найти текст песни");
        builder.setView(box);
        builder.setPositiveButton("Искать", null);
        builder.setNegativeButton("Закрыть", null);
        AlertDialog dialog = builder.create();
        fragment.showDialog(dialog);
        lyricsify.setOnClickListener(v -> {
            try {
                String q = URLEncoder.encode(field.getText().toString().trim(), "UTF-8");
                org.telegram.messenger.browser.Browser.openUrl(context, "https://www.lyricsify.com/search?q=" + q);
            } catch (Throwable ignore) {
            }
        });
        paste.setOnClickListener(v -> {
            android.content.ClipboardManager clipboard = (android.content.ClipboardManager) context.getSystemService(Context.CLIPBOARD_SERVICE);
            CharSequence text = null;
            if (clipboard != null && clipboard.hasPrimaryClip() && clipboard.getPrimaryClip() != null
                && clipboard.getPrimaryClip().getItemCount() > 0) {
                text = clipboard.getPrimaryClip().getItemAt(0).coerceToText(context);
            }
            String lrc = text == null ? "" : text.toString();
            if (parse(lrc).isEmpty()) {
                status.setText("В буфере нет текста с временем вида [01:23.45]. Скопируй именно LRC.");
                return;
            }
            MwtkStore.writeText(cacheName(key, ".lrc"), lrc);
            MwtkStore.file(cacheName(key, ".none2")).delete();
            if (key.equals(currentKey)) {
                currentKey = null;
            }
            dialog.dismiss();
            MwtkUi.toast("Текст сохранён");
        });
        View searchButton = dialog.getButton(AlertDialog.BUTTON_POSITIVE);
        if (searchButton == null) {
            return;
        }
        searchButton.setOnClickListener(v -> {
            String query = field.getText().toString().trim();
            if (query.isEmpty()) {
                return;
            }
            status.setText("Ищу…");
            list.removeAllViews();
            Utilities.globalQueue.postRunnable(() -> {
                String body = null;
                try {
                    body = get("https://lrclib.net/api/search?q=" + URLEncoder.encode(query, "UTF-8"));
                } catch (Throwable ignore) {
                }
                final String result = body;
                AndroidUtilities.runOnUIThread(() -> {
                    if (!dialog.isShowing()) {
                        return;
                    }
                    if (result == null) {
                        status.setText("Не получилось подключиться к сайту текстов. Проверь интернет.");
                        return;
                    }
                    int shown = 0;
                    try {
                        JSONArray array = new JSONArray(result);
                        for (int i = 0; i < array.length() && shown < 15; i++) {
                            JSONObject item = array.optJSONObject(i);
                            if (item == null) {
                                continue;
                            }
                            final String synced = item.optString("syncedLyrics", "");
                            if (synced.isEmpty() || "null".equals(synced)) {
                                continue;
                            }
                            final double found = item.optDouble("duration", 0);
                            TextView row = new TextView(context);
                            row.setTextSize(15);
                            row.setTextColor(Theme.getColor(Theme.key_windowBackgroundWhiteBlackText));
                            row.setBackground(Theme.getSelectorDrawable(false));
                            row.setPadding(AndroidUtilities.dp(8), AndroidUtilities.dp(10), AndroidUtilities.dp(8), AndroidUtilities.dp(10));
                            row.setText(item.optString("artistName", "") + " — " + item.optString("trackName", "")
                                + "  ·  " + ((int) found / 60) + ":" + String.format(java.util.Locale.US, "%02d", (int) found % 60));
                            row.setOnClickListener(rv -> {
                                String lrc = synced;
                                if (duration > 0 && found > 0 && Math.abs(found - duration) > 3) {
                                    double ratio = found / duration;
                                    if (ratio > 0.55 && ratio < 1.7) {
                                        lrc = scale(lrc, duration / found);
                                    }
                                }
                                MwtkStore.writeText(cacheName(key, ".lrc"), lrc);
                                MwtkStore.file(cacheName(key, ".none2")).delete();
                                if (key.equals(currentKey)) {
                                    currentKey = null;
                                }
                                dialog.dismiss();
                                MwtkUi.toast("Текст сохранён");
                            });
                            list.addView(row, LayoutHelper.createLinear(LayoutHelper.MATCH_PARENT, LayoutHelper.WRAP_CONTENT));
                            shown++;
                        }
                    } catch (Throwable ignore) {
                    }
                    status.setText(shown > 0
                        ? "Выбери вариант. Твой трек длится " + (duration / 60) + ":" + String.format(java.util.Locale.US, "%02d", duration % 60) + " — ближе по длине обычно точнее."
                        : "Ничего с таймингами не нашлось. Попробуй написать иначе.");
                });
            });
        });
    }
}
