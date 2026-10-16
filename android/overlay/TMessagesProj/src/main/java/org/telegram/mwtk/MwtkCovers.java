package org.telegram.mwtk;

import android.app.Activity;
import android.content.Intent;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.net.Uri;

import org.telegram.messenger.AndroidUtilities;
import org.telegram.messenger.ApplicationLoader;
import org.telegram.messenger.MessageObject;
import org.telegram.messenger.NotificationCenter;
import org.telegram.messenger.Utilities;
import org.telegram.ui.ActionBar.BaseFragment;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.security.MessageDigest;
import java.util.HashMap;
import java.util.Locale;

/**
 * Your own cover for a song. Kept only on this phone, by artist and title,
 * so the same song in any chat gets it.
 */
public final class MwtkCovers {

    private static final int REQUEST = 78121;
    private static final int SIDE = 640;
    private static final HashMap<String, Bitmap> cache = new HashMap<>();
    private static final HashMap<String, Boolean> missing = new HashMap<>();
    private static String pendingKey;
    private static boolean listening;
    private static int version;

    private MwtkCovers() {
    }

    /** Grows every time a cover is set or removed: views reload their image. */
    public static int version() {
        return version;
    }

    private static String keyFor(MessageObject message) {
        String performer = message.getMusicAuthor();
        String title = message.getMusicTitle();
        String text = (performer == null ? "" : performer.trim().toLowerCase(Locale.ROOT))
            + "\n" + (title == null ? "" : title.trim().toLowerCase(Locale.ROOT));
        if (text.trim().isEmpty() && message.getDocument() != null) {
            text = "doc" + message.getDocument().id;
        }
        try {
            MessageDigest md5 = MessageDigest.getInstance("MD5");
            byte[] hash = md5.digest(text.getBytes("UTF-8"));
            StringBuilder b = new StringBuilder();
            for (byte x : hash) {
                b.append(String.format(Locale.US, "%02x", x));
            }
            return b.toString();
        } catch (Throwable e) {
            return String.valueOf(text.hashCode());
        }
    }

    private static File fileFor(String key) {
        return MwtkStore.file("cover_" + key + ".jpg");
    }

    public static Bitmap get(MessageObject message) {
        if (message == null) {
            return null;
        }
        String key = keyFor(message);
        Bitmap cached = cache.get(key);
        if (cached != null || missing.containsKey(key)) {
            return cached;
        }
        File file = fileFor(key);
        Bitmap bitmap = file.exists() ? BitmapFactory.decodeFile(file.getAbsolutePath()) : null;
        if (bitmap != null) {
            cache.put(key, bitmap);
        } else {
            missing.put(key, true);
        }
        return bitmap;
    }

    public static boolean has(MessageObject message) {
        return get(message) != null;
    }

    public static void remove(MessageObject message) {
        String key = keyFor(message);
        fileFor(key).delete();
        cache.remove(key);
        missing.put(key, true);
        version++;
        MwtkConfig.changedExternally();
    }

    /** Opens the gallery; the picked picture becomes the cover of the song. */
    public static void pick(BaseFragment fragment, MessageObject message) {
        if (fragment == null || message == null || fragment.getParentActivity() == null) {
            return;
        }
        if (!listening) {
            listening = true;
            NotificationCenter.getGlobalInstance().addObserver((id, account, args) -> {
                if (args.length >= 3 && args[0] instanceof Integer && (Integer) args[0] == REQUEST) {
                    onResult((Integer) args[1], (Intent) args[2]);
                }
            }, NotificationCenter.onActivityResultReceived);
        }
        pendingKey = keyFor(message);
        Intent intent = new Intent(Intent.ACTION_GET_CONTENT);
        intent.setType("image/*");
        try {
            fragment.getParentActivity().startActivityForResult(Intent.createChooser(intent, "Обложка песни"), REQUEST);
        } catch (Throwable e) {
            MwtkUi.toast("Не нашлось приложения с картинками");
        }
    }

    private static void onResult(int resultCode, Intent data) {
        String key = pendingKey;
        pendingKey = null;
        if (key == null || resultCode != Activity.RESULT_OK || data == null || data.getData() == null) {
            return;
        }
        Uri uri = data.getData();
        Utilities.globalQueue.postRunnable(() -> {
            Bitmap result = null;
            try (InputStream in = ApplicationLoader.applicationContext.getContentResolver().openInputStream(uri)) {
                Bitmap source = BitmapFactory.decodeStream(in);
                if (source != null) {
                    int side = Math.min(source.getWidth(), source.getHeight());
                    Bitmap square = Bitmap.createBitmap(source,
                        (source.getWidth() - side) / 2, (source.getHeight() - side) / 2, side, side);
                    result = side > SIDE ? Bitmap.createScaledBitmap(square, SIDE, SIDE, true) : square;
                    try (FileOutputStream out = new FileOutputStream(fileFor(key))) {
                        result.compress(Bitmap.CompressFormat.JPEG, 90, out);
                    }
                }
            } catch (Throwable ignore) {
            }
            final Bitmap saved = result;
            AndroidUtilities.runOnUIThread(() -> {
                if (saved == null) {
                    MwtkUi.toast("Не получилось открыть картинку");
                    return;
                }
                cache.put(key, saved);
                missing.remove(key);
                version++;
                MwtkConfig.changedExternally();
                MwtkUi.toast("Обложка сохранена");
            });
        });
    }
}
