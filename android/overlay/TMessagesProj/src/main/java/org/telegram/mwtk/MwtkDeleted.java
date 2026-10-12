package org.telegram.mwtk;

import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.RectF;
import android.text.TextPaint;

import org.json.JSONArray;
import org.json.JSONObject;
import org.telegram.messenger.AndroidUtilities;
import org.telegram.messenger.MessageObject;

import java.util.ArrayList;
import java.util.Iterator;

/**
 * Deleted messages stay in the chat: deletions coming from the server are
 * not applied, the message is only marked. A journal keeps where it was.
 */
public final class MwtkDeleted {

    private static final String FILE = "deleted.json";
    private static final int LIMIT = 3000;

    private static JSONObject data;
    private static final TextPaint textPaint = new TextPaint(Paint.ANTI_ALIAS_FLAG);
    private static final Paint bgPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private static final RectF rect = new RectF();

    private MwtkDeleted() {
    }

    private static synchronized JSONObject data() {
        if (data == null) {
            data = MwtkStore.readJson(FILE);
        }
        return data;
    }

    // Channels have their own message ids, everything else shares one
    // id space per account.
    private static String key(int account, long channelId, int id) {
        return account + ":" + channelId + ":" + id;
    }

    public static boolean enabled() {
        MwtkConfig.load();
        return MwtkConfig.effects && MwtkConfig.flag(MwtkConfig.KEEP_DELETED, true);
    }

    /**
     * Called for server deletions. Returns ids that should really be
     * deleted (none while "keep in chat" is on).
     */
    public static synchronized ArrayList<Integer> filter(int account, long dialogId, ArrayList<Integer> ids) {
        if (!enabled() || ids == null || ids.isEmpty()) {
            return ids;
        }
        JSONObject d = data();
        long now = System.currentTimeMillis() / 1000;
        try {
            for (Integer id : ids) {
                JSONObject entry = new JSONObject();
                entry.put("a", account);
                entry.put("d", dialogId);
                entry.put("m", id);
                entry.put("t", now);
                d.put(key(account, dialogId == 0 ? 0 : -dialogId, id), entry);
            }
            trim(d);
        } catch (Throwable ignore) {
        }
        MwtkStore.writeJson(FILE, d);
        return new ArrayList<>();
    }

    private static void trim(JSONObject d) {
        if (d.length() <= LIMIT) {
            return;
        }
        ArrayList<String> remove = new ArrayList<>();
        Iterator<String> it = d.keys();
        int extra = d.length() - LIMIT;
        while (it.hasNext() && remove.size() < extra) {
            remove.add(it.next());
        }
        for (String key : remove) {
            d.remove(key);
        }
    }

    public static boolean isDeleted(MessageObject message) {
        if (message == null || message.messageOwner == null || !enabled()) {
            return false;
        }
        JSONObject d = data();
        if (d.length() == 0) {
            return false;
        }
        long channelId = message.messageOwner.peer_id != null ? message.messageOwner.peer_id.channel_id : 0;
        return d.has(key(message.currentAccount, channelId, message.getId()));
    }

    /** Small red "удалено" label above the bubble corner. */
    public static void drawMarker(Canvas canvas, MessageObject message, int right, int top, float alpha) {
        if (!isDeleted(message)) {
            return;
        }
        textPaint.setTextSize(AndroidUtilities.dp(11));
        textPaint.setFakeBoldText(true);
        textPaint.setColor(0xffffffff);
        String text = "удалено";
        float w = textPaint.measureText(text);
        float h = AndroidUtilities.dp(16);
        float x = right - w - AndroidUtilities.dp(14);
        float y = top - h + AndroidUtilities.dp(6);
        rect.set(x - AndroidUtilities.dp(6), y, x + w + AndroidUtilities.dp(6), y + h);
        bgPaint.setColor(0xe6d0101c);
        bgPaint.setAlpha((int) (0xe6 * alpha));
        textPaint.setAlpha((int) (255 * alpha));
        canvas.drawRoundRect(rect, h / 2, h / 2, bgPaint);
        canvas.drawText(text, x, y + h - AndroidUtilities.dp(4.5f), textPaint);
    }

    /** Journal: newest first, as (account, dialogId, messageId, time). */
    public static synchronized ArrayList<long[]> journal() {
        ArrayList<long[]> result = new ArrayList<>();
        JSONObject d = data();
        Iterator<String> it = d.keys();
        while (it.hasNext()) {
            JSONObject e = d.optJSONObject(it.next());
            if (e != null) {
                result.add(new long[] { e.optInt("a"), e.optLong("d"), e.optInt("m"), e.optLong("t") });
            }
        }
        java.util.Collections.sort(result, (a, b) -> Long.compare(b[3], a[3]));
        return result;
    }

    public static synchronized void clear() {
        data = new JSONObject();
        MwtkStore.writeJson(FILE, data);
    }

    public static JSONArray export() {
        JSONArray result = new JSONArray();
        for (long[] row : journal()) {
            JSONArray item = new JSONArray();
            for (long v : row) {
                item.put(v);
            }
            result.put(item);
        }
        return result;
    }
}
