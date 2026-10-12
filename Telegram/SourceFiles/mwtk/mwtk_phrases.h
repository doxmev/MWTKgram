/*
This file is part of MWTKgram, a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Ui {
class InputField;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace Mwtk::Phrases {

// Saved phrases, one per item.
[[nodiscard]] QStringList List();
void SetList(const QStringList &list);

// Typing a lone "/" in the message field offers the saved phrases.
void Attach(
	not_null<Ui::InputField*> field,
	not_null<Window::SessionController*> controller);

void ShowEditBox(not_null<Window::SessionController*> controller);

} // namespace Mwtk::Phrases
