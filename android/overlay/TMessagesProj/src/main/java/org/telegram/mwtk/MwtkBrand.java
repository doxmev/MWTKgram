package org.telegram.mwtk;

import android.util.SparseBooleanArray;

import org.telegram.messenger.R;

/** Strings that name the app itself say "MWTKgram"; service ones stay. */
public final class MwtkBrand {

    private static SparseBooleanArray keys;

    private MwtkBrand() {
    }

    private static SparseBooleanArray keys() {
        if (keys == null) {
            SparseBooleanArray result = new SparseBooleanArray();
            int[] ids = {
                R.string.AppName,
                R.string.AppNameBeta,
                R.string.NoChats,
                R.string.NotificationsPermissionAlertSubtitle,
                R.string.ImportImportingInfo,
                R.string.NotificationHiddenName,
                R.string.NotificationHiddenChatName,
                R.string.TelegramVersion,
                R.string.ClearingCacheDescription,
                R.string.ScreenCaptureAlert,
                R.string.StorageUsageTelegram,
                R.string.StorageUsageTelegramLess,
                R.string.ClearTelegramCache,
                R.string.TelegramCacheSize,
                R.string.LocalDatabaseSize,
                R.string.Page1Title,
                R.string.ProfilePopupNotificationInfo,
                R.string.LowDiskSpaceMessage,
                R.string.PermissionStorageWithHint,
                R.string.PermissionNoAudioWithHint,
                R.string.PermissionNoAudioVideoWithHint,
                R.string.PermissionNoCameraWithHint,
                R.string.PermissionNoCameraMicVideo,
                R.string.PermissionNoLocation,
                R.string.PermissionNoLocationFriends,
                R.string.PermissionNoLocationNavigation,
                R.string.PermissionNoStorageAvatar,
                R.string.PermissionDrawAboveOtherApps,
            };
            for (int id : ids) {
                result.put(id, true);
            }
            keys = result;
        }
        return keys;
    }

    public static String apply(int res, String value) {
        if (value == null || res == 0 || !keys().get(res, false)) {
            return value;
        }
        if (res == R.string.TelegramVersion) {
            return value.replace("Telegram for Android", "MWTKgram").replace("Telegram", "MWTKgram");
        }
        return value.replace("Telegram Beta", "MWTKgram").replace("Telegram", "MWTKgram");
    }
}
