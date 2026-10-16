package org.telegram.mwtk;

import android.content.Context;

import org.telegram.messenger.MessageObject;
import org.telegram.messenger.R;
import org.telegram.messenger.SendMessagesHelper;
import org.telegram.messenger.UserConfig;
import org.telegram.tgnet.ConnectionsManager;
import org.telegram.ui.ActionBar.ActionBarMenuItem;
import org.telegram.ui.ActionBar.AlertDialog;
import org.telegram.ui.ChatActivity;

import java.util.ArrayList;
import java.util.Calendar;

/** MWTK items in the chat menu and in message options. */
public final class MwtkChatMenu {

    public static final int HIDE = 7701;
    public static final int RENAME = 7702;
    public static final int NOTE = 7703;
    public static final int DELETED = 7704;
    public static final int TOGETHER = 7705;

    public static final int OPTION_REMIND = 7801;
    public static final int OPTION_PLAYLIST = 7802;
    public static final int OPTION_HIDE_CHATS = 7803;
    public static final int OPTION_SYNC_LYRICS = 7804;
    public static final int OPTION_SHIFT_LYRICS = 7805;
    public static final int OPTION_FIND_LYRICS = 7806;

    private MwtkChatMenu() {
    }

    public static void addItems(ActionBarMenuItem headerItem, ChatActivity chat) {
        MwtkConfig.load();
        if (headerItem == null || chat == null || !MwtkConfig.effects) {
            return;
        }
        long dialogId = chat.getDialogId();
        headerItem.lazilyAddSubItem(HIDE, R.drawable.msg_mini_lock2,
            MwtkHidden.isHidden(dialogId) ? "Вернуть из скрытых (MWTK)" : "Скрыть чат (MWTK)");
        headerItem.lazilyAddSubItem(RENAME, R.drawable.msg_edit, "Своё название (MWTK)");
        headerItem.lazilyAddSubItem(NOTE, R.drawable.msg_info, "Заметка MWTK");
        headerItem.lazilyAddSubItem(DELETED, R.drawable.msg_delete, "Удалённые сообщения (MWTK)");
        if (dialogId > 0 && dialogId != org.telegram.messenger.UserConfig.getInstance(chat.getCurrentAccount()).getClientUserId()) {
            headerItem.lazilyAddSubItem(TOGETHER, R.drawable.msg_filled_data_music,
                MwtkTogether.activeWith(dialogId) ? "Закончить слушать вместе" : "Слушать музыку вместе (MWTK)");
        }
    }

    public static boolean handle(ChatActivity chat, int id) {
        long dialogId = chat.getDialogId();
        switch (id) {
            case HIDE:
                MwtkHidden.toggle(chat, dialogId);
                return true;
            case RENAME: {
                String current = MwtkNames.get(dialogId);
                MwtkUi.input(chat, "Своё название",
                    "Название видишь только ты, у остальных всё как было.",
                    current, "Название", false, false,
                    current != null ? "Вернуть" : null,
                    text -> {
                        MwtkNames.set(dialogId, text);
                        org.telegram.messenger.NotificationCenter.getInstance(chat.getCurrentAccount())
                            .postNotificationName(org.telegram.messenger.NotificationCenter.updateInterfaces,
                                org.telegram.messenger.MessagesController.UPDATE_MASK_NAME);
                    });
                return true;
            }
            case NOTE: {
                String current = MwtkNotes.get(dialogId);
                MwtkUi.input(chat, "Заметка MWTK",
                    "Хранится только на этом телефоне.",
                    current, "Что важно помнить об этом чате", true, false,
                    current.isEmpty() ? null : "Удалить",
                    text -> MwtkNotes.set(dialogId, text));
                return true;
            }
            case DELETED:
                showDeleted(chat, dialogId);
                return true;
            case TOGETHER:
                if (MwtkTogether.activeWith(dialogId)) {
                    MwtkTogether.end();
                } else {
                    MwtkTogether.invite(chat, dialogId);
                }
                return true;
        }
        return false;
    }

    private static void showDeleted(ChatActivity chat, long dialogId) {
        Context context = chat.getParentActivity();
        if (context == null) {
            return;
        }
        ArrayList<long[]> rows = MwtkDeleted.journal();
        ArrayList<long[]> mine = new ArrayList<>();
        int account = chat.getCurrentAccount();
        for (long[] row : rows) {
            boolean channelMatch = row[1] != 0 && row[1] == dialogId;
            boolean globalMatch = row[1] == 0 && dialogId > 0;
            if (row[0] == account && (channelMatch || globalMatch) && mine.size() < 200) {
                mine.add(row);
            }
        }
        if (mine.isEmpty()) {
            MwtkUi.toast("Удалённых сообщений здесь нет");
            return;
        }
        String[] items = new String[mine.size()];
        java.text.DateFormat format = java.text.DateFormat.getDateTimeInstance(java.text.DateFormat.SHORT, java.text.DateFormat.SHORT);
        for (int i = 0; i < mine.size(); i++) {
            items[i] = "Сообщение #" + mine.get(i)[2] + " · " + format.format(new java.util.Date(mine.get(i)[3] * 1000));
        }
        AlertDialog.Builder builder = new AlertDialog.Builder(context);
        builder.setTitle("Удалённые сообщения");
        builder.setItems(items, (dialog, which) -> chat.scrollToMessageId((int) mine.get(which)[2], 0, true, 0, true, 0));
        chat.showDialog(builder.create());
    }

