package org.telegram.mwtk;

import org.json.JSONArray;
import org.json.JSONObject;
import org.telegram.messenger.AndroidUtilities;
import org.telegram.messenger.MessageObject;
import org.telegram.messenger.NotificationCenter;
import org.telegram.messenger.SendMessagesHelper;
import org.telegram.messenger.UserConfig;
import org.telegram.messenger.Utilities;
import org.telegram.tgnet.TLRPC;
import org.telegram.ui.ActionBar.BaseFragment;

import java.util.ArrayList;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * A playlist from Spotify, Yandex Music or Deezer by its link; the songs are
 * then found in the music search of Telegram.
 */
public final class MwtkImport {

    public interface Fetched {
        void run(String name, ArrayList<String[]> songs, String error);
    }

    public interface Found {
        void run(ArrayList<MessageObject> songs);
    }

    private static final Pattern DEEZER = Pattern.compile("deezer\\.com/(?:[a-z]{2}/)?(playlist|album)/(\\d+)");
    private static final Pattern SPOTIFY = Pattern.compile("open\\.spotify\\.com/(?:intl-[a-z]+/)?(playlist|album)/([A-Za-z0-9]+)");
    private static final Pattern YANDEX = Pattern.compile("music\\.yandex\\.[a-z]+/users/([^/]+)/playlists/(\\d+)");
    private static final Pattern SPOTIFY_ITEM = Pattern.compile("\"title\":\"((?:[^\"\\\\]|\\\\.)*)\",\"subtitle\":\"((?:[^\"\\\\]|\\\\.)*)\"");
    private static final Pattern SPOTIFY_NAME = Pattern.compile("\"name\":\"((?:[^\"\\\\]|\\\\.)*)\"");

    private MwtkImport() {
    }

    private static String unescape(String value) {
        try {
            return new JSONArray("[\"" + value + "\"]").getString(0);
        } catch (Throwable e) {
            return value;
        }
    }

    /** Song pairs are {artist, title}; called on the UI thread. */
    public static void fetch(String link, Fetched done) {
        Utilities.globalQueue.postRunnable(() -> {
            String name = null;
            ArrayList<String[]> songs = new ArrayList<>();
            String error;
            try {
                Matcher m;
                if ((m = DEEZER.matcher(link)).find()) {
                    String body = MwtkLyrics.get("https://api.deezer.com/" + m.group(1) + "/" + m.group(2) + "?limit=200");
                    JSONObject root = body == null ? new JSONObject() : new JSONObject(body);
                    name = root.optString("title");
                    JSONObject tracks = root.optJSONObject("tracks");
                    JSONArray data = tracks != null ? tracks.optJSONArray("data") : null;
                    for (int i = 0; data != null && i < data.length(); i++) {
                        JSONObject track = data.getJSONObject(i);
                        JSONObject artist = track.optJSONObject("artist");
                        songs.add(new String[] { artist != null ? artist.optString("name") : "", track.optString("title") });
                    }
                    error = songs.isEmpty() ? "Deezer не отдал плейлист. Он открыт для всех?" : null;
                } else if ((m = SPOTIFY.matcher(link)).find()) {
                    // Spotify has no open API: its embed page carries the track list.
                    String page = MwtkLyrics.get("https://open.spotify.com/embed/" + m.group(1) + "/" + m.group(2));
                    if (page != null) {
                        Matcher item = SPOTIFY_ITEM.matcher(page);
                        while (item.find()) {
                            songs.add(new String[] { unescape(item.group(2)), unescape(item.group(1)) });
                        }
                        Matcher title = SPOTIFY_NAME.matcher(page);
                        name = title.find() ? unescape(title.group(1)) : "Из Spotify";
                    }
                    error = songs.isEmpty() ? "Spotify не показал песни. Плейлист открыт для всех?" : null;
                } else if ((m = YANDEX.matcher(link)).find()) {
                    String body = MwtkLyrics.get("https://music.yandex.ru/handlers/playlist.jsx?owner=" + m.group(1) + "&kinds=" + m.group(2));
                    JSONObject playlist = body == null ? null : new JSONObject(body).optJSONObject("playlist");
                    if (playlist != null) {
                        name = playlist.optString("title");
                        JSONArray tracks = playlist.optJSONArray("tracks");
                        for (int i = 0; tracks != null && i < tracks.length(); i++) {
                            JSONObject track = tracks.getJSONObject(i);
                            JSONArray artists = track.optJSONArray("artists");
                            String artist = artists != null && artists.length() > 0 ? artists.getJSONObject(0).optString("name") : "";
                            songs.add(new String[] { artist, track.optString("title") });
                        }
                    }
                    error = songs.isEmpty() ? "Яндекс Музыка не отдала плейлист (бывает капча или закрытый плейлист)" : null;
                } else {
                    error = "Поддерживаются ссылки на плейлисты Deezer, Spotify и Яндекс Музыки (music.yandex.ru/users/…/playlists/…)";
                }
            } catch (Throwable e) {
                error = "Не получилось прочитать плейлист";
            }
            while (songs.size() > 80) {
                songs.remove(songs.size() - 1);
            }
            final String finalName = name;
            final String finalError = error;
            AndroidUtilities.runOnUIThread(() -> done.run(finalName, songs, finalError));
        });
    }

