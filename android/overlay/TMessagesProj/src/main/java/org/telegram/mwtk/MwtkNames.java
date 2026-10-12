package org.telegram.mwtk;

import android.text.TextUtils;

import org.json.JSONObject;
import org.telegram.messenger.UserConfig;
import org.telegram.tgnet.TLRPC;

import java.util.concurrent.ConcurrentHashMap;

/** Local "for me" names of chats, stored on this phone only. */
public final class MwtkNames {

    private static final String FILE = "names.json";
    private static volatile ConcurrentHashMap<String, String> names;

    private MwtkNames() {
    }

    private static ConcurrentHashMap<String, String> names() {
        if (names == null) {
            synchronized (MwtkNames.class) {
                if (names == null) {
                    ConcurrentHashMap<String, String> result = new ConcurrentHashMap<>();
                    JSONObject object = MwtkStore.readJson(FILE);
                    java.util.Iterator<String> it = object.keys();
                    while (it.hasNext()) {
                        String key = it.next();
                        String value = object.optString(key, "").trim();
                        if (!value.isEmpty()) {
                            result.put(key, value);
                        }
                    }
                    names = result;
                }
            }
        }
        return names;
    }

    // Same keys as the desktop sync: "<my user id>:<peer id>".
    private static String key(long peerId) {
        long self = UserConfig.getInstance(UserConfig.selectedAccount).getClientUserId();
        return self + ":" + peerId;
    }

    public static String get(long peerId) {
        if (names().isEmpty()) {
            return null;
        }
        return names().get(key(peerId));
    }

    public static void set(long peerId, String name) {
        String value = name == null ? "" : name.trim();
        if (value.isEmpty()) {
            names().remove(key(peerId));
        } else {
            names().put(key(peerId), value);
        }
        save();
    }

    private static void save() {
        JSONObject object = new JSONObject();
        try {
            for (java.util.Map.Entry<String, String> e : names().entrySet()) {
                object.put(e.getKey(), e.getValue());
            }
        } catch (Throwable ignore) {
        }
        MwtkStore.writeJson(FILE, object);
    }

    /** Returns the local name of a user, or null. */
    public static String user(TLRPC.User user) {
        if (user == null || names().isEmpty()) {
            return null;
        }
        return get(user.id);
    }

    /** Title of a chat with its local name applied. */
    public static String chat(TLRPC.Chat chat) {
        if (chat == null) {
            return "";
        }
        if (!names().isEmpty()) {
            // Desktop serializes chats as negative peer ids too.
            String local = get(-chat.id);
            if (!TextUtils.isEmpty(local)) {
                return local;
            }
        }
        return chat.title;
    }

    public static JSONObject export() {
        JSONObject object = new JSONObject();
        try {
            for (java.util.Map.Entry<String, String> e : names().entrySet()) {
                object.put(e.getKey(), e.getValue());
            }
        } catch (Throwable ignore) {
        }
        return object;
    }

    public static void importAll(JSONObject object) {
        java.util.Iterator<String> it = object.keys();
        while (it.hasNext()) {
            String key = it.next();
            String value = object.optString(key, "").trim();
            if (!value.isEmpty()) {
                names().put(key, value);
            }
        }
        save();
    }
}
