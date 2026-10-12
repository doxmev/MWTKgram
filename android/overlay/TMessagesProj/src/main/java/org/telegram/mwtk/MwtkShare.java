package org.telegram.mwtk;

import android.app.Activity;
import android.content.Intent;
import android.net.Uri;

import androidx.core.content.FileProvider;

import org.json.JSONArray;
import org.json.JSONObject;
import org.telegram.messenger.AndroidUtilities;
import org.telegram.messenger.ApplicationLoader;
import org.telegram.messenger.ChatObject;
import org.telegram.messenger.DialogObject;
import org.telegram.messenger.MessagesController;
import org.telegram.messenger.UserConfig;
import org.telegram.ui.ActionBar.AlertDialog;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.security.SecureRandom;
import java.util.Iterator;

import javax.crypto.Cipher;
import javax.crypto.spec.IvParameterSpec;
import javax.crypto.spec.SecretKeySpec;

/**
 * MWTK theme (.mwtktheme) and sync (.mwtk) files, compatible with the
 * desktop version: clicking one in a chat applies it.
 */
public final class MwtkShare {

    private static final byte[] THEME_MAGIC = "MWTKTHEME".getBytes(StandardCharsets.US_ASCII);
    private static final byte[] SYNC_MAGIC = "MWTKSYNC1".getBytes(StandardCharsets.US_ASCII);
    private static final long RESERVED = 0x80L << 48;
    private static final long BARE_MASK = 0xFFFFFFFFFFFFL;

    // Desktop option id <-> phone key (bool or int).
    private static final String[][] OPTIONS = {
        { "mwtk-effects", MwtkConfig.EFFECTS, "b" },
        { "mwtk-watermark", MwtkConfig.LABELS, "b" },
        { "mwtk-objects", MwtkConfig.FIGURES, "b" },
        { "mwtk-avatar-glow", MwtkConfig.GLOW, "b" },
        { "mwtk-online-stripe", MwtkConfig.STRIPE, "b" },
        { "mwtk-online-card-glow", MwtkConfig.CARD_GLOW, "b" },
        { "mwtk-online-dot", MwtkConfig.DOT, "b" },
        { "mwtk-online-pulse", MwtkConfig.PULSE, "b" },
        { "mwtk-online-tile", MwtkConfig.TILE, "b" },
        { "mwtk-app-icon", MwtkConfig.APP_ICON, "i" },
        { "mwtk-notify-style", MwtkConfig.NOTIFY_STYLE, "i" },
        { "mwtk-seasons", MwtkConfig.SEASONS, "b" },
        { "mwtk-music-pulse", MwtkConfig.MUSIC_PULSE, "b" },
        { "mwtk-lyrics", MwtkConfig.LYRICS, "b" },
        { "mwtk-lyrics-online", MwtkConfig.LYRICS_ONLINE, "b" },
        { "mwtk-lyrics-layout", MwtkConfig.LYRICS_LAYOUT, "i" },
        { "mwtk-lyrics-vanish", MwtkConfig.LYRICS_VANISH, "i" },
        { "mwtk-lyrics-style", MwtkConfig.LYRICS_STYLE, "i" },
        { "mwtk-lyrics-motion", MwtkConfig.LYRICS_MOTION, "i" },
        { "mwtk-lyrics-size", MwtkConfig.LYRICS_SIZE, "i" },
        { "mwtk-flyby", MwtkConfig.FLYBY, "b" },
        { "mwtk-holo-profile", MwtkConfig.HOLO, "b" },
        { "mwtk-ui-sounds", MwtkConfig.SOUNDS, "b" },
        { "mwtk-message-appear", MwtkConfig.APPEAR, "b" },
        { "mwtk-send-effect", MwtkConfig.SEND_SPARKS, "b" },
        { "mwtk-night-events", MwtkConfig.NIGHT, "b" },
        { "mwtk-keep-deleted-in-chat", MwtkConfig.KEEP_DELETED, "b" },
        { "mwtk-quick-phrases", MwtkConfig.PHRASES, "b" },
        { "mwtk-custom-color", MwtkConfig.CUSTOM_COLOR, "i" },
    };

    private MwtkShare() {
    }

    // ------------------------------------------------------------ ids

    /** Desktop serialized peer id -> phone dialog id. */
    public static long dialogFromDesktop(long serialized) {
        long value = serialized & ~RESERVED;
        long type = (value >>> 48) & 0x7F;
        long bare = value & BARE_MASK;
        return type == 0 ? bare : -bare;
    }

