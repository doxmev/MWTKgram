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

    /** Artists from your playlists and the song playing now (starting weight for the seeds). */
    private static ArrayList<String> knownArtists() {
        ArrayList<String> artists = new ArrayList<>();
        MessageObject playing = MediaController.getInstance().getPlayingMessageObject();
        if (playing != null && playing.isMusic()) {
            artists.add(playing.getMusicAuthor());
        }
        for (int i = 0; i < MwtkPlaylists.count(); i++) {
            JSONArray tracks = MwtkPlaylists.tracks(i);
            for (int k = 0; tracks != null && k < tracks.length(); k++) {
                JSONObject track = tracks.optJSONObject(k);
                if (track != null) {
                    artists.add(track.optString("performer", ""));
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

    /** Up to {@code take} songs of a Deezer artist that pass your dislikes and recent plays. */
    private static void addTop(long id, String name, int take, ArrayList<String> queries, java.util.Random random) {
        try {
            String top = MwtkLyrics.get("https://api.deezer.com/artist/" + id + "/top?limit=15");
            JSONArray songs = top == null ? null : new JSONObject(top).optJSONArray("data");
            if (songs == null) {
                return;
            }
            ArrayList<Integer> order = new ArrayList<>();
            for (int k = 0; k < songs.length(); k++) {
                order.add(k);
            }
            Collections.shuffle(order, random);
            int taken = 0;
            for (int k : order) {
                if (taken >= take) {
                    break;
                }
                String title = songs.getJSONObject(k).optString("title", "");
                if (title.isEmpty() || MwtkTaste.isDisliked(name, title) || MwtkTaste.playedRecently(name, title, 3)) {
                    continue;
                }
                String query = name + " " + title;
                if (!queries.contains(query)) {
                    queries.add(query);
                    taken++;
                }
            }
        } catch (Throwable ignore) {
        }
    }

    /**
     * Candidates like in a VK or Spotify mix, from the open Deezer catalog:
     * mostly songs of artists related to the ones you listen to, plus a few
     * songs of your own artists. Returns "artist title" queries.
     */
    private static ArrayList<String> candidates(ArrayList<String> seeds) {
        ArrayList<String> queries = new ArrayList<>();
        ArrayList<String> used = new ArrayList<>();
        for (String seed : seeds) {
            used.add(seed.toLowerCase(Locale.ROOT));
        }
        java.util.Random random = new java.util.Random();
        for (int s = 0; s < seeds.size(); s++) {
            try {
                String found = MwtkLyrics.get("https://api.deezer.com/search/artist?limit=1&q=" + encode(seeds.get(s)));
                JSONArray data = found == null ? null : new JSONObject(found).optJSONArray("data");
                if (data == null || data.length() == 0) {
                    continue;
                }
                long id = data.getJSONObject(0).optLong("id");
                if (s % 2 == 0) {
                    addTop(id, seeds.get(s), 1, queries, random); // one familiar song
                }
                String related = MwtkLyrics.get("https://api.deezer.com/artist/" + id + "/related?limit=20");
                JSONArray artists = related == null ? null : new JSONObject(related).optJSONArray("data");
                if (artists == null) {
                    continue;
                }
                ArrayList<Integer> order = new ArrayList<>();
                for (int k = 0; k < artists.length(); k++) {
                    order.add(k);
                }
                Collections.shuffle(order, random);
                int taken = 0;
                for (int k : order) {
                    if (taken >= 3) {
                        break;
                    }
                    JSONObject artist = artists.getJSONObject(k);
                    String name = artist.optString("name", "");
                    if (name.isEmpty() || MwtkTaste.artistBanned(name) || used.contains(name.toLowerCase(Locale.ROOT))) {
                        continue;
                    }
                    used.add(name.toLowerCase(Locale.ROOT));
                    addTop(artist.optLong("id"), name, 2, queries, random);
                    taken++;
                }
            } catch (Throwable ignore) {
            }
        }
        Collections.shuffle(queries, random);
        return queries;
    }

    private static boolean accept(MessageObject song, ArrayList<MessageObject> already) {
        String performer = song.getMusicAuthor();
        String title = song.getMusicTitle();
        if (MwtkTaste.isDisliked(performer, title) || MwtkTaste.artistBanned(performer)) {
            return false;
        }
        for (MessageObject other : already) {
            if (String.valueOf(other.getMusicAuthor()).equalsIgnoreCase(String.valueOf(performer))
                && String.valueOf(other.getMusicTitle()).equalsIgnoreCase(String.valueOf(title))) {
                return false;
            }
        }
        return true;
    }

    /**
     * «Мой микс»: built from everything you listen to — history with skips,
     * playlists and the song playing now — not only the current song.
     * Falls back to your own artists when Deezer is not reachable.
     */
    public static void mix(int account, Mixed done) {
        ArrayList<String> seeds = MwtkTaste.seedArtists(5, knownArtists());
        if (seeds.isEmpty()) {
            done.run(new ArrayList<>(), false);
            return;
        }
        org.telegram.messenger.Utilities.globalQueue.postRunnable(() -> {
            ArrayList<String> similar = candidates(seeds);
            AndroidUtilities.runOnUIThread(() -> {
                boolean exact = !similar.isEmpty();
                ArrayList<String> queries = exact ? similar : new ArrayList<>(seeds);
                while (queries.size() > (exact ? 22 : 6)) {
                    queries.remove(queries.size() - 1);
                }
                ArrayList<MessageObject> all = new ArrayList<>();
                int[] waiting = { queries.size() };
                for (String query : queries) {
                    search(account, query, songs -> {
                        if (exact) {
                            for (MessageObject song : songs) {
                                if (accept(song, all)) {
                                    all.add(song); // the best match for "artist title"
                                    break;
                                }
                            }
                        } else {
                            Collections.shuffle(songs);
                            int taken = 0;
                            for (MessageObject song : songs) {
                                if (taken < 4 && accept(song, all)) {
                                    all.add(song);
                                    taken++;
                                }
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
