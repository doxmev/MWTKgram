package org.telegram.mwtk;

import android.util.Base64;

import org.json.JSONObject;
import org.telegram.messenger.AndroidUtilities;
import org.telegram.messenger.MediaController;
import org.telegram.messenger.MessageObject;
import org.telegram.messenger.MessagesController;
import org.telegram.messenger.NotificationCenter;
import org.telegram.messenger.SendMessagesHelper;
import org.telegram.messenger.UserConfig;
import org.telegram.messenger.UserObject;
import org.telegram.tgnet.ConnectionsManager;
import org.telegram.tgnet.TLRPC;
import org.telegram.ui.ActionBar.AlertDialog;
import org.telegram.ui.ActionBar.BaseFragment;
import org.telegram.ui.LaunchActivity;

import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Random;

/**
 * «Слушать вместе»: you invite a person in a private chat, their MWTKgram
 * asks them first, and only after «Принять» their player follows yours.
 * The state travels in one message that the host edits (no spam, no
 * notifications), the same format as on the PC.
 */
public final class MwtkTogether implements NotificationCenter.NotificationCenterDelegate {

    private static final String PREFIX = "https://github.com/doxmev/MWTKgram#together=";
    private static final int INVITE_FRESH_SECONDS = 10 * 60;
    private static final long SEEK_JUMP_MS = 2500;

    private static MwtkTogether instance;

    // Session.
    private int account = -1;
    private long peer;
    private String id;
    private boolean host;
    private boolean active;

    // Host.
    private int control;
    private int shared;
    private long sharingDocument;
    private String lastTrack;
    private boolean lastPlaying;
    private long lastPosition;
    private long lastPositionAt;
    private long lastEdit;
    private final Runnable flush = this::pushState;

    // Partner.
    private int playing;

    private static final class Payload {
        String type = "";
        String session = "";
        int msg;
        long position;
        int at;
        boolean on;
    }

    private MwtkTogether() {
    }

    /** Starts listening to messages of all accounts. Safe to call often. */
    public static void setup() {
        if (instance != null) {
            return;
        }
        instance = new MwtkTogether();
        for (int a = 0; a < UserConfig.MAX_ACCOUNT_COUNT; a++) {
            NotificationCenter.getInstance(a).addObserver(instance, NotificationCenter.didReceiveNewMessages);
            NotificationCenter.getInstance(a).addObserver(instance, NotificationCenter.replaceMessagesObjects);
            NotificationCenter.getInstance(a).addObserver(instance, NotificationCenter.messageReceivedByServer);
        }
    }

    public static boolean activeWith(long dialogId) {
        return instance != null && instance.id != null && instance.peer == dialogId;
    }

    // ------------------------------------------------------------ payload

    private static String encode(Payload p) {
        try {
            JSONObject o = new JSONObject();
            o.put("t", p.type);
            o.put("s", p.session);
            if ("state".equals(p.type)) {
                o.put("m", p.msg);
                o.put("p", p.position);
                o.put("a", p.at);
                o.put("on", p.on ? 1 : 0);
            }
            return PREFIX + Base64.encodeToString(o.toString().getBytes(StandardCharsets.UTF_8),
                Base64.URL_SAFE | Base64.NO_PADDING | Base64.NO_WRAP);
        } catch (Throwable e) {
            return PREFIX;
        }
    }

    private static Payload decode(MessageObject message) {
        if (message == null || message.messageOwner == null || message.messageOwner.entities == null) {
            return null;
        }
        for (TLRPC.MessageEntity entity : message.messageOwner.entities) {
            if (!(entity instanceof TLRPC.TL_messageEntityTextUrl) || entity.url == null || !entity.url.startsWith(PREFIX)) {
                continue;
            }
            try {
                byte[] data = Base64.decode(entity.url.substring(PREFIX.length()), Base64.URL_SAFE | Base64.NO_PADDING | Base64.NO_WRAP);
                JSONObject o = new JSONObject(new String(data, StandardCharsets.UTF_8));
                Payload p = new Payload();
                p.type = o.optString("t");
                p.session = o.optString("s");
                p.msg = (int) o.optLong("m");
                p.position = o.optLong("p");
                p.at = o.optInt("a");
                p.on = o.optInt("on") != 0;
                return p.type.isEmpty() || p.session.isEmpty() ? null : p;
            } catch (Throwable ignore) {
            }
        }
        return null;
    }

