# MWTKgram hooks into Telegram for Android sources.
# Executed by apply.py with `edit(relative, transform)` available.


def replace_once(old, new):
    def transform(text):
        count = text.count(old)
        if count != 1:
            raise SystemExit("patches.py: expected one match, found %d: %r" % (count, old[:80]))
        return text.replace(old, new)
    return transform


def replace_all(old, new, expected=None):
    def transform(text):
        count = text.count(old)
        if count == 0 or (expected is not None and count != expected):
            raise SystemExit("patches.py: expected %s matches, found %d: %r" % (expected, count, old[:80]))
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

# ---------------------------------------------------------------- 1.0

# Whole-app transparency: background colors get the MWTK alpha.
edit("TMessagesProj/src/main/java/org/telegram/ui/ActionBar/Theme.java", replace_once(
    "    public static int getColor(int key, boolean[] isDefault, boolean ignoreAnimation) {\n",
    "    public static int getColor(int key, boolean[] isDefault, boolean ignoreAnimation) {\n"
    "        return org.telegram.mwtk.MwtkTheme.adjust(key, getColorMwtkOriginal(key, isDefault, ignoreAnimation));\n"
    "    }\n"
    "\n"
    "    private static int getColorMwtkOriginal(int key, boolean[] isDefault, boolean ignoreAnimation) {\n"))

# "Telegram" -> "MWTKgram" where the string names the app.
edit("TMessagesProj/src/main/java/org/telegram/messenger/LocaleController.java", replace_once(
    "        final String value = getStringV2(key, res, fallback);\n",
    "        final String value = org.telegram.mwtk.MwtkBrand.apply(res, getStringV2(key, res, fallback));\n"))

# Tablet mode: a live MWTK scene on the free right side.
edit(LAUNCH, replace_once(
    "            launchLayout.addView(backgroundTablet, LayoutHelper.createRelative(LayoutHelper.MATCH_PARENT, LayoutHelper.MATCH_PARENT));\n",
    "            launchLayout.addView(backgroundTablet, LayoutHelper.createRelative(LayoutHelper.MATCH_PARENT, LayoutHelper.MATCH_PARENT));\n"
    "            org.telegram.mwtk.MwtkTablet.attach(backgroundTablet);\n"))

# App-wide layers: backdrop, flying lyrics, hologram banners, fly-by.
edit(LAUNCH, replace_once(
    "        frameLayout.addView(drawerLayoutContainer, LayoutHelper.createFrame(LayoutHelper.MATCH_PARENT, LayoutHelper.MATCH_PARENT));\n",
    "        frameLayout.addView(drawerLayoutContainer, LayoutHelper.createFrame(LayoutHelper.MATCH_PARENT, LayoutHelper.MATCH_PARENT));\n"
    "        org.telegram.mwtk.MwtkOverlay.attach(frameLayout, drawerLayoutContainer);\n"))

# Hidden chats are filtered out of every chat list.
DIALOGS = "TMessagesProj/src/main/java/org/telegram/ui/DialogsActivity.java"
edit(DIALOGS, replace_once(
    "    public ArrayList<TLRPC.Dialog> getDialogsArray(int currentAccount, int dialogsType, int folderId, boolean frozen) {\n",
    "    public ArrayList<TLRPC.Dialog> getDialogsArray(int currentAccount, int dialogsType, int folderId, boolean frozen) {\n"
    "        return org.telegram.mwtk.MwtkHidden.filter(getDialogsArrayMwtkOriginal(currentAccount, dialogsType, folderId, frozen));\n"
    "    }\n"
    "\n"
    "    @NonNull\n"
    "    private ArrayList<TLRPC.Dialog> getDialogsArrayMwtkOriginal(int currentAccount, int dialogsType, int folderId, boolean frozen) {\n"))

# Floating chats: long press on the title.
edit(DIALOGS, replace_once(
    "        org.telegram.mwtk.MwtkDialogs.attachBackground(contentView, initialDialogsType == DIALOGS_TYPE_DEFAULT && folderId == 0 && !onlySelect);\n",
    "        org.telegram.mwtk.MwtkDialogs.attachBackground(contentView, initialDialogsType == DIALOGS_TYPE_DEFAULT && folderId == 0 && !onlySelect);\n"
    "        if (initialDialogsType == DIALOGS_TYPE_DEFAULT && folderId == 0 && !onlySelect) {\n"
    "            org.telegram.mwtk.MwtkFloating.attach(this, contentView, null);\n"
    "        }\n"))

