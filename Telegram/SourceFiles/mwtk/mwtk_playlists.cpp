/*
This file is part of MWTKgram, a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mwtk/mwtk_playlists.h"

#include "apiwrap.h"
#include "data/data_audio_msg_id.h"
#include "data/data_document.h"
#include "data/data_media_types.h"
#include "data/data_peer_id.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "data/data_peer.h"
#include "main/main_session.h"
#include "media/player/media_player_instance.h"
#include "settings.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/vertical_list.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QSaveFile>

#include <algorithm>
#include <map>
#include <random>

namespace Mwtk::Playlists {
namespace {

[[nodiscard]] QString Utf(const char *text) {
	return QString::fromUtf8(text);
}

struct Storage {
	bool loaded = false;
	std::map<quint64, std::vector<Playlist>> accounts;
	rpl::event_stream<> changes;
};

struct Active {
	base::weak_ptr<Main::Session> session;
	quint64 account = 0;
	int playlist = -1;
	std::vector<int> order;
	int position = 0;
	FullMsgId playing;
};

[[nodiscard]] Storage &State() {
	static auto result = Storage();
	return result;
}

[[nodiscard]] Active &CurrentActive() {
	static auto result = Active();
	return result;
}

[[nodiscard]] QString StoragePath() {
	return cWorkingDir() + u"tdata/mwtk_playlists.json"_q;
}

[[nodiscard]] quint64 AccountKey(not_null<Main::Session*> session) {
	return SerializePeerId(session->userPeerId());
}

void Load() {
	auto &state = State();
	if (state.loaded) {
		return;
	}
	state.loaded = true;
	auto file = QFile(StoragePath());
	if (!file.open(QIODevice::ReadOnly)) {
		return;
	}
	const auto document = QJsonDocument::fromJson(file.readAll());
	const auto accounts = document.object().value(u"accounts"_q).toObject();
	for (auto i = accounts.begin(); i != accounts.end(); ++i) {
		auto &list = state.accounts[i.key().toULongLong()];
		for (const auto &value : i.value().toArray()) {
			const auto object = value.toObject();
			auto playlist = Playlist();
			playlist.name = object.value(u"name"_q).toString();
			for (const auto &entry : object.value(u"tracks"_q).toArray()) {
				const auto track = entry.toObject();
				playlist.tracks.push_back({
					.peer = track.value(u"peer"_q).toString().toULongLong(),
					.msg = track.value(u"msg"_q).toString().toLongLong(),
					.title = track.value(u"title"_q).toString(),
					.performer = track.value(u"performer"_q).toString(),
					.duration = track.value(u"duration"_q).toInt(),
				});
			}
			list.push_back(std::move(playlist));
		}
	}
}

void Save() {
	auto &state = State();
	auto accounts = QJsonObject();
	for (const auto &[key, list] : state.accounts) {
		auto playlists = QJsonArray();
		for (const auto &playlist : list) {
			auto tracks = QJsonArray();
			for (const auto &track : playlist.tracks) {
				tracks.append(QJsonObject{
					{ u"peer"_q, QString::number(track.peer) },
					{ u"msg"_q, QString::number(track.msg) },
					{ u"title"_q, track.title },
					{ u"performer"_q, track.performer },
					{ u"duration"_q, track.duration },
				});
			}
			playlists.append(QJsonObject{
				{ u"name"_q, playlist.name },
				{ u"tracks"_q, tracks },
			});
		}
		accounts.insert(QString::number(key), playlists);
	}
	auto root = QJsonObject();
	root.insert(u"accounts"_q, accounts);
	QDir().mkpath(cWorkingDir() + u"tdata"_q);
	auto file = QSaveFile(StoragePath());
	if (file.open(QIODevice::WriteOnly)) {
		file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
		file.commit();
	}
	state.changes.fire({});
}

[[nodiscard]] std::vector<Playlist> &Mutable(
		not_null<Main::Session*> session) {
	Load();
	return State().accounts[AccountKey(session)];
}

[[nodiscard]] DocumentData *SongDocument(HistoryItem *item) {
	if (!item) {
		return nullptr;
	}
	if (const auto media = item->media()) {
		if (const auto document = media->document()) {
			if (document->isAudioFile()) {
				return document;
			}
		}
	}
	return nullptr;
}

[[nodiscard]] QString TrackLabel(const Track &track) {
	const auto title = track.title.isEmpty() ? Utf("Без названия") : track.title;
	return track.performer.isEmpty()
		? title
		: (track.performer + u" — "_q + title);
}

bool PlayTrackAt(not_null<Main::Session*> session, int attempt);

void PlayCurrent(not_null<Main::Session*> session) {
	PlayTrackAt(session, 0);
}

bool PlayTrackAt(not_null<Main::Session*> session, int attempt) {
	auto &active = CurrentActive();
	const auto &list = Mutable(session);
	if (active.playlist < 0
		|| active.playlist >= int(list.size())
		|| active.order.empty()
		|| attempt >= int(active.order.size())) {
		return false;
	}
	const auto &tracks = list[active.playlist].tracks;
	active.position = std::clamp(
		active.position,
		0,
		int(active.order.size()) - 1);
	const auto index = active.order[active.position];
	if (index < 0 || index >= int(tracks.size())) {
		return false;
	}
	const auto track = tracks[index];
	const auto peerId = DeserializePeerId(track.peer);
	const auto id = FullMsgId(peerId, MsgId(track.msg));
	const auto tryPlay = [=] {
		const auto item = session->data().message(id);
		if (const auto document = SongDocument(item)) {
			CurrentActive().playing = id;
			::Media::Player::instance()->play(
				AudioMsgId(document, item->fullId()));
			return true;
		}
		return false;
	};
	if (tryPlay()) {
		return true;
	}
	session->api().requestMessageData(
		session->data().peer(peerId),
		MsgId(track.msg),
		crl::guard(session, [=] {
			if (tryPlay()) {
				return;
			}
			// The message is gone, skip to the next one.
			auto &active = CurrentActive();
			if (active.position + 1 < int(active.order.size())) {
				++active.position;
				PlayTrackAt(session, attempt + 1);
			}
		}));
	return true;
}

[[nodiscard]] bool IsActiveFor(const AudioMsgId &current) {
	const auto &active = CurrentActive();
	return (active.playlist >= 0)
		&& active.session
		&& current.audio()
		&& (current.type() == AudioMsgId::Type::Song)
		&& (current.contextId() == active.playing);
}

} // namespace

const std::vector<Playlist> &List(not_null<Main::Session*> session) {
	return Mutable(session);
}

rpl::producer<> Changes() {
	return State().changes.events();
}

int Create(not_null<Main::Session*> session, const QString &name) {
	auto &list = Mutable(session);
	auto playlist = Playlist();
	playlist.name = name.trimmed().isEmpty()
		? (Utf("Плейлист ") + QString::number(list.size() + 1))
		: name.trimmed().left(64);
	list.push_back(std::move(playlist));
	Save();
	return int(list.size()) - 1;
}

void Rename(
		not_null<Main::Session*> session,
		int index,
		const QString &name) {
	auto &list = Mutable(session);
	if (index >= 0 && index < int(list.size()) && !name.trimmed().isEmpty()) {
		list[index].name = name.trimmed().left(64);
		Save();
	}
}

void Remove(not_null<Main::Session*> session, int index) {
	auto &list = Mutable(session);
	if (index >= 0 && index < int(list.size())) {
		list.erase(list.begin() + index);
		auto &active = CurrentActive();
		if (active.account == AccountKey(session)) {
			if (active.playlist == index) {
				active.playlist = -1;
			} else if (active.playlist > index) {
				--active.playlist;
			}
		}
		Save();
	}
}

bool AddTrack(
		not_null<Main::Session*> session,
		int index,
		not_null<HistoryItem*> item) {
	auto &list = Mutable(session);
	const auto document = SongDocument(item);
	if (!document || index < 0 || index >= int(list.size())) {
		return false;
	}
	auto track = Track();
	track.peer = SerializePeerId(item->history()->peer->id);
	track.msg = item->id.bare;
	if (const auto song = document->song()) {
		track.title = song->title;
		track.performer = song->performer;
	}
	if (track.title.isEmpty()) {
		track.title = document->filename();
	}
	track.duration = int(document->duration() / 1000);
	auto &tracks = list[index].tracks;
	const auto exists = ranges::any_of(tracks, [&](const Track &existing) {
		return (existing.peer == track.peer) && (existing.msg == track.msg);
	});
	if (exists) {
		return false;
	}
	tracks.push_back(std::move(track));
	Save();
	return true;
}

void RemoveTrack(not_null<Main::Session*> session, int index, int track) {
	auto &list = Mutable(session);
	if (index < 0 || index >= int(list.size())) {
		return;
	}
	auto &tracks = list[index].tracks;
	if (track < 0 || track >= int(tracks.size())) {
		return;
	}
	tracks.erase(tracks.begin() + track);
	auto &active = CurrentActive();
	if (active.account == AccountKey(session) && active.playlist == index) {
		active.playlist = -1;
	}
	Save();
}

void Play(
		not_null<Main::Session*> session,
		int index,
		int track,
		bool shuffle) {
	const auto &list = Mutable(session);
	if (index < 0 || index >= int(list.size())) {
		return;
	}
	const auto count = int(list[index].tracks.size());
	if (!count) {
		return;
	}
	auto &active = CurrentActive();
	active.session = base::make_weak(session);
	active.account = AccountKey(session);
	active.playlist = index;
	active.order.resize(count);
	for (auto i = 0; i != count; ++i) {
		active.order[i] = i;
	}
	const auto start = std::clamp(track, 0, count - 1);
	if (shuffle) {
		auto random = std::mt19937(std::random_device()());
		std::shuffle(active.order.begin(), active.order.end(), random);
		const auto i = ranges::find(active.order, start);
		if (track >= 0 && i != active.order.end()) {
			std::iter_swap(active.order.begin(), i);
		}
		active.position = 0;
	} else {
		active.position = start;
	}
	PlayCurrent(session);
}

bool Owns(const AudioMsgId &current) {
	return IsActiveFor(current);
}

bool HandleMove(const AudioMsgId &current, int delta) {
	if (!IsActiveFor(current)) {
		return false;
	}
	auto &active = CurrentActive();
	const auto session = active.session.get();
	const auto next = active.position + delta;
	if (next < 0 || next >= int(active.order.size())) {
		return false;
	}
	active.position = next;
	return PlayTrackAt(session, 0);
}

bool MoveAvailable(const AudioMsgId &current, int delta) {
	if (!IsActiveFor(current)) {
		return false;
	}
	const auto &active = CurrentActive();
	const auto next = active.position + delta;
	return (next >= 0) && (next < int(active.order.size()));
}

QString FormatDuration(int seconds) {
	if (seconds <= 0) {
		return QString();
	}
	return QString::number(seconds / 60)
		+ ':'
		+ QString::number(seconds % 60).rightJustified(2, '0');
}

void ShowAddToPlaylistBox(
		not_null<Window::SessionController*> controller,
		FullMsgId itemId) {
	const auto session = &controller->session();
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(Utf("Добавить в плейлист")));
		box->setWidth(st::boxWideWidth);
		const auto add = [=](int index) {
			const auto item = session->data().message(itemId);
			if (!item) {
				box->closeBox();
				return;
			}
			const auto &list = List(session);
			const auto name = (index >= 0 && index < int(list.size()))
				? list[index].name
				: QString();
			if (AddTrack(session, index, item)) {
				controller->showToast(
					Utf("Добавлено в «") + name + Utf("»"));
			} else {
				controller->showToast(
					Utf("Этот трек уже есть в «") + name + Utf("»"));
			}
			box->closeBox();
		};
		const auto &list = List(session);
		for (auto i = 0; i != int(list.size()); ++i) {
			const auto count = int(list[i].tracks.size());
			const auto button = box->addRow(
				object_ptr<Ui::SettingsButton>(
					box,
					rpl::single(list[i].name
						+ u"  ·  "_q
						+ QString::number(count)),
					st::settingsButtonNoIcon),
				style::margins());
			button->setClickedCallback([=] { add(i); });
		}
		if (!list.empty()) {
			Ui::AddSkip(box->verticalLayout());
		}
		const auto field = box->addRow(object_ptr<Ui::InputField>(
			box,
			st::defaultInputField,
			rpl::single(Utf("Название нового плейлиста"))));
		field->setMaxLength(64);
		box->setFocusCallback([=] {
			field->setFocusFast();
		});
		const auto create = [=] {
			add(Create(session, field->getLastText()));
		};
		field->submits(
		) | rpl::on_next(create, field->lifetime());
		box->addButton(rpl::single(Utf("Создать и добавить")), create);
		box->addButton(rpl::single(Utf("Отмена")), [=] {
			box->closeBox();
		});
	}));
}

namespace {

void ShowPlaylistBox(
		not_null<Window::SessionController*> controller,
		int index) {
	const auto session = &controller->session();
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setWidth(st::boxWideWidth);
		const auto &list = List(session);
		if (index < 0 || index >= int(list.size())) {
			return;
		}
		box->setTitle(rpl::single(list[index].name));
		const auto content = box->addRow(
			object_ptr<Ui::VerticalLayout>(box),
			style::margins());
		const auto fill = [=] {
			content->clear();
			const auto &list = List(session);
			if (index >= int(list.size())) {
				box->closeBox();
				return;
			}
			const auto &tracks = list[index].tracks;
			if (tracks.empty()) {
				content->add(
					object_ptr<Ui::FlatLabel>(
						content,
						rpl::single(Utf("Плейлист пуст. Нажми правой кнопкой "
							"на любой трек в чате → «В плейлист MWTK…».")),
						st::boxDividerLabel),
					st::boxRowPadding);
			}
			for (auto i = 0; i != int(tracks.size()); ++i) {
				const auto &track = tracks[i];
				auto label = QString::number(i + 1)
					+ u". "_q
					+ TrackLabel(track);
				const auto duration = FormatDuration(track.duration);
				if (!duration.isEmpty()) {
					label += u"  ·  "_q + duration;
				}
				const auto button = content->add(
					object_ptr<Ui::SettingsButton>(
						content,
						rpl::single(label),
						st::settingsButtonNoIcon));
				button->setClickedCallback([=] {
					Play(session, index, i, false);
				});
				const auto remove = Ui::CreateChild<Ui::IconButton>(
					button,
					st::boxTitleClose);
				button->sizeValue(
				) | rpl::on_next([=](QSize size) {
					remove->moveToRight(
						0,
						(size.height() - remove->height()) / 2,
						size.width());
				}, remove->lifetime());
				remove->setClickedCallback([=] {
					crl::on_main(box, [=] {
						RemoveTrack(session, index, i);
					});
				});
			}
		};
		fill();
		Changes(
		) | rpl::on_next([=] {
			crl::on_main(box, fill);
		}, box->lifetime());
		box->addButton(rpl::single(Utf("Играть")), [=] {
			Play(session, index, 0, false);
		});
		box->addButton(rpl::single(Utf("Перемешать")), [=] {
			Play(session, index, -1, true);
		});
		box->addLeftButton(rpl::single(Utf("Удалить")), [=] {
			Remove(session, index);
			box->closeBox();
		});
	}));
}

} // namespace

void ShowPlaylistsBox(not_null<Window::SessionController*> controller) {
	const auto session = &controller->session();
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(Utf("Мои плейлисты")));
		box->setWidth(st::boxWideWidth);
		const auto content = box->addRow(
			object_ptr<Ui::VerticalLayout>(box),
			style::margins());
		const auto fill = [=] {
			content->clear();
			const auto &list = List(session);
			if (list.empty()) {
				content->add(
					object_ptr<Ui::FlatLabel>(
						content,
						rpl::single(Utf("Пока нет плейлистов. Создай первый ниже "
							"или добавь трек из чата через правый клик.")),
						st::boxDividerLabel),
					st::boxRowPadding);
			}
			for (auto i = 0; i != int(list.size()); ++i) {
				const auto count = int(list[i].tracks.size());
				const auto button = content->add(
					object_ptr<Ui::SettingsButton>(
						content,
						rpl::single(list[i].name
							+ u"  ·  "_q
							+ QString::number(count)),
						st::settingsButtonNoIcon));
				button->setClickedCallback([=] {
					ShowPlaylistBox(controller, i);
				});
			}
		};
		fill();
		Changes(
		) | rpl::on_next([=] {
			crl::on_main(box, fill);
		}, box->lifetime());
		Ui::AddSkip(box->verticalLayout());
		const auto field = box->addRow(object_ptr<Ui::InputField>(
			box,
			st::defaultInputField,
			rpl::single(Utf("Название нового плейлиста"))));
		field->setMaxLength(64);
		const auto create = [=] {
			if (!field->getLastText().trimmed().isEmpty()) {
				Create(session, field->getLastText());
				field->clear();
			}
		};
		field->submits(
		) | rpl::on_next(create, field->lifetime());
		box->addButton(rpl::single(Utf("Создать")), create);
		box->addButton(rpl::single(Utf("Закрыть")), [=] {
			box->closeBox();
		});
	}));
}

} // namespace Mwtk::Playlists
