/*
This file is part of MWTKgram, a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class HistoryItem;

namespace Ui {
class PopupMenu;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace Mwtk::Reminders {

// "Remind me" for a message: forwards it to Saved Messages as a scheduled
// message, so the reminder arrives on every device, the phone included.
void FillMenu(
	not_null<Ui::PopupMenu*> menu,
	not_null<Window::SessionController*> controller,
	FullMsgId itemId);

} // namespace Mwtk::Reminders