# Deleted messages stay in the chat (server deletions are only marked).
CONTROLLER = "TMessagesProj/src/main/java/org/telegram/messenger/MessagesController.java"
edit(CONTROLLER, replace_once(
    "                    deletedMessages.put(0, arrayList);\n"
    "                }\n"
    "                arrayList.addAll(update.messages);",
    "                    deletedMessages.put(0, arrayList);\n"
    "                }\n"
    "                arrayList.addAll(org.telegram.mwtk.MwtkDeleted.filter(currentAccount, 0, update.messages));"))
edit(CONTROLLER, replace_once(
    "                    deletedMessages.put(dialogId, arrayList);\n"
    "                }\n"
    "                arrayList.addAll(update.messages);",
    "                    deletedMessages.put(dialogId, arrayList);\n"
    "                }\n"
    "                arrayList.addAll(org.telegram.mwtk.MwtkDeleted.filter(currentAccount, dialogId, update.messages));"))
edit("TMessagesProj/src/main/java/org/telegram/ui/Cells/ChatMessageCell.java", replace_once(
    "    public void drawTime(Canvas canvas, float alpha, boolean fromParent) {\n",
    "    public void drawTime(Canvas canvas, float alpha, boolean fromParent) {\n"
    "        if (!fromParent) {\n"
    "            org.telegram.mwtk.MwtkDeleted.drawMarker(canvas, currentMessageObject, backgroundDrawableRight, backgroundDrawableTop, alpha);\n"
    "        }\n"))

# Chat menu (hide, own name, note, deleted) and message options.
CHAT = "TMessagesProj/src/main/java/org/telegram/ui/ChatActivity.java"
# Effects inside the chat, over the wallpaper and under the messages.
edit(CHAT, replace_once(
    "        contentView.addView(chatListView, LayoutHelper.createFrame(LayoutHelper.MATCH_PARENT, LayoutHelper.MATCH_PARENT));\n",
    "        contentView.addView(chatListView, LayoutHelper.createFrame(LayoutHelper.MATCH_PARENT, LayoutHelper.MATCH_PARENT));\n"
    "        org.telegram.mwtk.MwtkChatFx.attach(contentView, chatListView);\n"))
edit(CHAT, replace_once(
    "            if (!isTopic && !ChatObject.isMonoForum(currentChat)) {\n"
    "                clearHistoryItem = headerItem.lazilyAddSubItem(clear_history, R.drawable.msg_clear,",
    "            org.telegram.mwtk.MwtkChatMenu.addItems(headerItem, this);\n"
    "            if (!isTopic && !ChatObject.isMonoForum(currentChat)) {\n"
    "                clearHistoryItem = headerItem.lazilyAddSubItem(clear_history, R.drawable.msg_clear,"))
edit(CHAT, replace_once(
    "                } else if (id == chat_enc_timer) {",
    "                } else if (org.telegram.mwtk.MwtkChatMenu.handle(ChatActivity.this, id)) {\n"
    "                    // MWTK menu item.\n"
    "                } else if (id == chat_enc_timer) {"))
edit(CHAT, replace_once(
    "                if (canForward) {\n"
    "                    items.add(LocaleController.getString(R.string.Forward));\n"
    "                    options.add(OPTION_FORWARD);\n"
    "                    icons.add(R.drawable.msg_forward);\n"
    "                }",
    "                if (canForward) {\n"
    "                    items.add(LocaleController.getString(R.string.Forward));\n"
    "                    options.add(OPTION_FORWARD);\n"
    "                    icons.add(R.drawable.msg_forward);\n"
    "                }\n"
    "                org.telegram.mwtk.MwtkChatMenu.addMessageOptions(selectedObject, items, options, icons);"))
