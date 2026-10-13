package org.telegram.mwtk;

import android.content.Context;

import org.json.JSONArray;
import org.json.JSONObject;
import org.telegram.messenger.AndroidUtilities;
import org.telegram.messenger.DialogObject;
import org.telegram.messenger.MediaController;
import org.telegram.messenger.MessageObject;
import org.telegram.messenger.MessagesController;
import org.telegram.messenger.UserConfig;
import org.telegram.tgnet.ConnectionsManager;
import org.telegram.tgnet.TLObject;
import org.telegram.tgnet.TLRPC;
import org.telegram.ui.ActionBar.AlertDialog;
import org.telegram.ui.ActionBar.BaseFragment;

import java.util.ArrayList;
import java.util.Collections;

/** Playlists of music from chats. Same file layout as on the desktop. */
public final class MwtkPlaylists {

    private static final String FILE = "playlists.json";
    private static JSONObject data;

    private MwtkPlaylists() {
    }

    private static synchronized JSONObject data() {
        if (data == null) {
            data = MwtkStore.readJson(FILE);
        }
        return data;
    }

    private static String accountKey() {
        return String.valueOf(UserConfig.getInstance(UserConfig.selectedAccount).getClientUserId());
    }

    /** Playlists of the current account: [{name, tracks:[{peer,msg,title,performer,duration}]}]. */
    private static synchronized JSONArray lists() {
        JSONArray result = data().optJSONArray(accountKey());
        if (result == null) {
            result = new JSONArray();
            try {
                data().put(accountKey(), result);
            } catch (Throwable ignore) {
            }
        }
        return result;
    }

    private static synchronized void save() {
        MwtkStore.writeJson(FILE, data());
    }

    private static String[] names() {
        JSONArray lists = lists();
        String[] result = new String[lists.length()];
        for (int i = 0; i < lists.length(); i++) {
            JSONObject list = lists.optJSONObject(i);
            int count = list != null && list.optJSONArray("tracks") != null ? list.optJSONArray("tracks").length() : 0;
            result[i] = (list != null ? list.optString("name") : "?") + "  ·  " + count;
        }
        return result;
    }

    public static void showAddBox(BaseFragment fragment, MessageObject message) {
        Context context = fragment.getParentActivity();
        if (context == null || message == null) {
            return;
        }
        String[] existing = names();
        String[] items = new String[existing.length + 1];
        System.arraycopy(existing, 0, items, 0, existing.length);
        items[existing.length] = "+ Новый плейлист";
        AlertDialog.Builder builder = new AlertDialog.Builder(context);
        builder.setTitle("В плейлист MWTK");
        builder.setItems(items, (dialog, which) -> {
            if (which == existing.length) {
                MwtkUi.input(fragment, "Новый плейлист", null, "", "Название", false, false, null, name -> {
                    if (name.trim().isEmpty()) {
                        return;
                    }
                    int index = create(name.trim());
                    add(index, message);
                });
            } else {
                add(which, message);
            }
        });
        fragment.showDialog(builder.create());
    }

    public static synchronized int create(String name) {
        try {
            JSONObject list = new JSONObject();
            list.put("name", name);
            list.put("tracks", new JSONArray());
            lists().put(list);
            save();
        } catch (Throwable ignore) {
        }
        return lists().length() - 1;
    }

    public static synchronized void add(int index, MessageObject message) {
        JSONObject list = lists().optJSONObject(index);
        if (list == null) {
            return;
        }
        try {
            JSONArray tracks = list.optJSONArray("tracks");
            if (tracks == null) {
                tracks = new JSONArray();
                list.put("tracks", tracks);
            }
            long peer = message.getDialogId();
            int msg = message.getId();
            for (int i = 0; i < tracks.length(); i++) {
                JSONObject t = tracks.optJSONObject(i);
                if (t != null && t.optLong("peer") == peer && t.optLong("msg") == msg) {
                    MwtkUi.toast("Уже в плейлисте");
                    return;
                }
            }
            JSONObject track = new JSONObject();
            track.put("peer", peer);
            track.put("msg", msg);
            track.put("title", message.getMusicTitle());
            track.put("performer", message.getMusicAuthor());
            track.put("duration", (int) message.getDuration());
            tracks.put(track);
            save();
            MwtkUi.toast("Добавлено в «" + list.optString("name") + "»");
        } catch (Throwable ignore) {
        }
    }

