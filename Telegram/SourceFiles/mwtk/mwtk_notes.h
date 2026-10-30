/*
This file is part of MWTKgram, a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <rpl/producer.h>

class PeerData;

namespace Window {
class SessionController;
} // namespace Window

namespace Mwtk::Notes {

[[nodiscard]] QString Get(not_null<PeerData*> peer);
[[nodiscard]] rpl::producer<QString> Value(not_null<PeerData*> peer);
void Set(not_null<PeerData*> peer, const QString &text);

void ShowEditBox(
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer);

} // namespace Mwtk::Notes