    private static Payload payload(String type, String session) {
        Payload p = new Payload();
        p.type = type;
        p.session = session;
        return p;
    }

    private static ArrayList<TLRPC.MessageEntity> entities(Payload p) {
        TLRPC.TL_messageEntityTextUrl link = new TLRPC.TL_messageEntityTextUrl();
        link.offset = 0;
        link.length = 2; // the 🎧 at the start
        link.url = encode(p);
        ArrayList<TLRPC.MessageEntity> result = new ArrayList<>();
        result.add(link);
        return result;
    }

    private static void send(int account, long peer, String text, Payload p) {
        SendMessagesHelper.SendMessageParams params = SendMessagesHelper.SendMessageParams.of(
            "🎧 " + text, peer, null, null, null, false, entities(p), null, null, true, 0, 0, null, false);
        SendMessagesHelper.getInstance(account).sendMessage(params);
    }

    private static void edit(int account, long peer, int messageId, String text, Payload p) {
        TLRPC.TL_messages_editMessage req = new TLRPC.TL_messages_editMessage();
        req.peer = MessagesController.getInstance(account).getInputPeer(peer);
        req.id = messageId;
        req.message = "🎧 " + text;
        req.flags |= 2048;
        req.entities = entities(p);
        req.flags |= 8;
        req.no_webpage = true;
        req.flags |= 2;
        ConnectionsManager.getInstance(account).sendRequest(req, (res, err) -> {
            if (res instanceof TLRPC.Updates) {
                MessagesController.getInstance(account).processUpdates((TLRPC.Updates) res, false);
            }
        });
    }

    private static String nameOf(int account, long dialogId) {
        TLRPC.User user = MessagesController.getInstance(account).getUser(dialogId);
        return user != null ? UserObject.getFirstName(user) : "Собеседник";
    }

    private void clear() {
        AndroidUtilities.cancelRunOnUIThread(flush);
        account = -1;
        peer = 0;
        id = null;
        host = false;
        active = false;
        control = 0;
        shared = 0;
        sharingDocument = 0;
        lastTrack = null;
        playing = 0;
    }

    // ------------------------------------------------------------ public

    public static void invite(BaseFragment fragment, long dialogId) {
        setup();
        end();
        MwtkTogether s = instance;
        s.account = fragment.getCurrentAccount();
        s.peer = dialogId;
        s.id = Long.toString(Math.abs(new Random().nextLong()), 36);
        s.host = true;
        s.active = false;
        send(s.account, dialogId, "Зову слушать музыку вместе. Нужен MWTKgram — он спросит, согласен ли ты.",
            payload("invite", s.id));
        MwtkUi.toast("Приглашение отправлено. Музыка пойдёт, когда " + nameOf(s.account, dialogId) + " примет его");
    }

    public static void end() {
        MwtkTogether s = instance;
        if (s == null || s.id == null) {
            return;
        }
        Payload p = payload("end", s.id);
        if (s.host && s.control != 0) {
            edit(s.account, s.peer, s.control, "Совместное прослушивание закончено", p);
        } else {
            send(s.account, s.peer, "Совместное прослушивание закончено", p);
        }
        s.clear();
    }

    /** Called often from the UI thread: the host follows its own player. */
    public static void tick() {
        MwtkTogether s = instance;
        if (s == null || !s.host || !s.active) {
            return;
        }
        MessageObject current = MediaController.getInstance().getPlayingMessageObject();
        if (current == null || !current.isMusic()) {
            return;
        }
        long now = System.currentTimeMillis();
        String track = current.getDialogId() + "_" + current.getId();
        boolean playingNow = !MediaController.getInstance().isMessagePaused();
        long position = MediaController.getInstance().getCurrentPosition();
        boolean changed = !track.equals(s.lastTrack) || playingNow != s.lastPlaying;
        if (!changed && playingNow && s.lastPositionAt != 0) {
            long expected = s.lastPosition + (now - s.lastPositionAt);
            changed = Math.abs(position - expected) > SEEK_JUMP_MS;
        }
        s.lastTrack = track;
        s.lastPlaying = playingNow;
        s.lastPosition = position;
        s.lastPositionAt = now;
        if (changed) {
            s.schedule();
        }
    }

