package org.telegram.mwtk;

import org.json.JSONObject;
import org.telegram.messenger.ApplicationLoader;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.nio.charset.StandardCharsets;

/** Small JSON files in the app's private folder. */
public final class MwtkStore {

    private MwtkStore() {
    }

    public static File file(String name) {
        File dir = new File(ApplicationLoader.applicationContext.getFilesDir(), "mwtk");
        if (!dir.exists()) {
            dir.mkdirs();
        }
        return new File(dir, name);
    }

    public static String readText(String name) {
        File f = file(name);
        if (!f.exists()) {
            return "";
        }
        try (FileInputStream in = new FileInputStream(f)) {
            byte[] data = new byte[(int) f.length()];
            int read = 0;
            while (read < data.length) {
                int r = in.read(data, read, data.length - read);
                if (r < 0) {
                    break;
                }
                read += r;
            }
            return new String(data, 0, read, StandardCharsets.UTF_8);
        } catch (Throwable e) {
            return "";
        }
    }

    public static void writeText(String name, String text) {
        File f = file(name);
        File tmp = new File(f.getParentFile(), name + ".tmp");
        try (FileOutputStream out = new FileOutputStream(tmp)) {
            out.write(text.getBytes(StandardCharsets.UTF_8));
        } catch (Throwable e) {
            return;
        }
        if (!tmp.renameTo(f)) {
            f.delete();
            tmp.renameTo(f);
        }
    }

    public static JSONObject readJson(String name) {
        try {
            String text = readText(name);
            return text.isEmpty() ? new JSONObject() : new JSONObject(text);
        } catch (Throwable e) {
            return new JSONObject();
        }
    }

    public static void writeJson(String name, JSONObject object) {
        writeText(name, object.toString());
    }
}