    public static void showListBox(BaseFragment fragment) {
        Context context = fragment.getParentActivity();
        if (context == null) {
            return;
        }
        String[] existing = names();
        if (existing.length == 0) {
            MwtkUi.toast("Плейлистов пока нет. Долгое нажатие на музыку → «В плейлист MWTK»");
            return;
        }
        AlertDialog.Builder builder = new AlertDialog.Builder(context);
        builder.setTitle("Мои плейлисты");
        builder.setItems(existing, (dialog, which) -> showPlaylist(fragment, which));
        fragment.showDialog(builder.create());
    }

    private static void showPlaylist(BaseFragment fragment, int index) {
        Context context = fragment.getParentActivity();
        JSONObject list = lists().optJSONObject(index);
        if (context == null || list == null) {
            return;
        }
        JSONArray tracks = list.optJSONArray("tracks");
        int count = tracks != null ? tracks.length() : 0;
        String[] items = new String[count + 3];
        items[0] = "▶ Играть по порядку";
        items[1] = "🔀 Вперемешку";
        for (int i = 0; i < count; i++) {
            JSONObject t = tracks.optJSONObject(i);
            String performer = t.optString("performer");
            items[i + 2] = (performer.isEmpty() ? "" : performer + " — ") + t.optString("title");
        }
        items[count + 2] = "🗑 Удалить плейлист";
        AlertDialog.Builder builder = new AlertDialog.Builder(context);
        builder.setTitle(list.optString("name"));
        builder.setItems(items, (dialog, which) -> {
            if (which == 0 || which == 1) {
                play(index, 0, which == 1);
            } else if (which == count + 2) {
                MwtkUi.confirm(fragment, "Удалить плейлист?", list.optString("name"), "Удалить", () -> {
                    synchronized (MwtkPlaylists.class) {
                        lists().remove(index);
                        save();
                    }
                });
            } else {
                play(index, which - 2, false);
            }
        });
        fragment.showDialog(builder.create());
    }

    // ------------------------------------------------------------ public api

    public interface Loaded {
        /** Messages in track order; null where the message is gone. */
        void run(ArrayList<MessageObject> messages);
    }

    private static final java.util.HashMap<String, MessageObject> cache = new java.util.HashMap<>();

    public static synchronized int count() {
        return lists().length();
    }

    public static synchronized String name(int index) {
        JSONObject list = lists().optJSONObject(index);
        return list != null ? list.optString("name") : "";
    }

    /** Copy of the tracks: [{peer, msg, title, performer, duration}]. */
    public static synchronized JSONArray tracks(int index) {
        JSONObject list = lists().optJSONObject(index);
        JSONArray tracks = list != null ? list.optJSONArray("tracks") : null;
        try {
            return tracks != null ? new JSONArray(tracks.toString()) : new JSONArray();
        } catch (Throwable e) {
            return new JSONArray();
        }
    }

    public static synchronized void rename(int index, String name) {
        JSONObject list = lists().optJSONObject(index);
        if (list != null && !name.trim().isEmpty()) {
            try {
                list.put("name", name.trim());
            } catch (Throwable ignore) {
            }
            save();
        }
    }

    public static synchronized void remove(int index) {
        if (index >= 0 && index < lists().length()) {
            lists().remove(index);
            save();
        }
    }

    public static synchronized void removeTrack(int index, int track) {
        JSONObject list = lists().optJSONObject(index);
        JSONArray tracks = list != null ? list.optJSONArray("tracks") : null;
        if (tracks != null && track >= 0 && track < tracks.length()) {
            tracks.remove(track);
            save();
        }
    }