    private void schedule() {
        AndroidUtilities.cancelRunOnUIThread(flush);
        // Not more often than once a second and a half: Telegram limits edits.
        long since = System.currentTimeMillis() - lastEdit;
        AndroidUtilities.runOnUIThread(flush, Math.max(500, 1500 - since));
    }

    // ------------------------------------------------------------ host

    private void pushState() {
        if (!host || !active || control == 0) {
            return;
        }
        MessageObject current = MediaController.getInstance().getPlayingMessageObject();
        if (current == null || !current.isMusic() || current.getDocument() == null) {
            return;
        }
        // The song must be a message in this chat for the partner to play it.
        if (current.getDialogId() == peer && current.getId() > 0) {
            shared = current.getId();
        } else if (sharingDocument != current.getDocument().id || shared == 0) {
            if (sharingDocument != current.getDocument().id && current.getDocument() instanceof TLRPC.TL_document) {
                sharingDocument = current.getDocument().id;
                shared = 0;
                SendMessagesHelper.SendMessageParams params = SendMessagesHelper.SendMessageParams.of(
                    (TLRPC.TL_document) current.getDocument(), null, null, peer, null, null, null, null, null, null,
                    true, 0, 0, 0, null, null, false);
                SendMessagesHelper.getInstance(account).sendMessage(params);
            }
            return; // continues when the server gives the song its id
        }
        Payload p = payload("state", id);
        p.msg = shared;
        p.position = MediaController.getInstance().getCurrentPosition();
        p.at = ConnectionsManager.getInstance(account).getCurrentTime();
        p.on = !MediaController.getInstance().isMessagePaused();
        String title = (current.getMusicAuthor() == null || current.getMusicAuthor().isEmpty() ? "" : current.getMusicAuthor() + " — ")
            + current.getMusicTitle();
        lastEdit = System.currentTimeMillis();
        edit(account, peer, control, "Слушаем вместе: " + title + (p.on ? " ▶" : " ⏸"), p);
    }

    // ------------------------------------------------------------ partner

    private void apply(Payload p) {
        MessageObject cached = null;
        MessageObject current = MediaController.getInstance().getPlayingMessageObject();
        if (current != null && current.getDialogId() == peer && current.getId() == p.msg) {
            cached = current;
        }
        if (cached != null) {
            playAt(cached, p);
            return;
        }
        TLRPC.TL_messages_getMessages req = new TLRPC.TL_messages_getMessages();
        req.id.add(p.msg);
        final int acc = account;
        ConnectionsManager.getInstance(acc).sendRequest(req, (res, err) -> AndroidUtilities.runOnUIThread(() -> {
            if (!(res instanceof TLRPC.messages_Messages) || !active || host) {
                return;
            }
            TLRPC.messages_Messages r = (TLRPC.messages_Messages) res;
            MessagesController.getInstance(acc).putUsers(r.users, false);
            MessagesController.getInstance(acc).putChats(r.chats, false);
            for (TLRPC.Message m : r.messages) {
                MessageObject object = new MessageObject(acc, m, false, true);
                if (object.isMusic()) {
                    playAt(object, p);
                    return;
                }
            }
        }));
    }

    private void playAt(MessageObject message, Payload p) {
        MediaController media = MediaController.getInstance();
        MessageObject current = media.getPlayingMessageObject();
        boolean same = current != null && current.getDialogId() == message.getDialogId() && current.getId() == message.getId();
        if (!same) {
            ArrayList<MessageObject> list = new ArrayList<>();
            list.add(message);
            media.setPlaylist(list, message, 0);
        }
        playing = message.getId();
        if (!p.on) {
            AndroidUtilities.runOnUIThread(() -> {
                MessageObject now = media.getPlayingMessageObject();
                if (now != null && !media.isMessagePaused()) {
                    media.pauseMessage(now);
                }
            }, 400);
            return;
        }
        AndroidUtilities.runOnUIThread(() -> {
            if (!active || playing != message.getId()) {
                return;
            }
            MessageObject now = media.getPlayingMessageObject();
            if (now == null || now.getId() != message.getId()) {
                return;
            }
            long passed = Math.max(0, ConnectionsManager.getInstance(account).getCurrentTime() - p.at) * 1000L + 700;
            media.seekToProgressMs(now, p.position + passed);
            if (media.isMessagePaused()) {
                media.playMessage(now);
            }
        }, 700);
    }

