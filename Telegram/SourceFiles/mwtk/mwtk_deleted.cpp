/*
This file is part of MWTKgram, a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mwtk/mwtk_deleted.h"

#include "base/unixtime.h"
#include "data/data_document.h"
#include "data/data_media_types.h"
#include "data/data_peer.h"
#include "data/data_peer_id.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_session.h"
#include "mwtk/mwtk_options.h"
#include "settings.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/labels.h"
#include "ui/vertical_list.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"

#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QSaveFile>

#include <deque>

namespace Mwtk::Deleted {
namespace {

constexpr auto kMaxEntries = 3000;
constexpr auto kShowLimit = 300;

struct Entry {
	quint64 account = 0;
	quint64 peer = 0;
	QString chat;
	QString sender;
	QString text;
	QString media;
	qint64 date = 0;
	qint64 deleted = 0;
	bool mine = false;
};

struct Storage {
	bool loaded = false;
	std::deque<Entry> entries;
	bool saveScheduled = false;
};

[[nodiscard]] Storage &State() {
	static auto result = Storage();
	return result;
}

[[nodiscard]] QString StoragePath() {
	return cWorkingDir() + u"tdata/mwtk_deleted.json"_q;
}

[[nodiscard]] QString Utf(const char *text) {
	return QString::fromUtf8(text);
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
	const auto list = QJsonDocument::fromJson(
		file.readAll()).object().value(u"entries"_q).toArray();
	for (const auto &value : list) {
		const auto o = value.toObject();
		state.entries.push_back({
			.account = o.value(u"account"_q).toString().toULongLong(),
			.peer = o.value(u"peer"_q).toString().toULongLong(),
			.chat = o.value(u"chat"_q).toString(),
			.sender = o.value(u"sender"_q).toString(),
			.text = o.value(u"text"_q).toString(),
			.media = o.value(u"media"_q).toString(),
			.date = qint64(o.value(u"date"_q).toDouble()),
			.deleted = qint64(o.value(u"deleted"_q).toDouble()),
			.mine = o.value(u"mine"_q).toBool(),
		});
	}
}

void Save() {
	auto &state = State();
	state.saveScheduled = false;
	auto list = QJsonArray();
	for (const auto &entry : state.entries) {
		list.append(QJsonObject{
			{ u"account"_q, QString::number(entry.account) },
			{ u"peer"_q, QString::number(entry.peer) },
			{ u"chat"_q, entry.chat },
			{ u"sender"_q, entry.sender },
			{ u"text"_q, entry.text },
			{ u"media"_q, entry.media },
			{ u"date"_q, double(entry.date) },
			{ u"deleted"_q, double(entry.deleted) },
			{ u"mine"_q, entry.mine },
		});
	}
	auto root = QJsonObject();
	root.insert(u"entries"_q, list);
	QDir().mkpath(cWorkingDir() + u"tdata"_q);
	auto file = QSaveFile(StoragePath());
	if (file.open(QIODevice::WriteOnly)) {
		file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
		file.commit();
	}
}

[[nodiscard]] QString MediaLabel(not_null<HistoryItem*> item) {
	const auto media = item->media();
	if (!media) {
		return QString();
	}
	if (media->photo()) {
		return Utf("фото");
	}
	if (const auto document = media->document()) {
		if (document->sticker()) {
			return Utf("стикер");
		} else if (document->isVoiceMessage()) {
			return Utf("голосовое");
		} else if (document->isVideoMessage()) {
			return Utf("кружок");
		} else if (document->isAnimation()) {
			return Utf("GIF");
		} else if (document->isVideoFile()) {
			return Utf("видео");
		} else if (document->isAudioFile()) {
			return Utf("музыка: ") + document->filename();
		}
		return Utf("файл: ") + document->filename();
	}
	return Utf("вложение");
}

[[nodiscard]] QString FormatTime(qint64 unixtime) {
	return QDateTime::fromSecsSinceEpoch(unixtime).toString(
		u"dd.MM.yyyy HH:mm"_q);
}

} // namespace

void Record(const std::vector<not_null<HistoryItem*>> &items, bool mine) {
	if (items.empty()
		|| !EffectsEnabled()
		|| !OptionValue(kOptionSaveDeleted)) {
		return;
	}
	Load();
	auto &state = State();
	const auto now = qint64(base::unixtime::now());
	auto added = 0;
	for (const auto &item : items) {
		if (item->isService() || !item->isRegular()) {
			continue;
		}
		const auto text = item->originalText().text;
		const auto media = MediaLabel(item);
		if (text.isEmpty() && media.isEmpty()) {
			continue;
		}
		const auto peer = item->history()->peer;
		state.entries.push_back({
			.account = SerializePeerId(peer->session().userPeerId()),
			.peer = SerializePeerId(peer->id),
			.chat = peer->name(),
			.sender = item->from()->name(),
			.text = text.left(4000),
			.media = media,
			.date = qint64(item->date()),
			.deleted = now,
			.mine = mine,
		});
		++added;
	}
	if (!added) {
		return;
	}
	while (state.entries.size() > kMaxEntries) {
		state.entries.pop_front();
	}
	if (!state.saveScheduled) {
		state.saveScheduled = true;
		crl::on_main([] { Save(); });
	}
}

bool HasAny(not_null<PeerData*> peer) {
	Load();
	const auto account = SerializePeerId(peer->session().userPeerId());
	const auto id = SerializePeerId(peer->id);
	for (const auto &entry : State().entries) {
		if (entry.account == account && entry.peer == id) {
			return true;
		}
	}
	return false;
}

void ShowBox(
		not_null<Window::SessionController*> controller,
		PeerData *peer) {
	Load();
	const auto account = SerializePeerId(
		controller->session().userPeerId());
	const auto id = peer ? SerializePeerId(peer->id) : quint64(0);
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(peer
			? (Utf("Удалённые: ") + peer->name())
			: Utf("Удалённые сообщения")));
		box->setWidth(st::boxWideWidth);
		const auto &entries = State().entries;
		auto shown = 0;
		for (auto i = entries.rbegin(); i != entries.rend(); ++i) {
			const auto &entry = *i;
			if (entry.account != account || (peer && entry.peer != id)) {
				continue;
			}
			auto header = (peer ? QString() : (entry.chat + u" · "_q))
				+ entry.sender
				+ u" · "_q
				+ FormatTime(entry.date)
				+ (entry.mine ? Utf(" · удалил ты") : Utf(" · удалено"));
			auto body = entry.text;
			if (!entry.media.isEmpty()) {
				body = u"["_q + entry.media + u"] "_q + body;
			}
			box->addRow(object_ptr<Ui::FlatLabel>(
				box,
				rpl::single(header),
				st::boxDividerLabel));
			const auto label = box->addRow(object_ptr<Ui::FlatLabel>(
				box,
				rpl::single(body),
				st::boxLabel));
			label->setSelectable(true);
			Ui::AddSkip(box->verticalLayout());
			if (++shown >= kShowLimit) {
				break;
			}
		}
		if (!shown) {
			box->addRow(object_ptr<Ui::FlatLabel>(
				box,
				rpl::single(Utf("Пока ничего не удалялось. MWTKgram сохраняет "
					"сообщения, которые удаляются, пока клиент открыт.")),
				st::boxDividerLabel));
		}
		box->addButton(rpl::single(Utf("Закрыть")), [=] {
			box->closeBox();
		});
		box->addLeftButton(rpl::single(Utf("Очистить")), [=] {
			auto &entries = State().entries;
			std::erase_if(entries, [&](const Entry &entry) {
				return entry.account == account
					&& (!peer || entry.peer == id);
			});
			Save();
			box->closeBox();
		});
	}));
}

} // namespace Mwtk::Deleted