    public static void addMessageOptions(MessageObject message, ArrayList<CharSequence> items, ArrayList<Integer> options, ArrayList<Integer> icons) {
        MwtkConfig.load();
        if (message == null || !MwtkConfig.effects || message.messageOwner == null || message.getId() <= 0) {
            return;
        }
        items.add("Напомнить (MWTK)");
        options.add(OPTION_REMIND);
        icons.add(R.drawable.msg_calendar2);
        if (message.isMusic()) {
            items.add("В плейлист MWTK");
            options.add(OPTION_PLAYLIST);
            icons.add(R.drawable.msg_fave);
            if (MwtkConfig.flag(MwtkConfig.LYRICS, true)) {
                items.add(MwtkConfig.flag(MwtkConfig.HIDE_CHATS, false)
                    ? "✓ Спрятать чаты (текст песни)"
                    : "Спрятать чаты (текст песни)");
                options.add(OPTION_HIDE_CHATS);
                icons.add(R.drawable.msg_viewintopic);
                items.add("Найти текст");
                options.add(OPTION_FIND_LYRICS);
                icons.add(R.drawable.msg_search);
                items.add("Написать / исправить текст");
                options.add(OPTION_SYNC_LYRICS);
                icons.add(R.drawable.msg_edit);
                items.add("Сдвиг текста песни");
                options.add(OPTION_SHIFT_LYRICS);
                icons.add(R.drawable.msg_speed);
            }
        }
    }

    public static void handleMessageOption(ChatActivity chat, int option, MessageObject message) {
        switch (option) {
            case OPTION_REMIND:
                remind(chat, message);
                break;
            case OPTION_PLAYLIST:
                MwtkPlaylists.showAddBox(chat, message);
                break;
            case OPTION_HIDE_CHATS:
                MwtkConfig.setFlag(MwtkConfig.HIDE_CHATS, !MwtkConfig.flag(MwtkConfig.HIDE_CHATS, false));
                break;
            case OPTION_SYNC_LYRICS:
                MwtkLyrics.showSyncEditor(chat, message);
                break;
            case OPTION_FIND_LYRICS:
                MwtkLyrics.showSearch(chat, message);
                break;
            case OPTION_SHIFT_LYRICS:
                MwtkLyrics.showOffsetBox(chat, message);
                break;
        }
    }

    private static void remind(ChatActivity chat, MessageObject message) {
        Context context = chat.getParentActivity();
        if (context == null) {
            return;
        }
        String[] items = { "Через час", "Вечером", "Завтра утром" };
        AlertDialog.Builder builder = new AlertDialog.Builder(context);
        builder.setTitle("Напомнить");
        builder.setItems(items, (dialog, which) -> {
            int now = ConnectionsManager.getInstance(chat.getCurrentAccount()).getCurrentTime();
            long when;
            Calendar calendar = Calendar.getInstance();
            if (which == 0) {
                when = now + 3600;
            } else if (which == 1) {
                calendar.set(Calendar.HOUR_OF_DAY, 20);
                calendar.set(Calendar.MINUTE, 0);
                calendar.set(Calendar.SECOND, 0);
                if (calendar.getTimeInMillis() - System.currentTimeMillis() < 30 * 60 * 1000) {
                    calendar.add(Calendar.DAY_OF_YEAR, 1);
                }
                when = calendar.getTimeInMillis() / 1000;
            } else {
                calendar.add(Calendar.DAY_OF_YEAR, 1);
                calendar.set(Calendar.HOUR_OF_DAY, 10);
                calendar.set(Calendar.MINUTE, 0);
                calendar.set(Calendar.SECOND, 0);
                when = calendar.getTimeInMillis() / 1000;
            }
            ArrayList<MessageObject> list = new ArrayList<>();
            list.add(message);
            long self = UserConfig.getInstance(chat.getCurrentAccount()).getClientUserId();
            SendMessagesHelper.getInstance(chat.getCurrentAccount()).sendMessage(
                list, self, false, false, true, (int) Math.max(when, now + 60), 0);
            MwtkUi.toast("Напомню: придёт в «Избранное» на всех устройствах");
        });
        chat.showDialog(builder.create());
    }
}
