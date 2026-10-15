package org.telegram.mwtk;

import android.text.TextUtils;

import org.json.JSONArray;
import org.json.JSONObject;
import org.telegram.messenger.AndroidUtilities;
import org.telegram.messenger.MediaController;
import org.telegram.messenger.MessageObject;
import org.telegram.messenger.MessagesController;
import org.telegram.messenger.NotificationCenter;
import org.telegram.messenger.SendMessagesHelper;
import org.telegram.messenger.UserConfig;
import org.telegram.tgnet.ConnectionsManager;
import org.telegram.tgnet.TLRPC;
import org.telegram.ui.ActionBar.BaseFragment;

import java.util.ArrayList;
import java.util.Collections;
import java.util.Locale;

/**
 * The official Telegram music search: the bot from the app config
 * ("music_search_username", the same one the attach menu uses). It finds
 * songs everywhere, not only in the chats the account is in.
 */
public final class MwtkMusicBot {

    public interface Found {
        void run(ArrayList<MessageObject> songs);
    }

    public interface Mixed {
        void run(ArrayList<MessageObject> songs, boolean hadArtists);
    }

    private static int localId = -2_000_000;

    private MwtkMusicBot() {
    }

    private static void resolve(int account, java.util.function.Consumer<TLRPC.User> done) {
        MessagesController controller = MessagesController.getInstance(account);
        String username = controller.config.musicSearchUsername.get();
        if (TextUtils.isEmpty(username)) {
            done.accept(null);
            return;
        }
        TLRPC.User bot = controller.getUser(username);
        if (bot != null) {
            done.accept(bot);
            return;
        }
        controller.getUserNameResolver().resolve(username, botId ->
            done.accept(botId == null ? null : controller.getUser(botId)));
    }

    /** Songs for the query as local messages; an empty list on failure. */
    public static void search(int account, String query, Found done) {
        resolve(account, bot -> {
            if (bot == null) {
                done.run(new ArrayList<>());
                return;
            }
            MessagesController controller = MessagesController.getInstance(account);
            TLRPC.User self = UserConfig.getInstance(account).getCurrentUser();
            TLRPC.TL_messages_getInlineBotResults req = new TLRPC.TL_messages_getInlineBotResults();
            req.bot = controller.getInputUser(bot);
            req.peer = MessagesController.getInputPeer(self);
            req.offset = "";
            req.query = query == null ? "" : query;
            ConnectionsManager.getInstance(account).sendRequestTyped(req, AndroidUtilities::runOnUIThread, (res, err) -> {
                ArrayList<MessageObject> songs = new ArrayList<>();
                if (res != null) {
                    controller.putUsers(res.users, false);
                    for (TLRPC.BotInlineResult r : res.results) {
                        if (!(r instanceof TLRPC.TL_botInlineMediaResult)) {
                            continue;
                        }
                        TLRPC.Document document = ((TLRPC.TL_botInlineMediaResult) r).document;
                        if (document == null) {
                            continue;
                        }
                        TLRPC.TL_message message = new TLRPC.TL_message();
                        message.out = true;
                        message.id = localId--;
                        message.peer_id = new TLRPC.TL_peerUser();
                        message.from_id = new TLRPC.TL_peerUser();
                        message.peer_id.user_id = message.from_id.user_id = UserConfig.getInstance(account).getClientUserId();
                        message.date = (int) (System.currentTimeMillis() / 1000);
                        message.message = "";
                        message.media = new TLRPC.TL_messageMediaDocument();
                        message.media.flags |= 3;
                        message.media.document = document;
                        message.flags |= TLRPC.MESSAGE_FLAG_HAS_MEDIA | TLRPC.MESSAGE_FLAG_HAS_FROM_ID;
                        MessageObject object = new MessageObject(account, message, false, true);
                        if (object.isMusic()) {
                            songs.add(object);
                        }
                    }
                }
                done.run(songs);
            });
        });
    }

    /**
     * A song from the bot is not a real message: it is sent to «Избранное»
     * first, and the saved message goes to the playlist.
     */
    public static void saveAndAdd(BaseFragment fragment, MessageObject song) {
        if (song == null) {
            return;
        }
        if (song.getId() > 0) {
            MwtkPlaylists.showAddBox(fragment, song);
            return;
        }
        int account = fragment.getCurrentAccount();
        long self = UserConfig.getInstance(account).getClientUserId();
        TLRPC.Document document = song.getDocument();
        if (document == null) {
            return;
        }
        NotificationCenter center = NotificationCenter.getInstance(account);
        NotificationCenter.NotificationCenterDelegate[] holder = new NotificationCenter.NotificationCenterDelegate[1];
        holder[0] = (id, acc, args) -> {
            if (args.length < 4 || !(args[2] instanceof TLRPC.Message) || !(args[3] instanceof Long)) {
                return;
            }
            TLRPC.Message message = (TLRPC.Message) args[2];
            if ((Long) args[3] != self || message.media == null || message.media.document == null
                || message.media.document.id != document.id) {
                return;
            }
            center.removeObserver(holder[0], NotificationCenter.messageReceivedByServer);
            MwtkPlaylists.showAddBox(fragment, new MessageObject(account, message, false, true));
        };
        center.addObserver(holder[0], NotificationCenter.messageReceivedByServer);
        AndroidUtilities.runOnUIThread(() -> center.removeObserver(holder[0], NotificationCenter.messageReceivedByServer), 60_000);
        if (!(document instanceof TLRPC.TL_document)) {
            center.removeObserver(holder[0], NotificationCenter.messageReceivedByServer);
            MwtkUi.toast("Эту песню не получилось сохранить");
            return;
        }
        // The song is already on Telegram servers: send it as an existing
        // document, like a sticker (the old way crashed on a missing file path).
        SendMessagesHelper.SendMessageParams params = SendMessagesHelper.SendMessageParams.of(
            (TLRPC.TL_document) document, null, null, self, null, null, null, null, null, null,
            true, 0, 0, 0, null, null, false);
        SendMessagesHelper.getInstance(account).sendMessage(params);
        MwtkUi.toast("Сохраняю песню в «Избранное»…");
    }

