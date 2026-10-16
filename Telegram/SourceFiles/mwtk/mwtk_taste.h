/*
This file is part of MWTKgram, a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Mwtk::Taste {

// What you listen to, what you skip and what you dislike. Feeds «Мой
// микс» the way VK / Spotify mixes work: by your whole listening, not
// only by the song that plays now.

// Starts recording plays from the music player.
void Setup();

void Dislike(const QString &performer, const QString &title);

[[nodiscard]] bool IsDisliked(const QString &performer, const QString &title);
[[nodiscard]] bool ArtistBanned(const QString &performer);
[[nodiscard]] bool PlayedRecently(
	const QString &performer,
	const QString &title,
	int days);

// Artists to build a mix around, picked by weight (more listening, more
// weight; skips and dislikes lower it). `extra` adds artists with a
// starting weight (for example the ones from your playlists).
[[nodiscard]] std::vector<QString> SeedArtists(
	int count,
	const std::vector<QString> &extra);

} // namespace Mwtk::Taste