    public static MessageObject cached(long peer, long msg) {
        synchronized (cache) {
            return cache.get(peer + ":" + msg);
        }
    }

    /** Loads the messages of a playlist (from the cache or the server). */
    public static void load(int index, Loaded done) {
        JSONArray tracks = tracks(index);
        final ArrayList<long[]> order = new ArrayList<>();
        for (int i = 0; i < tracks.length(); i++) {
            JSONObject t = tracks.optJSONObject(i);
            if (t != null) {
                order.add(new long[] { t.optLong("peer"), t.optLong("msg") });
            }
        }
        loadOrder(order, done);
    }

    private static void loadOrder(ArrayList<long[]> order, Loaded done) {
        final int account = UserConfig.selectedAccount;
        java.util.HashMap<Long, ArrayList<Integer>> byChannel = new java.util.HashMap<>();
        for (long[] track : order) {
            if (cached(track[0], track[1]) != null) {
                continue;
            }
            long channel = DialogObject.isChatDialog(track[0])
                && org.telegram.messenger.ChatObject.isChannel(MessagesController.getInstance(account).getChat(-track[0]))
                ? -track[0] : 0;
            ArrayList<Integer> ids = byChannel.get(channel);
            if (ids == null) {
                ids = new ArrayList<>();
                byChannel.put(channel, ids);
            }
            ids.add((int) track[1]);
        }
        final Runnable finish = () -> {
            ArrayList<MessageObject> result = new ArrayList<>();
            for (long[] track : order) {
                result.add(cached(track[0], track[1]));
            }
            done.run(result);
        };
        if (byChannel.isEmpty()) {
            finish.run();
            return;
        }
        final int[] pending = { byChannel.size() };
        for (java.util.Map.Entry<Long, ArrayList<Integer>> entry : byChannel.entrySet()) {
            TLObject request;
            final long channel = entry.getKey();
            if (channel != 0) {
                TLRPC.TL_channels_getMessages req = new TLRPC.TL_channels_getMessages();
                req.channel = MessagesController.getInstance(account).getInputChannel(channel);
                req.id = entry.getValue();
                request = req;
            } else {
                TLRPC.TL_messages_getMessages req = new TLRPC.TL_messages_getMessages();
                req.id = entry.getValue();
                request = req;
            }
            ConnectionsManager.getInstance(account).sendRequest(request, (response, error) -> {
                ArrayList<MessageObject> objects = new ArrayList<>();
                if (response instanceof TLRPC.messages_Messages) {
                    TLRPC.messages_Messages res = (TLRPC.messages_Messages) response;
                    MessagesController.getInstance(account).putUsers(res.users, false);
                    MessagesController.getInstance(account).putChats(res.chats, false);
                    for (TLRPC.Message message : res.messages) {
                        if (message instanceof TLRPC.TL_messageEmpty) {
                            continue;
                        }
                        objects.add(new MessageObject(account, message, false, true));
                    }
                }
                AndroidUtilities.runOnUIThread(() -> {
                    synchronized (cache) {
                        for (MessageObject object : objects) {
                            cache.put(object.getDialogId() + ":" + object.getId(), object);
                        }
                    }
                    if (--pending[0] == 0) {
                        finish.run();
                    }
                });
            });
        }
    }

    /** Starts a ready list: plays {@code first} and continues through the list. */
    public static void playMessages(ArrayList<MessageObject> list, MessageObject first) {
        ArrayList<MessageObject> playlist = new ArrayList<>();
        for (MessageObject object : list) {
            if (object != null && object.isMusic()) {
                playlist.add(object);
            }
        }
        if (playlist.isEmpty()) {
            return;
        }
        if (first == null || !playlist.contains(first)) {
            first = playlist.get(0);
        }
        Collections.reverse(playlist);
        MediaController.getInstance().setPlaylist(playlist, first, 0);
    }