    private void ask(int acc, long dialogId, String session) {
        BaseFragment fragment = LaunchActivity.getLastFragment();
        if (fragment == null || fragment.getParentActivity() == null) {
            return;
        }
        String name = nameOf(acc, dialogId);
        AlertDialog.Builder builder = new AlertDialog.Builder(fragment.getParentActivity());
        builder.setTitle("Слушать вместе?");
        builder.setMessage(name + " зовёт слушать музыку вместе.\n\n"
            + "Если согласишься, у тебя будет играть то же, что включает " + name + ": песня, пауза и перемотка. "
            + "Выйти можно в любой момент — в меню чата (⋮) → «Закончить слушать вместе».");
        builder.setPositiveButton("Принять", (d, w) -> {
            clear();
            account = acc;
            peer = dialogId;
            id = session;
            host = false;
            active = true;
            send(acc, dialogId, "Согласен слушать вместе", payload("accept", session));
            MwtkUi.toast("Ждём, что включит " + name + "…");
        });
        builder.setNegativeButton("Отклонить", (d, w) ->
            send(acc, dialogId, "Не сейчас", payload("decline", session)));
        fragment.showDialog(builder.create());
    }

    private void handle(int acc, MessageObject message) {
        Payload p = decode(message);
        if (p == null) {
            return;
        }
        long dialogId = message.getDialogId();
        if (message.isOut()) {
            return;
        }
        if ("invite".equals(p.type)) {
            int age = ConnectionsManager.getInstance(acc).getCurrentTime() - message.messageOwner.date;
            if (age < INVITE_FRESH_SECONDS && dialogId > 0 && MwtkConfig.flag(MwtkConfig.TOGETHER_ALLOW, true)) {
                ask(acc, dialogId, p.session);
            }
            return;
        }
        if (acc != account || dialogId != peer || id == null || !id.equals(p.session)) {
            return;
        }
        String name = nameOf(acc, dialogId);
        if ("accept".equals(p.type) && host) {
            active = true;
            MwtkUi.toast(name + " слушает с тобой. Включай музыку!");
            lastTrack = null;
            schedule();
        } else if ("decline".equals(p.type) && host) {
            MwtkUi.toast(name + " сейчас не может слушать вместе");
            clear();
        } else if ("state".equals(p.type) && !host && active) {
            apply(p);
        } else if ("end".equals(p.type)) {
            MwtkUi.toast("Совместное прослушивание закончено");
            clear();
        }
    }

    @Override
    @SuppressWarnings("unchecked")
    public void didReceivedNotification(int notification, int acc, Object... args) {
        if (notification == NotificationCenter.didReceiveNewMessages || notification == NotificationCenter.replaceMessagesObjects) {
            if (args.length < 2 || !(args[1] instanceof ArrayList)) {
                return;
            }
            if (notification == NotificationCenter.didReceiveNewMessages && args.length > 2 && args[2] instanceof Boolean && (Boolean) args[2]) {
                return; // scheduled
            }
            for (Object o : (ArrayList<Object>) args[1]) {
                if (o instanceof MessageObject) {
                    handle(acc, (MessageObject) o);
                }
            }
        } else if (notification == NotificationCenter.messageReceivedByServer) {
            // Our own messages got their ids: the invite to edit later, the shared song.
            if (args.length < 4 || !(args[2] instanceof TLRPC.Message) || !(args[3] instanceof Long)) {
                return;
            }
            TLRPC.Message m = (TLRPC.Message) args[2];
            if (acc != account || (Long) args[3] != peer || !host) {
                return;
            }
            MessageObject object = new MessageObject(acc, m, false, false);
            Payload p = decode(object);
            if (p != null && "invite".equals(p.type) && p.session.equals(id)) {
                control = m.id;
            } else if (m.media != null && m.media.document != null && m.media.document.id == sharingDocument) {
                shared = m.id;
                pushState();
            }
        }
    }
}