    /** Artists you listen to: the song playing now and your playlists. */
    private static ArrayList<String> seedArtists() {
        ArrayList<String> artists = new ArrayList<>();
        MessageObject playing = MediaController.getInstance().getPlayingMessageObject();
        if (playing != null && playing.isMusic()) {
            addArtist(artists, playing.getMusicAuthor());
        }
        for (int i = 0; i < MwtkPlaylists.count(); i++) {
            JSONArray tracks = MwtkPlaylists.tracks(i);
            for (int k = 0; tracks != null && k < tracks.length(); k++) {
                JSONObject track = tracks.optJSONObject(k);
                if (track != null) {
                    addArtist(artists, track.optString("performer", ""));
                }
            }
        }
        return artists;
    }

    private static String encode(String text) {
        try {
            return java.net.URLEncoder.encode(text, "UTF-8");
        } catch (Throwable e) {
            return "";
        }
    }

    /**
     * Similar artists from the open Deezer catalog (no key needed): for each
     * artist you listen to, its related artists and their top songs.
     * Returns "artist — title" queries, or an empty list on failure.
     */
    private static ArrayList<String> similarSongs(ArrayList<String> seeds) {
        ArrayList<String> queries = new ArrayList<>();
        ArrayList<String> used = new ArrayList<>();
        for (String seed : seeds) {
            used.add(seed.toLowerCase(Locale.ROOT));
        }
        java.util.Random random = new java.util.Random();
        for (int s = 0; s < Math.min(4, seeds.size()); s++) {
            try {
                String found = MwtkLyrics.get("https://api.deezer.com/search/artist?limit=1&q=" + encode(seeds.get(s)));
                JSONArray data = found == null ? null : new JSONObject(found).optJSONArray("data");
                if (data == null || data.length() == 0) {
                    continue;
                }
                long id = data.getJSONObject(0).optLong("id");
                String related = MwtkLyrics.get("https://api.deezer.com/artist/" + id + "/related?limit=12");
                JSONArray artists = related == null ? null : new JSONObject(related).optJSONArray("data");
                if (artists == null) {
                    continue;
                }
                int taken = 0;
                for (int a = 0; a < artists.length() && taken < 3; a++) {
                    JSONObject artist = artists.getJSONObject(random.nextInt(artists.length()));
                    String name = artist.optString("name", "");
                    if (name.isEmpty() || used.contains(name.toLowerCase(Locale.ROOT))) {
                        continue;
                    }
                    used.add(name.toLowerCase(Locale.ROOT));
                    String top = MwtkLyrics.get("https://api.deezer.com/artist/" + artist.optLong("id") + "/top?limit=8");
                    JSONArray songs = top == null ? null : new JSONObject(top).optJSONArray("data");
                    if (songs == null || songs.length() == 0) {
                        continue;
                    }
                    for (int k = 0; k < 2 && k < songs.length(); k++) {
                        String title = songs.getJSONObject(random.nextInt(songs.length())).optString("title", "");
                        String query = name + " " + title;
                        if (!title.isEmpty() && !queries.contains(query)) {
                            queries.add(query);
                        }
                    }
                    taken++;
                }
            } catch (Throwable ignore) {
            }
        }
        Collections.shuffle(queries);
        return queries;
    }

    /**
     * «Мой микс» like in VK: songs of artists similar to the ones you listen
     * to, not the same artists again. Falls back to your own artists when
     * the catalog of similar artists is not reachable.
     */
    public static void mix(int account, Mixed done) {
        ArrayList<String> seeds = seedArtists();
        if (seeds.isEmpty()) {
            done.run(new ArrayList<>(), false);
            return;
        }
        Collections.shuffle(seeds);
        org.telegram.messenger.Utilities.globalQueue.postRunnable(() -> {
            ArrayList<String> similar = similarSongs(seeds);
            AndroidUtilities.runOnUIThread(() -> {
                boolean exact = !similar.isEmpty();
                ArrayList<String> queries = exact ? similar : new ArrayList<>(seeds);
                while (queries.size() > (exact ? 18 : 6)) {
                    queries.remove(queries.size() - 1);
                }
                ArrayList<MessageObject> all = new ArrayList<>();
                int[] waiting = { queries.size() };
                for (String query : queries) {
                    search(account, query, songs -> {
                        if (exact) {
                            if (!songs.isEmpty()) {
                                all.add(songs.get(0)); // the best match for "artist title"
                            }
                        } else {
                            Collections.shuffle(songs);
                            for (int i = 0; i < Math.min(4, songs.size()); i++) {
                                all.add(songs.get(i));
                            }
                        }
                        if (--waiting[0] == 0) {
                            Collections.shuffle(all);
                            done.run(all, true);
                        }
                    });
                }
            });
        });
    }

    private static void addArtist(ArrayList<String> artists, String name) {
        if (name == null) {
            return;
        }
        String clean = name.trim();
        if (clean.isEmpty() || "null".equals(clean)) {
            return;
        }
        for (String existing : artists) {
            if (existing.toLowerCase(Locale.ROOT).equals(clean.toLowerCase(Locale.ROOT))) {
                return;
            }
        }
        artists.add(clean);
    }
}