    /** Phone dialog id -> desktop serialized peer id. */
    public static long desktopFromDialog(int account, long dialogId) {
        if (DialogObject.isUserDialog(dialogId)) {
            return dialogId | RESERVED;
        }
        long bare = -dialogId;
        boolean channel = ChatObject.isChannel(MessagesController.getInstance(account).getChat(bare));
        return bare | ((channel ? 2L : 1L) << 48) | RESERVED;
    }

    private static long self() {
        return UserConfig.getInstance(UserConfig.selectedAccount).getClientUserId();
    }

    // ------------------------------------------------------------ crypto

    private static byte[] key() throws Exception {
        return MessageDigest.getInstance("SHA-256").digest(
            ("mwtk-sync:" + self()).getBytes(StandardCharsets.UTF_8));
    }

    private static byte[] crypt(byte[] data, byte[] iv) throws Exception {
        Cipher cipher = Cipher.getInstance("AES/CTR/NoPadding");
        cipher.init(Cipher.ENCRYPT_MODE, new SecretKeySpec(key(), "AES"), new IvParameterSpec(iv));
        return cipher.doFinal(data);
    }

    // ------------------------------------------------------------ options

    private static JSONObject collectOptions() {
        JSONObject result = new JSONObject();
        try {
            for (String[] o : OPTIONS) {
                if ("b".equals(o[2])) {
                    result.put(o[0], legacyBool(o[1]));
                } else {
                    result.put(o[0], legacyInt(o[1]));
                }
            }
        } catch (Throwable ignore) {
        }
        return result;
    }

    private static boolean legacyBool(String key) {
        switch (key) {
            case MwtkConfig.EFFECTS: case MwtkConfig.LABELS: case MwtkConfig.FIGURES:
            case MwtkConfig.GLOW: case MwtkConfig.STRIPE: case MwtkConfig.CARD_GLOW:
            case MwtkConfig.DOT: case MwtkConfig.PULSE: case MwtkConfig.TILE:
                return MwtkConfig.getBool(key);
        }
        return MwtkConfig.flag(key, true);
    }

    private static int legacyInt(String key) {
        if (MwtkConfig.NOTIFY_STYLE.equals(key)) {
            return MwtkConfig.num(key, 1);
        } else if (MwtkConfig.LYRICS_MOTION.equals(key)) {
            return MwtkConfig.num(key, 60);
        } else if (MwtkConfig.LYRICS_SIZE.equals(key)) {
            return MwtkConfig.num(key, 100);
        }
        return MwtkConfig.num(key, 0);
    }

    private static void applyOptions(JSONObject options) {
        if (options == null) {
            return;
        }
        for (String[] o : OPTIONS) {
            if (!options.has(o[0])) {
                continue;
            }
            if ("b".equals(o[2])) {
                boolean value = options.optBoolean(o[0]);
                switch (o[1]) {
                    case MwtkConfig.EFFECTS: case MwtkConfig.LABELS: case MwtkConfig.FIGURES:
                    case MwtkConfig.GLOW: case MwtkConfig.STRIPE: case MwtkConfig.CARD_GLOW:
                    case MwtkConfig.DOT: case MwtkConfig.PULSE: case MwtkConfig.TILE:
                        MwtkConfig.setBool(o[1], value);
                        break;
                    default:
                        MwtkConfig.setFlag(o[1], value);
                }
            } else {
                int value = options.optInt(o[0]);
                MwtkConfig.setNum(o[1], value);
                if (MwtkConfig.APP_ICON.equals(o[1])) {
                    MwtkIcons.apply(value);
                }
            }
        }
    }

    // ------------------------------------------------------------ files

    private static String desktopNames() throws Exception {
        JSONObject phone = MwtkNames.export();
        JSONObject result = new JSONObject();
        int account = UserConfig.selectedAccount;
        Iterator<String> it = phone.keys();
        while (it.hasNext()) {
            String key = it.next();
            String[] parts = key.split(":");
            if (parts.length != 2) {
                continue;
            }
            long self = Long.parseLong(parts[0]) | RESERVED;
            long peer = desktopFromDialog(account, Long.parseLong(parts[1]));
            result.put(self + ":" + peer, phone.optString(key));
        }
        return result.toString();
    }

    private static void importNames(String text) throws Exception {
        JSONObject desktop = new JSONObject(text);
        JSONObject phone = new JSONObject();
        Iterator<String> it = desktop.keys();
        while (it.hasNext()) {
            String key = it.next();
            String[] parts = key.split(":");
            if (parts.length != 2) {
                continue;
            }
            long self = Long.parseLong(parts[0]) & BARE_MASK;
            long dialog = dialogFromDesktop(Long.parseLong(parts[1]));
            phone.put(self + ":" + dialog, desktop.optString(key));
        }
        MwtkNames.importAll(phone);
    }

