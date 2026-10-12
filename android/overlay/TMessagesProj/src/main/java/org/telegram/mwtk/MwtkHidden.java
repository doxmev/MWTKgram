package org.telegram.mwtk;

import android.content.Context;
import android.content.DialogInterface;
import android.hardware.biometrics.BiometricPrompt;
import android.os.Build;
import android.os.CancellationSignal;
import android.os.SystemClock;
import android.util.Base64;

import org.json.JSONArray;
import org.json.JSONObject;
import org.telegram.messenger.NotificationCenter;
import org.telegram.messenger.UserConfig;
import org.telegram.tgnet.TLRPC;
import org.telegram.ui.ActionBar.BaseFragment;

import java.security.MessageDigest;
import java.security.SecureRandom;
import java.util.ArrayList;
import java.util.HashSet;

/**
 * Hidden chats: they disappear from the chat list until unlocked with the
 * password or a fingerprint. Unlocked state lasts five minutes.
 */
public final class MwtkHidden {

    private static final String FILE = "hidden.json";
    private static final long UNLOCK_TIME = 5 * 60 * 1000;

    private static JSONObject data;
    private static volatile HashSet<Long> cache;
    private static volatile long cacheAccountUser;
    private static volatile long unlockedUntil;

    private MwtkHidden() {
    }

    private static synchronized JSONObject data() {
        if (data == null) {
            data = MwtkStore.readJson(FILE);
        }
        return data;
    }

    private static long self() {
        return UserConfig.getInstance(UserConfig.selectedAccount).getClientUserId();
    }

    private static synchronized HashSet<Long> set() {
        long user = self();
        if (cache == null || cacheAccountUser != user) {
            HashSet<Long> result = new HashSet<>();
            JSONArray list = data().optJSONObject("chats") != null
                ? data().optJSONObject("chats").optJSONArray(String.valueOf(user))
                : null;
            if (list != null) {
                for (int i = 0; i < list.length(); i++) {
                    result.add(list.optLong(i));
                }
            }
            cache = result;
            cacheAccountUser = user;
        }
        return cache;
    }

    private static synchronized void saveSet(HashSet<Long> value) {
        try {
            JSONObject chats = data().optJSONObject("chats");
            if (chats == null) {
                chats = new JSONObject();
                data().put("chats", chats);
            }
            JSONArray list = new JSONArray();
            for (Long id : value) {
                list.put(id);
            }
            chats.put(String.valueOf(self()), list);
        } catch (Throwable ignore) {
        }
        MwtkStore.writeJson(FILE, data());
        cache = value;
    }

    public static boolean isHidden(long dialogId) {
        return set().contains(dialogId);
    }

    public static boolean unlocked() {
        return SystemClock.elapsedRealtime() < unlockedUntil;
    }

    public static void lock() {
        unlockedUntil = 0;
        refreshLists();
    }

    /** Chat list filter. */
    public static ArrayList<TLRPC.Dialog> filter(ArrayList<TLRPC.Dialog> dialogs) {
        HashSet<Long> hidden = set();
        if (dialogs == null || hidden.isEmpty() || unlocked()) {
            return dialogs;
        }
        boolean any = false;
        for (int i = 0, n = dialogs.size(); i < n; i++) {
            TLRPC.Dialog dialog = dialogs.get(i);
            if (dialog != null && hidden.contains(dialog.id)) {
                any = true;
                break;
            }
        }
        if (!any) {
            return dialogs;
        }
        ArrayList<TLRPC.Dialog> result = new ArrayList<>(dialogs.size());
        for (int i = 0, n = dialogs.size(); i < n; i++) {
            TLRPC.Dialog dialog = dialogs.get(i);
            if (dialog == null || !hidden.contains(dialog.id)) {
                result.add(dialog);
            }
        }
        return result;
    }

    private static void refreshLists() {
        for (int a = 0; a < UserConfig.MAX_ACCOUNT_COUNT; a++) {
            if (UserConfig.getInstance(a).isClientActivated()) {
                NotificationCenter.getInstance(a).postNotificationName(NotificationCenter.dialogsNeedReload);
            }
        }
    }

    private static boolean hasPassword() {
        return data().has("hash");
    }

    // Same as the desktop: hex(sha256^1000(salt + ":mwtk:" + password)).
    private static String hash(String password, String salt) {
        try {
            MessageDigest digest = MessageDigest.getInstance("SHA-256");
            byte[] value = (salt + ":mwtk:" + password).getBytes("UTF-8");
            for (int i = 0; i < 1000; i++) {
                value = digest.digest(value);
            }
            StringBuilder b = new StringBuilder();
            for (byte v : value) {
                b.append(String.format("%02x", v));
            }
            return b.toString();
        } catch (Throwable e) {
            return "";
        }
    }

