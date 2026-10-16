/*
This file is part of MWTKgram, a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mwtk/mwtk_together.h"

#include "api/api_common.h"
#include "api/api_editing.h"
#include "api/api_sending.h"
#include "apiwrap.h"
#include "base/call_delayed.h"
#include "base/random.h"
#include "base/timer.h"
#include "base/unixtime.h"
#include "core/application.h"
#include "data/data_changes.h"
#include "data/data_document.h"
#include "data/data_drafts.h"
#include "data/data_media_types.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "media/audio/media_audio.h"
#include "media/player/media_player_instance.h"
#include "mwtk/mwtk_options.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_entity.h"
#include "ui/widgets/labels.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>

namespace Mwtk::Together {
namespace {

const auto kPrefix = u"https://github.com/doxmev/MWTKgram#together="_q;
constexpr auto kInviteFreshSeconds = 10 * 60;
constexpr auto kSeekJumpMs = 2500;

struct Payload {
	QString type; // invite, accept, decline, state, end
	QString session;
	MsgId msg = 0;
	int64 position = 0; // ms
	int at = 0; // unixtime when position was right
	bool playing = false;
};

[[nodiscard]] QString Encode(const Payload &payload) {
	auto object = QJsonObject{
		{ u"t"_q, payload.type },
		{ u"s"_q, payload.session },
	};
	if (payload.type == u"state"_q) {
		object.insert(u"m"_q, double(payload.msg.bare));
		object.insert(u"p"_q, double(payload.position));
		object.insert(u"a"_q, payload.at);
		object.insert(u"on"_q, payload.playing ? 1 : 0);
	}
	return kPrefix + QString::fromLatin1(QJsonDocument(object).toJson(
		QJsonDocument::Compact).toBase64(
			QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

[[nodiscard]] std::optional<Payload> Decode(not_null<HistoryItem*> item) {
	const auto &text = item->originalText();
	for (const auto &entity : text.entities) {
		if (entity.type() != EntityType::CustomUrl
			|| !entity.data().startsWith(kPrefix)) {
			continue;
		}
		const auto data = QByteArray::fromBase64(
			entity.data().mid(kPrefix.size()).toLatin1(),
			QByteArray::Base64UrlEncoding);
		const auto object = QJsonDocument::fromJson(data).object();
		auto result = Payload{
			.type = object.value(u"t"_q).toString(),
			.session = object.value(u"s"_q).toString(),
			.msg = MsgId(int64(object.value(u"m"_q).toDouble())),
			.position = int64(object.value(u"p"_q).toDouble()),
			.at = object.value(u"a"_q).toInt(),
			.playing = (object.value(u"on"_q).toInt() != 0),
		};
		if (result.type.isEmpty() || result.session.isEmpty()) {
			return std::nullopt;
		}
		return result;
	}
	return std::nullopt;
}

[[nodiscard]] TextWithEntities Message(
		const QString &text,
		const Payload &payload) {
	// The payload hides in a link on the 🎧 at the start.
	auto result = TextWithEntities{ QString::fromUtf8("🎧 ") + text };
	result.entities.push_back(EntityInText(
		EntityType::CustomUrl,
		0,
		2,
		Encode(payload)));
	return result;
}

struct State {
	base::weak_ptr<Main::Session> session;
	PeerData *peer = nullptr;
	QString id;
	bool host = false;
	bool active = false;

	// Host.
	FullMsgId control;
	FullMsgId shared; // the song as a message in this chat
	DocumentData *sharing = nullptr; // being sent right now
	AudioMsgId lastTrack;
	bool lastPlaying = false;
	int64 lastPosition = 0;
	crl::time lastPositionAt = 0;
	crl::time lastEdit = 0;
	std::unique_ptr<base::Timer> flush;

	// Partner.
	FullMsgId playing;

	rpl::lifetime lifetime; // per-session subscriptions
	rpl::lifetime setup;
	bool setupDone = false;
};

[[nodiscard]] State &Current() {
	static auto result = State();
	return result;
}

void Toast(const QString &text) {
	if (const auto window = Core::App().activePrimaryWindow()) {
		window->showToast(TextWithEntities{ text });
	}
}

[[nodiscard]] QString NameOf(not_null<PeerData*> peer) {
	return peer->shortName().isEmpty() ? peer->name() : peer->shortName();
}

void Send(
		not_null<Main::Session*> session,
		not_null<PeerData*> peer,
		const TextWithEntities &text) {
	auto message = Api::MessageToSend(
		Api::SendAction(session->data().history(peer)));
	message.textWithTags = TextWithTags{
		text.text,
		TextUtilities::ConvertEntitiesToTextTags(text.entities),
	};
	message.webPage.removed = true;
	session->api().sendMessage(std::move(message));
}

void Clear() {
	auto &state = Current();
	state.lifetime.destroy();
	state.flush = nullptr;
	state.session = base::weak_ptr<Main::Session>();
	state.peer = nullptr;
	state.id = QString();
	state.host = false;
	state.active = false;
	state.control = FullMsgId();
	state.shared = FullMsgId();
	state.sharing = nullptr;
	state.lastTrack = AudioMsgId();
	state.playing = FullMsgId();
}

// ------------------------------------------------------------ host

void PushState();

void ShareCurrentSong(
		not_null<Main::Session*> session,
		not_null<DocumentData*> document) {
	auto &state = Current();
	if (state.sharing == document) {
		return;
	}
	state.sharing = document;
	const auto history = session->data().history(state.peer);
	session->data().itemIdChanged(
	) | rpl::on_next([=](Data::Session::IdChange change) {
		auto &state = Current();
		const auto item = session->data().message(change.newId);
		if (!item
			|| item->history() != history
			|| !item->media()
			|| item->media()->document() != state.sharing) {
			return;
		}
		state.shared = change.newId;
		state.sharing = nullptr;
		PushState();
	}, state.lifetime);
	Api::SendExistingDocument(
		Api::MessageToSend(Api::SendAction(history)),
		document);
}

void PushState() {
	auto &state = Current();
	const auto session = state.session.get();
	if (!session || !state.host || !state.active || !state.control) {
		return;
	}
	const auto item = session->data().message(state.control);
	if (!item) {
		return;
	}
	const auto player = ::Media::Player::instance();
	const auto current = player->current(AudioMsgId::Type::Song);
	const auto document = current.audio();
	const auto track = player->getState(AudioMsgId::Type::Song);
	if (!document || !document->song()) {
		return;
	}
	// The song must be a message in this chat for the partner to play it.
	const auto context = current.contextId();
	if (context.peer == state.peer->id && IsServerMsgId(context.msg)) {
		state.shared = context;
	} else if (!state.shared
		|| !session->data().message(state.shared)
		|| !session->data().message(state.shared)->media()
		|| session->data().message(state.shared)->media()->document() != document) {
		ShareCurrentSong(session, document);
		return;
	}
	const auto playing = !IsPausedOrPausing(track.state)
		&& !IsStoppedOrStopping(track.state);
	const auto position = (track.frequency > 0)
		? (track.position * 1000 / track.frequency)
		: 0;
	const auto payload = Payload{
		.type = u"state"_q,
		.session = state.id,
		.msg = state.shared.msg,
		.position = position,
		.at = int(base::unixtime::now()),
		.playing = playing,
	};
	const auto song = document->song();
	const auto title = (song->performer.isEmpty() ? QString() : (song->performer + u" — "_q))
		+ (song->title.isEmpty() ? document->filename() : song->title);
	const auto text = Message(
		QString::fromUtf8("Слушаем вместе: ") + title + (playing
			? QString::fromUtf8(" ▶")
			: QString::fromUtf8(" ⏸")),
		payload);
	state.lastEdit = crl::now();
	Api::EditTextMessage(
		item,
		text,
		Data::WebPageDraft{ .removed = true },
		Api::SendOptions(),
		[](mtpRequestId) {},
		[](const QString &, mtpRequestId) {},
		false);
}

void SchedulePush() {
	auto &state = Current();
	if (!state.flush) {
		state.flush = std::make_unique<base::Timer>([] { PushState(); });
	}
	// Not more often than once a second and a half: Telegram limits edits.
	const auto since = crl::now() - state.lastEdit;
	state.flush->callOnce(std::max(crl::time(500), crl::time(1500) - since));
}

void WatchPlayer() {
	auto &state = Current();
	::Media::Player::instance()->updatedNotifier(
	) | rpl::filter([](const ::Media::Player::TrackState &track) {
		return track.id.type() == AudioMsgId::Type::Song;
	}) | rpl::on_next([](const ::Media::Player::TrackState &track) {
		auto &state = Current();
		if (!state.host || !state.active) {
			return;
		}
		const auto now = crl::now();
		const auto playing = !IsPausedOrPausing(track.state)
			&& !IsStoppedOrStopping(track.state);
		const auto position = (track.frequency > 0)
			? (track.position * 1000 / track.frequency)
			: 0;
		auto changed = (track.id != state.lastTrack)
			|| (playing != state.lastPlaying);
		if (!changed && playing && state.lastPositionAt) {
			const auto expected = state.lastPosition
				+ (now - state.lastPositionAt);
			changed = std::abs(position - expected) > kSeekJumpMs;
		}
		state.lastTrack = track.id;
		state.lastPlaying = playing;
		state.lastPosition = position;
		state.lastPositionAt = now;
		if (changed) {
			SchedulePush();
		}
	}, state.lifetime);
}

// ------------------------------------------------------------ partner

void PlayAt(
		not_null<Main::Session*> session,
		FullMsgId id,
		const Payload &payload) {
	const auto item = session->data().message(id);
	const auto document = (item && item->media())
		? item->media()->document()
		: nullptr;
	if (!document) {
		return;
	}
	auto &state = Current();
	const auto player = ::Media::Player::instance();
	const auto current = player->current(AudioMsgId::Type::Song);
	if (current.contextId() != id) {
		player->play(AudioMsgId(document, id));
	}
	state.playing = id;
	if (!payload.playing) {
		base::call_delayed(400, [] {
			::Media::Player::instance()->pause(AudioMsgId::Type::Song);
		});
		return;
	}
	const auto duration = document->duration();
	base::call_delayed(700, [=] {
		auto &state = Current();
		if (state.playing != id || !state.active) {
			return;
		}
		const auto player = ::Media::Player::instance();
		if (player->current(AudioMsgId::Type::Song).contextId() != id) {
			return;
		}
		const auto passed = std::max(
			int64(base::unixtime::now()) - payload.at,
			int64(0)) * 1000 + 700;
		const auto target = payload.position + passed;
		if (duration > 0) {
			player->startSeeking(AudioMsgId::Type::Song);
			player->finishSeeking(
				AudioMsgId::Type::Song,
				std::clamp(double(target) / duration, 0., 0.999));
		}
		const auto track = player->getState(AudioMsgId::Type::Song);
		if (IsPausedOrPausing(track.state)) {
			player->play(AudioMsgId::Type::Song);
		}
	});
}

void Apply(not_null<Main::Session*> session, const Payload &payload) {
	auto &state = Current();
	const auto id = FullMsgId(state.peer->id, payload.msg);
	if (session->data().message(id)) {
		PlayAt(session, id, payload);
		return;
	}
	session->api().requestMessageData(state.peer, payload.msg, crl::guard(session, [=] {
		auto &state = Current();
		if (state.active && !state.host) {
			PlayAt(session, id, payload);
		}
	}));
}

void Ask(
		not_null<Main::Session*> session,
		not_null<PeerData*> peer,
		const QString &id) {
	const auto window = Core::App().activePrimaryWindow();
	if (!window) {
		return;
	}
	const auto name = NameOf(peer);
	window->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(QString::fromUtf8("Слушать вместе?")));
		box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			QString::fromUtf8(
				"%1 зовёт слушать музыку вместе.\n\n"
				"Если согласишься, у тебя будет играть то же, что включает %1: "
				"песня, пауза и перемотка. Выйти можно в любой момент — "
				"в меню чата (⋯) → «Закончить слушать вместе»."
			).arg(name),
			st::boxLabel));
		box->addButton(rpl::single(QString::fromUtf8("Принять")), [=] {
			box->closeBox();
			Clear();
			auto &state = Current();
			state.session = base::make_weak(session);
			state.peer = peer;
			state.id = id;
			state.host = false;
			state.active = true;
			Send(session, peer, Message(
				QString::fromUtf8("Согласен слушать вместе"),
				{ .type = u"accept"_q, .session = id }));
			Toast(QString::fromUtf8("Ждём, что включит %1…").arg(name));
		});
		box->addButton(rpl::single(QString::fromUtf8("Отклонить")), [=] {
			box->closeBox();
			Send(session, peer, Message(
				QString::fromUtf8("Не сейчас"),
				{ .type = u"decline"_q, .session = id }));
		});
	}));
}

void Handle(not_null<Main::Session*> session, not_null<HistoryItem*> item) {
	const auto payload = Decode(item);
	if (!payload) {
		return;
	}
	auto &state = Current();
	const auto peer = item->history()->peer;
	if (item->out()) {
		// Our own invite got its real id: remember it to edit it later.
		if (state.host
			&& payload->type == u"invite"_q
			&& payload->session == state.id
			&& IsServerMsgId(item->id)) {
			state.control = item->fullId();
		}
		return;
	}
	if (payload->type == u"invite"_q) {
		const auto fresh = (base::unixtime::now() - item->date())
			< kInviteFreshSeconds;
		if (fresh && OptionValue(kOptionTogetherAllow) && peer->isUser()) {
			Ask(session, peer, payload->session);
		}
		return;
	}
	if (state.peer != peer || state.id != payload->session) {
		return;
	}
	if (payload->type == u"accept"_q && state.host) {
		state.active = true;
		Toast(QString::fromUtf8("%1 слушает с тобой. Включай музыку!").arg(NameOf(peer)));
		SchedulePush();
	} else if (payload->type == u"decline"_q && state.host) {
		Toast(QString::fromUtf8("%1 сейчас не может слушать вместе.").arg(NameOf(peer)));
		Clear();
	} else if (payload->type == u"state"_q && !state.host && state.active) {
		Apply(session, *payload);
	} else if (payload->type == u"end"_q) {
		Toast(QString::fromUtf8("Совместное прослушивание закончено."));
		Clear();
	}
}

} // namespace

void Setup() {
	auto &state = Current();
	if (state.setupDone) {
		return;
	}
	state.setupDone = true;
	Core::App().domain().activeSessionValue(
	) | rpl::map([](Main::Session *session) {
		if (!session) {
			return rpl::never<std::pair<Main::Session*, HistoryItem*>>()
				| rpl::type_erased;
		}
		return rpl::merge(
			session->data().newItemAdded(
			) | rpl::map([=](not_null<HistoryItem*> item) {
				return std::make_pair(session, item.get());
			}),
			session->changes().messageUpdates(
				Data::MessageUpdate::Flag::Edited
			) | rpl::map([=](const Data::MessageUpdate &update) {
				return std::make_pair(session, update.item.get());
			}),
			session->data().itemIdChanged(
			) | rpl::map([=](Data::Session::IdChange change) {
				return std::make_pair(
					session,
					session->data().message(change.newId));
			})
		) | rpl::type_erased;
	}) | rpl::flatten_latest(
	) | rpl::on_next([](std::pair<Main::Session*, HistoryItem*> pair) {
		if (pair.first && pair.second) {
			Handle(pair.first, pair.second);
		}
	}, state.setup);
}

bool ActiveWith(not_null<PeerData*> peer) {
	const auto &state = Current();
	return (state.peer == peer) && !state.id.isEmpty();
}

void Invite(
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer) {
	End();
	const auto session = &controller->session();
	auto &state = Current();
	state.session = base::make_weak(session);
	state.peer = peer;
	state.id = QString::number(base::RandomValue<uint64>(), 36);
	state.host = true;
	state.active = false;
	WatchPlayer();
	Send(session, peer, Message(
		QString::fromUtf8(
			"Зову слушать музыку вместе. Нужен MWTKgram — он спросит, согласен ли ты."),
		{ .type = u"invite"_q, .session = state.id }));
	controller->showToast(QString::fromUtf8(
		"Приглашение отправлено. Музыка пойдёт, когда %1 примет его.").arg(NameOf(peer)));
}

void End() {
	auto &state = Current();
	const auto session = state.session.get();
	if (!session || !state.peer || state.id.isEmpty()) {
		Clear();
		return;
	}
	const auto payload = Payload{ .type = u"end"_q, .session = state.id };
	const auto text = Message(
		QString::fromUtf8("Совместное прослушивание закончено"),
		payload);
	if (state.host && state.control) {
		if (const auto item = session->data().message(state.control)) {
			Api::EditTextMessage(
				item,
				text,
				Data::WebPageDraft{ .removed = true },
				Api::SendOptions(),
				[](mtpRequestId) {},
				[](const QString &, mtpRequestId) {},
				false);
		}
	} else {
		Send(session, state.peer, text);
	}
	Clear();
}

} // namespace Mwtk::Together
