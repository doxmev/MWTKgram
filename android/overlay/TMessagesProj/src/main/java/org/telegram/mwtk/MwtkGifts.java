package org.telegram.mwtk;

import org.telegram.messenger.DialogObject;
import org.telegram.messenger.UserConfig;
import org.telegram.tgnet.tl.TL_stars;
import org.telegram.ui.Stars.StarsController;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;

/**
 * Archive of gifts: archived gifts disappear from your own profile in this
 * app (the official Telegram can only hide them from other people).
 */
public final class MwtkGifts {

    private static boolean showArchived;
    private static final HashMap<StarsController.GiftsList, ArrayList<TL_stars.SavedStarGift>> stash = new HashMap<>();

    private MwtkGifts() {
    }

    private static String key(TL_stars.SavedStarGift gift) {
        long from = gift.from_id != null ? DialogObject.getPeerDialogId(gift.from_id) : 0;
        long id = gift.gift != null ? gift.gift.id : 0;
        return id + "_" + gift.date + "_" + from;
    }

    private static ArrayList<String> keys() {
        String value = MwtkConfig.text(MwtkConfig.GIFTS_ARCHIVED, "");
        return value.isEmpty() ? new ArrayList<>() : new ArrayList<>(Arrays.asList(value.split(",")));
    }

    public static boolean isArchived(TL_stars.SavedStarGift gift) {
        return gift != null && keys().contains(key(gift));
    }

    public static boolean isMine(int account, long dialogId) {
        return dialogId == 0 || dialogId == UserConfig.getInstance(account).getClientUserId();
    }

    /** Called with every loaded page of your gifts: archived ones are put aside. */
    public static ArrayList<TL_stars.SavedStarGift> filter(StarsController.GiftsList list, ArrayList<TL_stars.SavedStarGift> page) {
        if (showArchived || list == null || !isMine(list.currentAccount, list.dialogId)) {
            return page;
        }
        ArrayList<String> archived = keys();
        if (archived.isEmpty()) {
            return page;
        }
        ArrayList<TL_stars.SavedStarGift> result = new ArrayList<>();
        ArrayList<TL_stars.SavedStarGift> aside = stash.get(list);
        if (aside == null) {
            aside = new ArrayList<>();
            stash.put(list, aside);
        }
        for (TL_stars.SavedStarGift gift : page) {
            if (archived.contains(key(gift))) {
                aside.add(gift);
            } else {
                result.add(gift);
            }
        }
        return result;
    }

    public static void toggle(Object anyList, TL_stars.SavedStarGift gift) {
        ArrayList<String> archived = keys();
        String key = key(gift);
        boolean archive = !archived.contains(key);
        archived.remove(key);
        if (archive) {
            archived.add(key);
        }
        MwtkConfig.setText(MwtkConfig.GIFTS_ARCHIVED, String.join(",", archived));
        if (anyList instanceof StarsController.GiftsList && archive && !showArchived) {
            StarsController.GiftsList list = (StarsController.GiftsList) anyList;
            list.gifts.remove(gift);
            ArrayList<TL_stars.SavedStarGift> aside = stash.get(list);
            if (aside == null) {
                aside = new ArrayList<>();
                stash.put(list, aside);
            }
            aside.add(gift);
            list.notifyUpdate();
        }
        MwtkUi.toast(archive
            ? "Подарок в архиве. Посмотреть: зажми подарок → «Показать архив»"
            : "Подарок вернулся из архива");
    }

    public static boolean showingArchived() {
        return showArchived;
    }

    public static void toggleShowArchived(Object anyList) {
        showArchived = !showArchived;
        if (!(anyList instanceof StarsController.GiftsList)) {
            return;
        }
        StarsController.GiftsList list = (StarsController.GiftsList) anyList;
        if (showArchived) {
            ArrayList<TL_stars.SavedStarGift> aside = stash.remove(list);
            if (aside != null) {
                list.gifts.addAll(aside);
            }
        } else {
            ArrayList<TL_stars.SavedStarGift> aside = new ArrayList<>();
            ArrayList<String> archived = keys();
            for (TL_stars.SavedStarGift gift : new ArrayList<>(list.gifts)) {
                if (archived.contains(key(gift))) {
                    list.gifts.remove(gift);
                    aside.add(gift);
                }
            }
            stash.put(list, aside);
        }
        list.notifyUpdate();
    }
}
