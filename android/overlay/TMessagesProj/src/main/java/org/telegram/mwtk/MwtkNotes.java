package org.telegram.mwtk;

import org.json.JSONObject;
import org.telegram.messenger.UserConfig;

/** Private notes about chats, stored on this phone only. */
public final class MwtkNotes {

    private static final String FILE = "notes.json";
    private static JSONObject data;

    private MwtkNotes() {
    }

    private static synchronized JSONObject data() {
        if (data == null) {
            data = MwtkStore.readJson(FILE);
        }
        return data;
    }

    private static String key(long peerId) {
        long self = UserConfig.getInstance(UserConfig.selectedAccount).getClientUserId();
        return self + ":" + peerId;
    }

    public static synchronized String get(long peerId) {
        return data().optString(key(peerId), "");
    }

    public static synchronized void set(long peerId, String text) {
        try {
            String value = text == null ? "" : text.trim();
            if (value.isEmpty()) {
                data().remove(key(peerId));
            } else {
                data().put(key(peerId), value);
            }
        } catch (Throwable ignore) {
        }
        MwtkStore.writeJson(FILE, data());
    }
}
