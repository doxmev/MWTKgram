/*
This file is part of MWTKgram, a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class PeerData;

namespace Window {
class SessionController;
} // namespace Window

namespace Mwtk::Hidden {

[[nodiscard]] bool IsHidden(not_null<const PeerData*> peer);
void SetHidden(not_null<PeerData*> peer, bool hidden);

// Hides or shows the chat, asks to set a password first if there is none.
void Toggle(
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer);

// Asks for the password and shows the hidden chats.
void Open(not_null<Window::SessionController*> controller);

// Installs the Ctrl+Shift+L hotkey.
void Setup();

} // namespace Mwtk::Hidden