    /** Re-reads the file after a sync import. */
    public static synchronized void reload() {
        data = null;
        cache = null;
        refreshLists();
    }

    private static synchronized void setPassword(String password) {
        try {
            byte[] saltBytes = new byte[16];
            new SecureRandom().nextBytes(saltBytes);
            String salt = Base64.encodeToString(saltBytes, Base64.NO_WRAP);
            data().put("salt", salt);
            data().put("hash", hash(password, salt));
        } catch (Throwable ignore) {
        }
        MwtkStore.writeJson(FILE, data());
    }

    private static boolean check(String password) {
        return hasPassword() && hash(password, data().optString("salt")).equals(data().optString("hash"));
    }

    private static void unlock() {
        unlockedUntil = SystemClock.elapsedRealtime() + UNLOCK_TIME;
        refreshLists();
        MwtkUi.toast("Скрытые чаты открыты на 5 минут");
    }

    /** Hide or return a chat; asks to create a password first. */
    public static void toggle(BaseFragment fragment, long dialogId) {
        if (!hasPassword()) {
            createPassword(fragment, () -> toggle(fragment, dialogId));
            return;
        }
        HashSet<Long> value = new HashSet<>(set());
        if (value.contains(dialogId)) {
            value.remove(dialogId);
            MwtkUi.toast("Чат возвращён из скрытых");
        } else {
            value.add(dialogId);
            MwtkUi.toast("Чат скрыт. Открыть: Настройки → MWTKgram → Скрытые чаты");
        }
        saveSet(value);
        refreshLists();
    }

    private static void createPassword(BaseFragment fragment, Runnable done) {
        MwtkUi.input(fragment, "Пароль для скрытых чатов",
            "Придумай пароль. Его нужно вводить, чтобы увидеть скрытые чаты.",
            "", "Пароль", false, true, null, text -> {
                if (text.length() < 3) {
                    MwtkUi.toast("Минимум 3 символа");
                    return;
                }
                setPassword(text);
                done.run();
            });
    }

    /** Opens hidden chats: fingerprint when possible, else the password. */
    public static void open(BaseFragment fragment) {
        if (!hasPassword()) {
            createPassword(fragment, () -> open(fragment));
            return;
        }
        if (unlocked()) {
            lock();
            MwtkUi.toast("Скрытые чаты закрыты");
            return;
        }
        Context context = fragment.getParentActivity();
        if (context != null && Build.VERSION.SDK_INT >= 28 && MwtkConfig.flag("hidden_fingerprint", true)) {
            try {
                BiometricPrompt prompt = new BiometricPrompt.Builder(context)
                    .setTitle("Скрытые чаты")
                    .setSubtitle("Подтверди, что это ты")
                    .setNegativeButton("Пароль", context.getMainExecutor(),
                        (DialogInterface dialog, int which) -> askPassword(fragment))
                    .build();
                prompt.authenticate(new CancellationSignal(), context.getMainExecutor(),
                    new BiometricPrompt.AuthenticationCallback() {
                        @Override
                        public void onAuthenticationSucceeded(BiometricPrompt.AuthenticationResult result) {
                            unlock();
                        }

                        @Override
                        public void onAuthenticationError(int code, CharSequence message) {
                            if (code != BiometricPrompt.BIOMETRIC_ERROR_USER_CANCELED
                                && code != BiometricPrompt.BIOMETRIC_ERROR_CANCELED) {
                                askPassword(fragment);
                            }
                        }
                    });
                return;
            } catch (Throwable ignore) {
            }
        }
        askPassword(fragment);
    }

    private static void askPassword(BaseFragment fragment) {
        MwtkUi.input(fragment, "Скрытые чаты", null, "", "Пароль", false, true, null, text -> {
            if (check(text)) {
                unlock();
            } else {
                MwtkUi.toast("Неверный пароль");
            }
        });
    }

    public static void changePassword(BaseFragment fragment) {
        if (!hasPassword()) {
            createPassword(fragment, () -> MwtkUi.toast("Пароль установлен"));
            return;
        }
        MwtkUi.input(fragment, "Текущий пароль", null, "", "Пароль", false, true, null, text -> {
            if (!check(text)) {
                MwtkUi.toast("Неверный пароль");
                return;
            }
            createPassword(fragment, () -> MwtkUi.toast("Пароль изменён"));
        });
    }
}
