/*
This file is part of MWTKgram, a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mwtk/mwtk_taste.h"

#include "base/random.h"
#include "base/unixtime.h"
#include "data/data_audio_msg_id.h"
#include "data/data_document.h"
#include "media/audio/media_audio.h"
#include "media/player/media_player_instance.h"
#include "settings.h"

#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QSaveFile>

#include <cmath>
#include <map>
#include <random>

namespace Mwtk::Taste {
namespace {

constexpr auto kMaxHistory = 600;
constexpr auto kSkipSeconds = 30;
constexpr auto kHalfLifeDays = 14.;

struct Play {
	QString performer;
	QString title;
	int when = 0;
	bool skipped = false;
};

struct Disliked {
	QString performer;
	QString title;
};

struct State {
	bool loaded = false;
	std::vector<Play> history;
	std::vector<Disliked> disliked;

	AudioMsgId current;
	QString performer;
	QString title;
	int listened = 0;
	int duration = 0;
	bool setup = false;
	rpl::lifetime lifetime;
};

[[nodiscard]] State &Current() {
	static auto result = State();
	return result;
}

[[nodiscard]] QString Key(const QString &text) {
	return text.trimmed().toLower();
}

[[nodiscard]] QString Path() {
	return cWorkingDir() + u"tdata/mwtk_taste.json"_q;
}

void Load() {
	auto &state = Current();
	if (state.loaded) {
		return;
	}
	state.loaded = true;
	auto file = QFile(Path());
	if (!file.open(QIODevice::ReadOnly)) {
		return;
	}
	const auto root = QJsonDocument::fromJson(file.readAll()).object();
	for (const auto &value : root.value(u"history"_q).toArray()) {
		const auto object = value.toObject();
		state.history.push_back({
			.performer = object.value(u"p"_q).toString(),
			.title = object.value(u"t"_q).toString(),
			.when = object.value(u"w"_q).toInt(),
			.skipped = object.value(u"s"_q).toBool(),
		});
	}
	for (const auto &value : root.value(u"disliked"_q).toArray()) {
		const auto object = value.toObject();
		state.disliked.push_back({
			.performer = object.value(u"p"_q).toString(),
			.title = object.value(u"t"_q).toString(),
		});
	}
}

void Save() {
	const auto &state = Current();
	auto history = QJsonArray();
	for (const auto &play : state.history) {
		history.push_back(QJsonObject{
			{ u"p"_q, play.performer },
			{ u"t"_q, play.title },
			{ u"w"_q, play.when },
			{ u"s"_q, play.skipped },
		});
	}
	auto disliked = QJsonArray();
	for (const auto &item : state.disliked) {
		disliked.push_back(QJsonObject{
			{ u"p"_q, item.performer },
			{ u"t"_q, item.title },
		});
	}
	auto file = QSaveFile(Path());
	if (file.open(QIODevice::WriteOnly)) {
		file.write(QJsonDocument(QJsonObject{
			{ u"history"_q, history },
			{ u"disliked"_q, disliked },
		}).toJson(QJsonDocument::Compact));
		file.commit();
	}
}

void FinishCurrent() {
	auto &state = Current();
	if (!state.current || state.performer.isEmpty()) {
		state.current = AudioMsgId();
		return;
	}
	Load();
	const auto almostWhole = (state.duration > 0)
		&& (state.listened * 10 >= state.duration * 6);
	state.history.push_back({
		.performer = state.performer,
		.title = state.title,
		.when = int(base::unixtime::now()),
		.skipped = (state.listened < kSkipSeconds) && !almostWhole,
	});
	if (state.history.size() > size_t(kMaxHistory)) {
		state.history.erase(
			state.history.begin(),
			state.history.begin() + (state.history.size() - kMaxHistory));
	}
	state.current = AudioMsgId();
	Save();
}

void Track(const ::Media::Player::TrackState &track) {
	auto &state = Current();
	if (track.id != state.current) {
		FinishCurrent();
		const auto document = track.id.audio();
		const auto song = document ? document->song() : nullptr;
		if (!song) {
			return;
		}
		state.current = track.id;
		state.performer = song->performer;
		state.title = song->title;
		state.listened = 0;
		state.duration = int(document->duration() / 1000);
	}
	if (track.frequency > 0) {
		state.listened = std::max(
			state.listened,
			int(track.position / track.frequency));
	}
}

} // namespace

void Setup() {
	auto &state = Current();
	if (state.setup) {
		return;
	}
	state.setup = true;
	::Media::Player::instance()->updatedNotifier(
	) | rpl::filter([](const ::Media::Player::TrackState &track) {
		return track.id.type() == AudioMsgId::Type::Song;
	}) | rpl::on_next([](const ::Media::Player::TrackState &track) {
		Track(track);
	}, state.lifetime);
}

void Dislike(const QString &performer, const QString &title) {
	Load();
	auto &state = Current();
	if (!IsDisliked(performer, title)) {
		state.disliked.push_back({ performer.trimmed(), title.trimmed() });
		Save();
	}
}

bool IsDisliked(const QString &performer, const QString &title) {
	Load();
	const auto p = Key(performer);
	const auto t = Key(title);
	for (const auto &item : Current().disliked) {
		if (Key(item.performer) == p && Key(item.title) == t) {
			return true;
		}
	}
	return false;
}

bool ArtistBanned(const QString &performer) {
	Load();
	const auto p = Key(performer);
	if (p.isEmpty()) {
		return false;
	}
	auto count = 0;
	for (const auto &item : Current().disliked) {
		if (Key(item.performer) == p) {
			++count;
		}
	}
	return count >= 2; // Two dislikes: the artist is not offered again.
}

bool PlayedRecently(const QString &performer, const QString &title, int days) {
	Load();
	const auto p = Key(performer);
	const auto t = Key(title);
	const auto since = int(base::unixtime::now()) - days * 86400;
	for (const auto &play : Current().history) {
		if (play.when >= since
			&& Key(play.performer) == p
			&& Key(play.title) == t) {
			return true;
		}
	}
	return false;
}

std::vector<QString> SeedArtists(
		int count,
		const std::vector<QString> &extra) {
	Load();
	auto weights = std::map<QString, double>();
	auto names = std::map<QString, QString>();
	const auto add = [&](const QString &name, double weight) {
		const auto key = Key(name);
		if (key.isEmpty()) {
			return;
		}
		weights[key] += weight;
		if (!names.contains(key)) {
			names[key] = name.trimmed();
		}
	};
	const auto now = int(base::unixtime::now());
	for (const auto &play : Current().history) {
		const auto days = std::max(now - play.when, 0) / 86400.;
		const auto fresh = std::pow(0.5, days / kHalfLifeDays);
		add(play.performer, (play.skipped ? -0.6 : 1.) * fresh);
	}
	for (const auto &name : extra) {
		add(name, 1.2);
	}
	for (const auto &item : Current().disliked) {
		add(item.performer, -1.5);
	}
	auto pool = std::vector<std::pair<QString, double>>();
	for (const auto &[key, weight] : weights) {
		if (weight > 0.05 && !ArtistBanned(names[key])) {
			pool.emplace_back(names[key], weight);
		}
	}
	auto random = std::mt19937(uint32(base::RandomValue<uint32>()));
	auto result = std::vector<QString>();
	while (!pool.empty() && int(result.size()) < count) {
		auto total = 0.;
		for (const auto &[name, weight] : pool) {
			total += weight;
		}
		auto pick = std::uniform_real_distribution<double>(0., total)(random);
		auto index = 0;
		for (; index + 1 < int(pool.size()); ++index) {
			pick -= pool[index].second;
			if (pick <= 0.) {
				break;
			}
		}
		result.push_back(pool[index].first);
		pool.erase(pool.begin() + index);
	}
	return result;
}

} // namespace Mwtk::Taste