edit(CHAT, replace_once(
    "        boolean preserveDim = false;\n"
    "        switch (option) {\n"
    "            case OPTION_RETRY: {",
    "        boolean preserveDim = false;\n"
    "        switch (option) {\n"
    "            case org.telegram.mwtk.MwtkChatMenu.OPTION_REMIND:\n"
    "            case org.telegram.mwtk.MwtkChatMenu.OPTION_PLAYLIST:\n"
    "            case org.telegram.mwtk.MwtkChatMenu.OPTION_HIDE_CHATS:\n"
    "            case org.telegram.mwtk.MwtkChatMenu.OPTION_SYNC_LYRICS:\n"
    "            case org.telegram.mwtk.MwtkChatMenu.OPTION_SHIFT_LYRICS:\n"
    "            case org.telegram.mwtk.MwtkChatMenu.OPTION_FIND_LYRICS: {\n"
    "                org.telegram.mwtk.MwtkChatMenu.handleMessageOption(ChatActivity.this, option, selectedObject);\n"
    "                break;\n"
    "            }\n"
    "            case OPTION_RETRY: {"))
edit(CHAT, replace_all(
    "AndroidUtilities.removeDiacritics(currentChat.title)",
    "AndroidUtilities.removeDiacritics(org.telegram.mwtk.MwtkNames.chat(currentChat))"))
edit(CHAT, replace_all(
    "AndroidUtilities.removeDiacritics(chat.title)",
    "AndroidUtilities.removeDiacritics(org.telegram.mwtk.MwtkNames.chat(chat))"))

# Quick phrases and the send effect.
ENTER = "TMessagesProj/src/main/java/org/telegram/ui/Components/ChatActivityEnterView.java"
edit(ENTER, replace_once(
    "        messageEditText.addTextChangedListener(new EditTextSuggestionsFix());\n",
    "        messageEditText.addTextChangedListener(new EditTextSuggestionsFix());\n"
    "        org.telegram.mwtk.MwtkPhrases.attach(parentFragment, messageEditText);\n"))
edit(ENTER, replace_once(
    "    public boolean processSendingText(CharSequence text, boolean notify, int scheduleDate, int scheduleRepeatPeriod, long payStars) {\n",
    "    public boolean processSendingText(CharSequence text, boolean notify, int scheduleDate, int scheduleRepeatPeriod, long payStars) {\n"
    "        org.telegram.mwtk.MwtkOverlay.onSend();\n"))

# Local "for me" names.
edit("TMessagesProj/src/main/java/org/telegram/messenger/UserObject.java", replace_once(
    "    public static String getUserName(TLRPC.User user) {\n"
    "        if (user == null || isDeleted(user)) {",
    "    public static String getUserName(TLRPC.User user) {\n"
    "        final String mwtkName = org.telegram.mwtk.MwtkNames.user(user);\n"
    "        if (mwtkName != null) {\n"
    "            return mwtkName;\n"
    "        }\n"
    "        if (user == null || isDeleted(user)) {"))
edit(DIALOG_CELL, replace_all(
    "AndroidUtilities.escape(chat.title)",
    "AndroidUtilities.escape(org.telegram.mwtk.MwtkNames.chat(chat))", 2))

# Hologram over the profile photo.
edit("TMessagesProj/src/main/java/org/telegram/ui/ProfileActivity.java", replace_once(
    "        avatarContainer.addView(avatarImage, LayoutHelper.createFrame(LayoutHelper.MATCH_PARENT, LayoutHelper.MATCH_PARENT));\n",
    "        avatarContainer.addView(avatarImage, LayoutHelper.createFrame(LayoutHelper.MATCH_PARENT, LayoutHelper.MATCH_PARENT));\n"
    "        org.telegram.mwtk.MwtkHolo.attach(avatarContainer, avatarImage);\n"))

# MWTK theme and sync files open in the app.
edit("TMessagesProj/src/main/java/org/telegram/messenger/AndroidUtilities.java", replace_once(
    "        String mimeType = message.type == MessageObject.TYPE_FILE || message.type == MessageObject.TYPE_TEXT ? message.getMimeType() : null;\n"
    "        return openForView(f, message.getFileName(), mimeType, activity, resourcesProvider, restrict);",
    "        if (org.telegram.mwtk.MwtkShare.handleOpen(activity, f, message.getFileName())) {\n"
    "            return true;\n"
    "        }\n"
    "        String mimeType = message.type == MessageObject.TYPE_FILE || message.type == MessageObject.TYPE_TEXT ? message.getMimeType() : null;\n"
    "        return openForView(f, message.getFileName(), mimeType, activity, resourcesProvider, restrict);"))
