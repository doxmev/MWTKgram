/*
This file is part of MWTKgram, a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class PeerData;
class QJsonObject;

namespace Window {
class SessionController;
} // namespace Window

namespace Mwtk::Names {

// Local "for me" names of chats. Stored on this computer only.
[[nodiscard]] QString Get(not_null<const PeerData*> peer);
void Set(not_null<PeerData*> peer, const QString &name);

// Called from PeerData::updateNameDelayed: remembers the real name and
// returns the name to show.
[[nodiscard]] QString Resolve(
	not_null<const PeerData*> peer,
	const QString &serverName);

[[nodiscard]] QString Original(not_null<const PeerData*> peer);

void ShowEditBox(
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer);

// All names as JSON (for the PC <-> phone sync) and back.
[[nodiscard]] QJsonObject Export();
void Import(const QJsonObject &object);

} // namespace Mwtk::Names
