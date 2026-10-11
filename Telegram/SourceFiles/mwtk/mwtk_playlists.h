/*
This file is part of MWTKgram, a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <rpl/producer.h>

class AudioMsgId;
class HistoryItem;

namespace Main {
class Session;
} // namespace Main

namespace Window {
class SessionController;
} // namespace Window

namespace Mwtk::Playlists {

struct Track {
	uint64 peer = 0;
	int64 msg = 0;
	QString title;
	QString performer;
	int duration = 0;
};

struct Playlist {
	QString name;
	std::vector<Track> tracks;
};

[[nodiscard]] const std::vector<Playlist> &List(
	not_null<Main::Session*> session);
[[nodiscard]] rpl::producer<> Changes();

int Create(not_null<Main::Session*> session, const QString &name);
void Rename(not_null<Main::Session*> session, int index, const QString &name);
void Remove(not_null<Main::Session*> session, int index);
bool AddTrack(
	not_null<Main::Session*> session,
	int index,
	not_null<HistoryItem*> item);
void RemoveTrack(not_null<Main::Session*> session, int index, int track);

void Play(
	not_null<Main::Session*> session,
	int index,
	int track,
	bool shuffle);

// Hooks for Media::Player::Instance.
[[nodiscard]] bool Owns(const AudioMsgId &current);
[[nodiscard]] bool HandleMove(const AudioMsgId &current, int delta);
[[nodiscard]] bool MoveAvailable(const AudioMsgId &current, int delta);

void ShowAddToPlaylistBox(
	not_null<Window::SessionController*> controller,
	FullMsgId itemId);
void ShowPlaylistsBox(not_null<Window::SessionController*> controller);

[[nodiscard]] QString FormatDuration(int seconds);

} // namespace Mwtk::Playlists
