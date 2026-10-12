# MWTKgram hooks into Telegram for Android sources.
# Executed by apply.py with `edit(relative, transform)` available.


def replace_once(old, new):
    def transform(text):
        count = text.count(old)
        if count != 1:
            raise SystemExit("patches.py: expected one match, found %d: %r" % (count, old[:80]))
        return text.replace(old, new)
    return transform


ONLINE = "user != null && !user.bot && isOnline()"
REGULAR = "currentDialogFolderId == 0 && !isTopic"

# First start: dark theme with the red accent.
edit("TMessagesProj/src/main/java/org/telegram/messenger/ApplicationLoader.java", replace_once(
    "        super.onCreate();\n",
    "        super.onCreate();\n"
    "        org.telegram.mwtk.MwtkConfig.applyFirstRunDefaults(applicationContext);\n"))

# Chat list backdrop with floating labels.
edit("TMessagesProj/src/main/java/org/telegram/ui/DialogsActivity.java", replace_once(
    "        fragmentView = contentView;\n",
    "        fragmentView = contentView;\n"
    "        org.telegram.mwtk.MwtkDialogs.attachBackground(contentView, initialDialogsType == DIALOGS_TYPE_DEFAULT && folderId == 0 && !onlySelect);\n"))

DIALOG_CELL = "TMessagesProj/src/main/java/org/telegram/ui/Cells/DialogCell.java"

# Translucent rounded card under each chat row.
edit(DIALOG_CELL, replace_once(
    "        canvas.save();\n"
    "        canvas.translate(0, -rightFragmentOffset * rightFragmentOpenedProgress);\n",
    "        org.telegram.mwtk.MwtkDialogs.drawCard(canvas, getMeasuredWidth(), getMeasuredHeight(), " + ONLINE + ", " + REGULAR + ");\n"
    "        canvas.save();\n"
    "        canvas.translate(0, -rightFragmentOffset * rightFragmentOpenedProgress);\n"))

# Soft halo behind the avatar.
edit(DIALOG_CELL, replace_once(
    "        if (drawAvatar && (!(isTopic && forumTopic != null && forumTopic.id == 1) || archivedChatsDrawable == null || !archivedChatsDrawable.isDraw())) {\n",
    "        if (drawAvatar) {\n"
    "            org.telegram.mwtk.MwtkDialogs.drawHalo(canvas, storyParams.originalAvatarRect, " + ONLINE + ", " + REGULAR + ");\n"
    "        }\n"
    "        if (drawAvatar && (!(isTopic && forumTopic != null && forumTopic.id == 1) || archivedChatsDrawable == null || !archivedChatsDrawable.isDraw())) {\n"))

# Rounded-tile avatars for online users.
edit(DIALOG_CELL, replace_once(
    "                avatarRadius = dp(28);\n",
    "                avatarRadius = org.telegram.mwtk.MwtkDialogs.avatarRadius(dp(28), " + ONLINE + ");\n"))

# The online dot can be hidden, and can pulse.
edit(DIALOG_CELL, replace_once(
    "                if (isOnline || onlineProgress != 0) {\n",
    "                if ((isOnline || onlineProgress != 0) && org.telegram.mwtk.MwtkDialogs.showOnlineDot()) {\n"))
edit(DIALOG_CELL, replace_once(
    "                    canvas.drawCircle(left, top, dp(5) * onlineProgress, Theme.dialogs_onlineCirclePaint);\n",
    "                    canvas.drawCircle(left, top, dp(5) * onlineProgress, Theme.dialogs_onlineCirclePaint);\n"
    "                    if (org.telegram.mwtk.MwtkDialogs.drawPulse(canvas, left, top, onlineProgress)) {\n"
    "                        needInvalidate = true;\n"
    "                    }\n"))

# Settings → MWTKgram.
SETTINGS = "TMessagesProj/src/main/java/org/telegram/ui/SettingsActivity.java"
edit(SETTINGS, replace_once(
    "        items.add(SettingCell.Factory.of(1, IconBackgroundColors.BLUE.top,",
    "        items.add(SettingCell.Factory.of(777, IconBackgroundColors.RED.top, IconBackgroundColors.RED.bottom, R.drawable.settings_power, \"MWTKgram\", \"Тема, эффекты, встряхивание\"));\n"
    "        items.add(SettingCell.Factory.of(1, IconBackgroundColors.BLUE.top,"))
edit(SETTINGS, replace_once(
    "        switch (item.id) {\n"
    "            case 1:\n"
    "                presentSettingFragment(new UserInfoActivity());\n",
    "        switch (item.id) {\n"
    "            case 777:\n"
    "                presentSettingFragment(new org.telegram.mwtk.MwtkSettingsActivity());\n"
    "                break;\n"
    "            case 1:\n"
    "                presentSettingFragment(new UserInfoActivity());\n"))

# Shake and tilt only while the app is on screen.
LAUNCH = "TMessagesProj/src/main/java/org/telegram/ui/LaunchActivity.java"
edit(LAUNCH, replace_once(
    "        super.onPause();\n"
    "        isResumed = false;\n",
    "        super.onPause();\n"
    "        org.telegram.mwtk.MwtkMotion.get().pause();\n"
    "        isResumed = false;\n"))
edit(LAUNCH, replace_once(
    "        super.onResume();\n"
    "        isResumed = true;\n",
    "        super.onResume();\n"
    "        org.telegram.mwtk.MwtkMotion.get().resume();\n"
    "        isResumed = true;\n"))