    private static void importNotes(String text) throws Exception {
        JSONObject accounts = new JSONObject(text).optJSONObject("accounts");
        if (accounts == null) {
            return;
        }
        JSONObject peers = accounts.optJSONObject(String.valueOf(self()));
        if (peers == null) {
            return;
        }
        Iterator<String> it = peers.keys();
        while (it.hasNext()) {
            String key = it.next();
            MwtkNotes.set(dialogFromDesktop(Long.parseLong(key)), peers.optString(key));
        }
    }

    private static void importHidden(String text) throws Exception {
        JSONObject root = new JSONObject(text);
        JSONObject phone = MwtkStore.readJson("hidden.json");
        if (!root.optString("hash").isEmpty()) {
            phone.put("salt", root.optString("salt"));
            phone.put("hash", root.optString("hash"));
        }
        JSONObject accounts = root.optJSONObject("accounts");
        JSONArray list = accounts != null ? accounts.optJSONArray(String.valueOf(self())) : null;
        if (list != null) {
            JSONArray converted = new JSONArray();
            for (int i = 0; i < list.length(); i++) {
                converted.put(dialogFromDesktop(Long.parseLong(list.optString(i))));
            }
            JSONObject chats = phone.optJSONObject("chats");
            if (chats == null) {
                chats = new JSONObject();
                phone.put("chats", chats);
            }
            chats.put(String.valueOf(self()), converted);
        }
        MwtkStore.writeJson("hidden.json", phone);
        MwtkHidden.reload();
    }

    private static void importPlaylists(String text) throws Exception {
        JSONObject accounts = new JSONObject(text).optJSONObject("accounts");
        JSONArray lists = accounts != null ? accounts.optJSONArray(String.valueOf(self())) : null;
        if (lists == null) {
            return;
        }
        JSONArray converted = new JSONArray();
        for (int i = 0; i < lists.length(); i++) {
            JSONObject list = lists.optJSONObject(i);
            if (list == null) {
                continue;
            }
            JSONObject out = new JSONObject();
            out.put("name", list.optString("name"));
            JSONArray tracks = new JSONArray();
            JSONArray source = list.optJSONArray("tracks");
            for (int t = 0; source != null && t < source.length(); t++) {
                JSONObject track = source.optJSONObject(t);
                JSONObject copy = new JSONObject();
                copy.put("peer", dialogFromDesktop(Long.parseLong(track.optString("peer", "0"))));
                copy.put("msg", Long.parseLong(track.optString("msg", "0")));
                copy.put("title", track.optString("title"));
                copy.put("performer", track.optString("performer"));
                copy.put("duration", track.optInt("duration"));
                tracks.put(copy);
            }
            out.put("tracks", tracks);
            converted.put(out);
        }
        MwtkPlaylists.replaceAll(converted);
    }

    // ------------------------------------------------------------ entry points

    private static byte[] read(File file) throws Exception {
        try (FileInputStream in = new FileInputStream(file)) {
            byte[] data = new byte[(int) Math.min(file.length(), 8 * 1024 * 1024)];
            int read = 0;
            while (read < data.length) {
                int r = in.read(data, read, data.length - read);
                if (r < 0) {
                    break;
                }
                read += r;
            }
            return data;
        }
    }

    private static boolean startsWith(byte[] data, byte[] magic) {
        if (data.length < magic.length) {
            return false;
        }
        for (int i = 0; i < magic.length; i++) {
            if (data[i] != magic[i]) {
                return false;
            }
        }
        return true;
    }

