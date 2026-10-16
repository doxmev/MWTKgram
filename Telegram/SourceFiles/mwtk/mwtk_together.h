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

namespace Mwtk::Together {

// «Слушать вместе»: you invite a person in a private chat, their MWTKgram
// asks them first, and only after «Принять» their player follows yours.
// The state travels in one message that you edit (no chat spam, no
// notifications), so it works between PC and Android.

void Setup();

[[nodiscard]] bool ActiveWith(not_null<PeerData*> peer);
void Invite(
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer);
void End();

} // namespace Mwtk::Together
