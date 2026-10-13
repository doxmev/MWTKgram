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
import org.telegram.messenger.MediaController;
import org.telegram.messenger.MessageObject;
import org.telegram.messenger.Utilities;
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

        LyricsView(Context context) {
            super(context);
            setWillNotDraw(false);
            setClickable(false);
            hintPaint.setTextSize(AndroidUtilities.dp(13));
            hintPaint.setColor(0x99ffffff);
            hintPaint.setTextAlign(Paint.Align.CENTER);
            backdrop.setCallback(this);
        }

        @Override
        protected boolean verifyDrawable(android.graphics.drawable.Drawable who) {
            return who == backdrop || super.verifyDrawable(who);
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
            String[] texts = line.text.split(" ");
            if (line.text.trim().isEmpty() || texts.length == 0) {
                return result;
            }
            int layout = MwtkConfig.num(MwtkConfig.LYRICS_LAYOUT, 0);
            int style = MwtkConfig.num(MwtkConfig.LYRICS_STYLE, 0);
            float sizeK = MwtkConfig.num(MwtkConfig.LYRICS_SIZE, 100) / 100f;
            int accent = MwtkConfig.accent();
            Random r = new Random(currentKey == null ? index : currentKey.hashCode() * 31L + index);
            // Tablet mode: the lyrics use the free right side; "hide chats" keeps the whole screen.
            android.graphics.Rect area = hideMode ? null : MwtkTablet.freeArea(this);
            float areaLeft = area != null ? area.left : 0;
            float areaTop = area != null ? area.top : 0;
            float areaW = area != null ? area.width() : getWidth();
            float areaH = area != null ? area.height() : getHeight();
            boolean wide = areaW > AndroidUtilities.dp(700) || (area != null && areaW > AndroidUtilities.dp(420));
            float base = Math.max(AndroidUtilities.dp(22), Math.min(areaW / (wide ? 14f : 9f), AndroidUtilities.dp(wide ? 56 : 40))) * sizeK;
            int total = 0;
            for (String t : texts) {
                total += t.length() + 1;
            }
            double span = Math.max(line.end - line.at, 400) * 0.8;
            double at = line.at;
            for (int i = 0; i < texts.length; i++) {
                Word w = new Word();
                w.text = texts[i];
                if (line.words != null && line.words.length == texts.length) {
                    w.start = line.words[i];
                    w.end = i + 1 < texts.length ? line.words[i + 1] : line.end;
                } else {
                    w.start = (long) at;
                    at += span * (texts[i].length() + 1) / total;
                    w.end = (long) at;
                }
                w.out = line.end + (layout == 2 ? 0 : (long) (350f * i / texts.length));
                w.phase = r.nextFloat() * 6.28f;
                double angle = r.nextFloat() * 6.28;
                w.fromX = (float) Math.cos(angle) * base * 3;
                w.fromY = (float) Math.sin(angle) * base * 2;
                result.add(w);
            }
            float width = areaW;
            float height = areaH;
            float cx = areaLeft + (layout == 2 ? width / 2 : width * (0.5f + (r.nextFloat() - 0.5f) * 0.2f));
            float cy = areaTop + (layout == 2 ? height * 0.42f : height * (0.38f + (r.nextFloat() - 0.5f) * 0.16f));
            // On a narrow phone screen every layout wraps into rows; a wide screen fits longer rows.
            int perRow = layout == 1 ? 1 : (wide ? 6 : 3);
            float rowH = base * 1.3f;
            ArrayList<ArrayList<Word>> rows = new ArrayList<>();
            ArrayList<Word> row = new ArrayList<>();
            float rowWidth = 0;
            TextPaint measure = new TextPaint(Paint.ANTI_ALIAS_FLAG);
            measure.setTypeface(Typeface.create(Typeface.DEFAULT, Typeface.BOLD));
            for (Word w : result) {
                float size = layout == 2 ? base : base * (0.8f + r.nextFloat() * 0.45f);
                measure.setTextSize(size);
                float ww = measure.measureText(w.text);
                int want = layout == 0 ? 1 + r.nextInt(perRow) : perRow;
                if (!row.isEmpty() && (row.size() >= want || rowWidth + ww > width * 0.86f)) {
                    rows.add(row);
                    row = new ArrayList<>();
                    rowWidth = 0;
                }
                w.dim = render(w.text, size, false, style, accent);
                w.lit = render(w.text, size, true, style, accent);
                row.add(w);
                rowWidth += ww + base * 0.4f;
            }
            if (!row.isEmpty()) {
                rows.add(row);
            }
            for (int ri = 0; ri < rows.size(); ri++) {
                ArrayList<Word> words = rows.get(ri);
                float rowTotal = 0;
                for (Word w : words) {
                    rowTotal += w.dim.getWidth() - w.dim.getHeight() * 0.5f;
                }
                float x = cx - rowTotal / 2 + (layout == 0 ? (r.nextFloat() - 0.5f) * base : 0);
                if (layout == 1) {
                    x = cx - width * (wide ? 0.3f : 0.25f) + ri * base * (wide ? 1.1f : 0.6f);
                }
                for (Word w : words) {
                    float ww = w.dim.getWidth() - w.dim.getHeight() * 0.5f;
                    w.x = x + ww / 2;
                    w.y = cy + (ri - (rows.size() - 1) / 2f) * rowH + (layout == 0 ? (r.nextFloat() - 0.5f) * base * 0.5f : 0);
                    w.rotation = layout == 0 ? (r.nextFloat() - 0.5f) * 12f : layout == 1 ? -3f : 0;
                    x += ww;
                }
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

        @Override
        protected void onDraw(Canvas canvas) {
            long frame = System.currentTimeMillis();
            float dt = lastFrame == 0 ? 0 : Math.min((frame - lastFrame) / 1000f, 0.1f);
            lastFrame = frame;
            if (hideMode) {
                backdrop.setBounds(0, 0, getWidth(), getHeight());
                backdrop.draw(canvas);
                canvas.drawText("Нажми, чтобы вернуть чаты", getWidth() / 2f, getHeight() - AndroidUtilities.dp(40), hintPaint);
            }
            long now = position();
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
                float seconds = frame / 1000f;
                for (ArrayList<Word> words : layouts) {
                    for (Word w : words) {
                        if (w.gone || w.dim == null || now < w.start - 450) {
                            continue;
                        }
                        float k = Math.min(1f, Math.max(0f, (now - (w.start - 450)) / 550f));
                        float ease = 1f - (1f - k) * (1f - k) * (1f - k);
                        float onScreen = (now - w.start) / 1000f;
                        float x = w.x + w.fromX * (1 - ease)
                            + (float) Math.sin(seconds * 0.9f + w.phase) * AndroidUtilities.dp(4) * motion
                            + onScreen * AndroidUtilities.dp(2) * motion * (float) Math.cos(w.phase);
                        float y = w.y + w.fromY * (1 - ease)
                            + (float) Math.cos(seconds * 1.1f + w.phase) * AndroidUtilities.dp(3) * motion
                            - onScreen * AndroidUtilities.dp(1.5f) * motion;
                        float alpha = ease;
                        float scale = 0.3f + 0.7f * ease;
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
}
