package org.telegram.mwtk;

import org.json.JSONArray;
import org.json.JSONObject;
import org.telegram.messenger.MediaController;
import org.telegram.messenger.MessageObject;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.Locale;
import java.util.Map;
import java.util.Random;

/**
 * What you listen to, what you skip and what you dislike. Feeds «Мой микс»
 * the way VK / Spotify mixes work: by your whole listening, not only by the
 * song that plays now.
 */
public final class MwtkTaste {

    private static final String FILE = "mwtk_taste.json";
    private static final int MAX_HISTORY = 600;
    private static final int SKIP_SECONDS = 30;
    private static final double HALF_LIFE_DAYS = 14;

    private static JSONObject root;

    private static String currentKey;
    private static String currentPerformer;
    private static String currentTitle;
    private static int listened;
    private static int duration;

    private MwtkTaste() {
    }

    private static String key(String text) {
        return text == null ? "" : text.trim().toLowerCase(Locale.ROOT);
    }

    private static synchronized JSONObject root() {
        if (root == null) {
            root = MwtkStore.readJson(FILE);
            if (root == null) {
                root = new JSONObject();
            }
        }
        return root;
    }

    private static JSONArray array(String name) {
        JSONArray array = root().optJSONArray(name);
        if (array == null) {
            array = new JSONArray();
            try {
                root().put(name, array);
            } catch (Throwable ignore) {
            }
        }
        return array;
    }

    private static void save() {
        MwtkStore.writeJson(FILE, root());
    }

    /** Called often from the UI thread: follows the playing song. */
    public static void tick() {
        MessageObject playing = MediaController.getInstance().getPlayingMessageObject();
        String key = playing != null && playing.isMusic()
            ? playing.getDialogId() + "_" + playing.getId()
            : null;
        if (key == null ? currentKey != null : !key.equals(currentKey)) {
            finish();
            if (key != null) {
                currentKey = key;
                currentPerformer = playing.getMusicAuthor();
                currentTitle = playing.getMusicTitle();
                listened = 0;
                duration = (int) playing.getDuration();
            }
        }
        if (key != null) {
            listened = Math.max(listened, (int) (MediaController.getInstance().getCurrentPosition() / 1000));
        }
    }

    private static void finish() {
        if (currentKey == null) {
            return;
        }
        String performer = currentPerformer;
        currentKey = null;
        if (performer == null || performer.trim().isEmpty()) {
            return;
        }
        boolean almostWhole = duration > 0 && listened * 10 >= duration * 6;
        try {
            JSONObject play = new JSONObject();
            play.put("p", performer);
            play.put("t", currentTitle == null ? "" : currentTitle);
            play.put("w", System.currentTimeMillis() / 1000);
            play.put("s", listened < SKIP_SECONDS && !almostWhole);
            JSONArray history = array("history");
            history.put(play);
            while (history.length() > MAX_HISTORY) {
                history.remove(0);
            }
            save();
        } catch (Throwable ignore) {
        }
    }

    public static void dislike(String performer, String title) {
        if (isDisliked(performer, title)) {
            return;
        }
        try {
            JSONObject item = new JSONObject();
            item.put("p", performer == null ? "" : performer.trim());
            item.put("t", title == null ? "" : title.trim());
            array("disliked").put(item);
            save();
        } catch (Throwable ignore) {
        }
    }

    public static boolean isDisliked(String performer, String title) {
        String p = key(performer);
        String t = key(title);
        JSONArray disliked = array("disliked");
        for (int i = 0; i < disliked.length(); i++) {
            JSONObject item = disliked.optJSONObject(i);
            if (item != null && key(item.optString("p")).equals(p) && key(item.optString("t")).equals(t)) {
                return true;
            }
        }
        return false;
    }

    /** Two dislikes for one artist: the artist is not offered again. */
    public static boolean artistBanned(String performer) {
        String p = key(performer);
        if (p.isEmpty()) {
            return false;
        }
        int count = 0;
        JSONArray disliked = array("disliked");
        for (int i = 0; i < disliked.length(); i++) {
            JSONObject item = disliked.optJSONObject(i);
            if (item != null && key(item.optString("p")).equals(p)) {
                count++;
            }
        }
        return count >= 2;
    }

    public static boolean playedRecently(String performer, String title, int days) {
        String p = key(performer);
        String t = key(title);
        long since = System.currentTimeMillis() / 1000 - days * 86400L;
        JSONArray history = array("history");
        for (int i = 0; i < history.length(); i++) {
            JSONObject play = history.optJSONObject(i);
            if (play != null && play.optLong("w") >= since
                && key(play.optString("p")).equals(p) && key(play.optString("t")).equals(t)) {
                return true;
            }
        }
        return false;
    }

    /**
     * Artists to build a mix around, picked by weight: more listening — more
     * weight, skips and dislikes lower it. {@code extra} artists (playlists,
     * the song playing now) get a starting weight.
     */
    public static ArrayList<String> seedArtists(int count, ArrayList<String> extra) {
        HashMap<String, Double> weights = new HashMap<>();
        HashMap<String, String> names = new HashMap<>();
        long now = System.currentTimeMillis() / 1000;
        JSONArray history = array("history");
        for (int i = 0; i < history.length(); i++) {
            JSONObject play = history.optJSONObject(i);
            if (play == null) {
                continue;
            }
            double days = Math.max(now - play.optLong("w"), 0) / 86400.0;
            double fresh = Math.pow(0.5, days / HALF_LIFE_DAYS);
            add(weights, names, play.optString("p"), (play.optBoolean("s") ? -0.6 : 1.0) * fresh);
        }
        for (String name : extra) {
            add(weights, names, name, 1.2);
        }
        JSONArray disliked = array("disliked");
        for (int i = 0; i < disliked.length(); i++) {
            JSONObject item = disliked.optJSONObject(i);
            if (item != null) {
                add(weights, names, item.optString("p"), -1.5);
            }
        }
        ArrayList<String> poolNames = new ArrayList<>();
        ArrayList<Double> poolWeights = new ArrayList<>();
        for (Map.Entry<String, Double> entry : weights.entrySet()) {
            String name = names.get(entry.getKey());
            if (entry.getValue() > 0.05 && !artistBanned(name)) {
                poolNames.add(name);
                poolWeights.add(entry.getValue());
            }
        }
        ArrayList<String> result = new ArrayList<>();
        Random random = new Random();
        while (!poolNames.isEmpty() && result.size() < count) {
            double total = 0;
            for (double w : poolWeights) {
                total += w;
            }
            double pick = random.nextDouble() * total;
            int index = 0;
            for (; index + 1 < poolNames.size(); index++) {
                pick -= poolWeights.get(index);
                if (pick <= 0) {
                    break;
                }
            }
            result.add(poolNames.remove(index));
            poolWeights.remove(index);
        }
        return result;
    }

    private static void add(HashMap<String, Double> weights, HashMap<String, String> names, String name, double weight) {
        String k = key(name);
        if (k.isEmpty() || "null".equals(k)) {
            return;
        }
        Double was = weights.get(k);
        weights.put(k, (was == null ? 0 : was) + weight);
        if (!names.containsKey(k)) {
            names.put(k, name.trim());
        }
    }
}