    /** Called when a document is opened: true if it was an MWTK file. */
    public static boolean handleOpen(Activity activity, File file, String name) {
        if (file == null || name == null || activity == null) {
            return false;
        }
        String lower = name.toLowerCase();
        if (!lower.endsWith(".mwtktheme") && !lower.endsWith(".mwtk")) {
            return false;
        }
        if (!file.exists()) {
            return false;
        }
        try {
            byte[] data = read(file);
            if (startsWith(data, THEME_MAGIC)) {
                JSONObject root = new JSONObject(new String(data, THEME_MAGIC.length, data.length - THEME_MAGIC.length, StandardCharsets.UTF_8));
                AlertDialog.Builder builder = new AlertDialog.Builder(activity);
                builder.setTitle("Тема MWTK");
                builder.setMessage("Применить «" + root.optString("name", "Тема MWTK") + "»?");
                builder.setPositiveButton("Применить", (d, w) -> applyOptions(root.optJSONObject("options")));
                builder.setNegativeButton("Отмена", null);
                builder.show();
                return true;
            }
            if (startsWith(data, SYNC_MAGIC)) {
                byte[] iv = new byte[16];
                System.arraycopy(data, SYNC_MAGIC.length, iv, 0, 16);
                int offset = SYNC_MAGIC.length + 16;
                byte[] encrypted = new byte[data.length - offset];
                System.arraycopy(data, offset, encrypted, 0, encrypted.length);
                JSONObject root = new JSONObject(new String(crypt(encrypted, iv), StandardCharsets.UTF_8));
                AlertDialog.Builder builder = new AlertDialog.Builder(activity);
                builder.setTitle("Синхронизация MWTK");
                builder.setMessage("Загрузить настройки MWTK, плейлисты, заметки, свои названия и скрытые чаты из этого файла?");
                builder.setPositiveButton("Загрузить", (d, w) -> {
                    try {
                        applyOptions(root.optJSONObject("options"));
                        JSONObject files = root.optJSONObject("files");
                        if (files != null) {
                            if (files.has("mwtk_names.json")) importNames(files.optString("mwtk_names.json"));
                            if (files.has("mwtk_notes.json")) importNotes(files.optString("mwtk_notes.json"));
                            if (files.has("mwtk_hidden.json")) importHidden(files.optString("mwtk_hidden.json"));
                            if (files.has("mwtk_playlists.json")) importPlaylists(files.optString("mwtk_playlists.json"));
                            if (files.has("mwtk_phrases.json")) MwtkPhrases.importAll(new JSONArray(files.optString("mwtk_phrases.json")));
                        }
                        MwtkUi.toast("Готово: настройки MWTK загружены");
                    } catch (Throwable e) {
                        MwtkUi.toast("Не получилось прочитать файл");
                    }
                });
                builder.setNegativeButton("Отмена", null);
                builder.show();
                return true;
            }
        } catch (Throwable e) {
            MwtkUi.toast("Этот файл от другого аккаунта или повреждён");
            return true;
        }
        return false;
    }

    private static void shareFile(Activity activity, File file, String title) {
        try {
            Uri uri = FileProvider.getUriForFile(activity, ApplicationLoader.getApplicationId() + ".provider", file);
            Intent intent = new Intent(Intent.ACTION_SEND);
            intent.setType("application/octet-stream");
            intent.putExtra(Intent.EXTRA_STREAM, uri);
            intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
            activity.startActivity(Intent.createChooser(intent, title));
        } catch (Throwable e) {
            MwtkUi.toast("Не получилось поделиться файлом");
        }
    }

    public static void exportTheme(Activity activity) {
        try {
            JSONObject root = new JSONObject();
            root.put("format", 1);
            root.put("name", "Тема MWTK с телефона");
            root.put("options", collectOptions());
            File file = new File(AndroidUtilities.getCacheDir(), "MWTK.mwtktheme");
            try (FileOutputStream out = new FileOutputStream(file)) {
                out.write(THEME_MAGIC);
                out.write(root.toString(2).getBytes(StandardCharsets.UTF_8));
            }
            shareFile(activity, file, "Поделиться темой MWTK");
        } catch (Throwable e) {
            MwtkUi.toast("Не получилось сохранить тему");
        }
    }

    /** Sync file for the desktop: send it to Saved Messages from the share sheet. */
    public static void exportSync(Activity activity) {
        try {
            JSONObject root = new JSONObject();
            root.put("format", 1);
            root.put("options", collectOptions());
            JSONObject files = new JSONObject();
            files.put("mwtk_names.json", desktopNames());
            files.put("mwtk_phrases.json", MwtkPhrases.export().toString());
            root.put("files", files);
            byte[] iv = new byte[16];
            new SecureRandom().nextBytes(iv);
            byte[] encrypted = crypt(root.toString().getBytes(StandardCharsets.UTF_8), iv);
            File file = new File(AndroidUtilities.getCacheDir(), "MWTK-sync.mwtk");
            try (FileOutputStream out = new FileOutputStream(file)) {
                out.write(SYNC_MAGIC);
                out.write(iv);
                out.write(encrypted);
            }
            shareFile(activity, file, "Отправь в «Избранное»");
        } catch (Throwable e) {
            MwtkUi.toast("Не получилось сделать файл синхронизации");
        }
    }
}