    /** Finds every song in the music search of Telegram, keeping the order. */
    public static void findAll(int account, ArrayList<String[]> songs, Found done) {
        MessageObject[] found = new MessageObject[songs.size()];
        int[] left = { songs.size() };
        if (songs.isEmpty()) {
            done.run(new ArrayList<>());
            return;
        }
        for (int i = 0; i < songs.size(); i++) {
            final int index = i;
            MwtkMusicBot.search(account, songs.get(i)[0] + " " + songs.get(i)[1], result -> {
                if (!result.isEmpty()) {
                    found[index] = result.get(0);
                }
                if (--left[0] == 0) {
                    ArrayList<MessageObject> list = new ArrayList<>();
                    for (MessageObject object : found) {
                        if (object != null) {
                            list.add(object);
                        }
                    }
                    done.run(list);
                }
            });
        }
    }

    /**
     * Songs from the bot are not real messages: each one is sent to
     * «Избранное» and the saved message goes to the new playlist.
     */
    public static void saveAll(BaseFragment fragment, ArrayList<MessageObject> songs, String name) {
        int account = fragment.getCurrentAccount();
        long self = UserConfig.getInstance(account).getClientUserId();
        int index = MwtkPlaylists.create(name);
        ArrayList<Long> pending = new ArrayList<>();
        for (MessageObject song : songs) {
            if (song.getId() > 0) {
                MwtkPlaylists.add(index, song);
            } else if (song.getDocument() instanceof TLRPC.TL_document) {
                pending.add(song.getDocument().id);
            }
        }
        if (pending.isEmpty()) {
            MwtkUi.toast("Плейлист «" + name + "» готов");
            return;
        }
        NotificationCenter center = NotificationCenter.getInstance(account);
        NotificationCenter.NotificationCenterDelegate[] holder = new NotificationCenter.NotificationCenterDelegate[1];
        holder[0] = (id, acc, args) -> {
            if (args.length < 4 || !(args[2] instanceof TLRPC.Message) || !(args[3] instanceof Long) || (Long) args[3] != self) {
                return;
            }
            TLRPC.Message message = (TLRPC.Message) args[2];
            if (message.media == null || message.media.document == null || !pending.remove(message.media.document.id)) {
                return;
            }
            MwtkPlaylists.add(index, new MessageObject(account, message, false, false));
            if (pending.isEmpty()) {
                center.removeObserver(holder[0], NotificationCenter.messageReceivedByServer);
                MwtkUi.toast("Плейлист «" + name + "» готов");
            }
        };
        center.addObserver(holder[0], NotificationCenter.messageReceivedByServer);
        AndroidUtilities.runOnUIThread(() -> center.removeObserver(holder[0], NotificationCenter.messageReceivedByServer), 10 * 60_000);
        // Not all at once: Telegram does not like dozens of messages a second.
        long delay = 0;
        for (MessageObject song : songs) {
            if (song.getId() > 0 || !(song.getDocument() instanceof TLRPC.TL_document)) {
                continue;
            }
            TLRPC.TL_document document = (TLRPC.TL_document) song.getDocument();
            AndroidUtilities.runOnUIThread(() -> {
                SendMessagesHelper.SendMessageParams params = SendMessagesHelper.SendMessageParams.of(
                    document, null, null, self, null, null, null, null, null, null,
                    true, 0, 0, 0, null, null, false);
                SendMessagesHelper.getInstance(account).sendMessage(params);
            }, delay);
            delay += 450;
        }
        MwtkUi.toast("Сохраняю песни в «Избранное» и собираю плейлист «" + name + "»…");
    }
}