    /** Loads the playlist messages from the server and starts playback. */
    public static void play(int index, int start, boolean shuffle) {
        JSONObject list = lists().optJSONObject(index);
        if (list == null) {
            return;
        }
        JSONArray tracks = list.optJSONArray("tracks");
        if (tracks == null || tracks.length() == 0) {
            return;
        }
        final int account = UserConfig.selectedAccount;
        final ArrayList<long[]> order = new ArrayList<>();
        for (int i = 0; i < tracks.length(); i++) {
            JSONObject t = tracks.optJSONObject(i);
            order.add(new long[] { t.optLong("peer"), t.optLong("msg") });
        }
        // Group ids by channel; everything else goes in one request.
        java.util.HashMap<Long, ArrayList<Integer>> byChannel = new java.util.HashMap<>();
        for (long[] track : order) {
            long channel = DialogObject.isChatDialog(track[0])
                && org.telegram.messenger.ChatObject.isChannel(MessagesController.getInstance(account).getChat(-track[0]))
                ? -track[0] : 0;
            ArrayList<Integer> ids = byChannel.get(channel);
            if (ids == null) {
                ids = new ArrayList<>();
                byChannel.put(channel, ids);
            }
            ids.add((int) track[1]);
        }
        final java.util.HashMap<String, MessageObject> loaded = new java.util.HashMap<>();
        final int[] pending = { byChannel.size() };
        for (java.util.Map.Entry<Long, ArrayList<Integer>> entry : byChannel.entrySet()) {
            TLObject request;
            final long channel = entry.getKey();
            if (channel != 0) {
                TLRPC.TL_channels_getMessages req = new TLRPC.TL_channels_getMessages();
                req.channel = MessagesController.getInstance(account).getInputChannel(channel);
                req.id = entry.getValue();
                request = req;
            } else {
                TLRPC.TL_messages_getMessages req = new TLRPC.TL_messages_getMessages();
                req.id = entry.getValue();
                request = req;
            }
            ConnectionsManager.getInstance(account).sendRequest(request, (response, error) -> {
                ArrayList<MessageObject> objects = new ArrayList<>();
                if (response instanceof TLRPC.messages_Messages) {
                    TLRPC.messages_Messages res = (TLRPC.messages_Messages) response;
                    MessagesController.getInstance(account).putUsers(res.users, false);
                    MessagesController.getInstance(account).putChats(res.chats, false);
                    for (TLRPC.Message message : res.messages) {
                        if (message instanceof TLRPC.TL_messageEmpty) {
                            continue;
                        }
                        objects.add(new MessageObject(account, message, false, true));
                    }
                }
                AndroidUtilities.runOnUIThread(() -> {
                    for (MessageObject object : objects) {
                        loaded.put(object.getDialogId() + ":" + object.getId(), object);
                    }
                    if (--pending[0] == 0) {
                        start(order, loaded, start, shuffle);
                    }
                });
            });
        }
    }

    private static void start(ArrayList<long[]> order, java.util.HashMap<String, MessageObject> loaded, int start, boolean shuffle) {
        ArrayList<MessageObject> playlist = new ArrayList<>();
        MessageObject first = null;
        for (int i = 0; i < order.size(); i++) {
            MessageObject object = loaded.get(order.get(i)[0] + ":" + order.get(i)[1]);
            if (object != null && object.isMusic()) {
                playlist.add(object);
                if (i == start) {
                    first = object;
                }
            }
        }
        if (playlist.isEmpty()) {
            MwtkUi.toast("Треки не найдены — возможно, их удалили из чатов");
            return;
        }
        if (shuffle) {
            Collections.shuffle(playlist);
            first = playlist.get(0);
        }
        if (first == null) {
            first = playlist.get(0);
        }
        // Telegram plays the list from the end towards the start.
        Collections.reverse(playlist);
        MediaController.getInstance().setPlaylist(playlist, first, 0);
    }

    public static synchronized void replaceAll(JSONArray lists) {
        try {
            data().put(accountKey(), lists);
        } catch (Throwable ignore) {
        }
        save();
    }

    public static JSONObject export() {
        return data();
    }
}
