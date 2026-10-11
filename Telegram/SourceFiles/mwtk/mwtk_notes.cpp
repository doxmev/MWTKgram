/*
This file is part of MWTKgram, a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mwtk/mwtk_notes.h"

#include "data/data_peer.h"
#include "data/data_peer_id.h"
#include "main/main_session.h"
#include "settings.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QSaveFile>

#include <map>

namespace Mwtk::Notes {
namespace {

using Key = std::pair<quint64, quint64>;

struct Storage {
	bool loaded = false;
	std::map<Key, QString> notes;
	rpl::event_stream<Key> changes;
};

[[nodiscard]] Storage &State() {
	static auto result = Storage();
	return result;
}

[[nodiscard]] QString StoragePath() {
	return cWorkingDir() + u"tdata/mwtk_notes.json"_q;
}

[[nodiscard]] Key KeyFor(not_null<PeerData*> peer) {
	return {
		SerializePeerId(peer->session().userPeerId()),
		SerializePeerId(peer->id),
	};
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
	const auto accounts = QJsonDocument::fromJson(
		file.readAll()).object().value(u"accounts"_q).toObject();
	for (auto i = accounts.begin(); i != accounts.end(); ++i) {
		const auto account = i.key().toULongLong();
		const auto peers = i.value().toObject();
		for (auto j = peers.begin(); j != peers.end(); ++j) {
			const auto text = j.value().toString();
			if (!text.isEmpty()) {
				state.notes[{ account, j.key().toULongLong() }] = text;
			}
		}
	}
}

void Save() {
	auto accounts = QJsonObject();
	for (const auto &[key, text] : State().notes) {
		const auto account = QString::number(key.first);
		auto peers = accounts.value(account).toObject();
		peers.insert(QString::number(key.second), text);
		accounts.insert(account, peers);
	}
	auto root = QJsonObject();
	root.insert(u"accounts"_q, accounts);
	QDir().mkpath(cWorkingDir() + u"tdata"_q);
	auto file = QSaveFile(StoragePath());
	if (file.open(QIODevice::WriteOnly)) {
		file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
		file.commit();
	}
}

} // namespace

QString Get(not_null<PeerData*> peer) {
	Load();
	const auto &notes = State().notes;
	const auto i = notes.find(KeyFor(peer));
	return (i != notes.end()) ? i->second : QString();
}

rpl::producer<QString> Value(not_null<PeerData*> peer) {
	const auto key = KeyFor(peer);
	return rpl::single(
		Get(peer)
	) | rpl::then(State().changes.events(
	) | rpl::filter([=](const Key &changed) {
		return changed == key;
	}) | rpl::map([=] {
		return Get(peer);
	}));
}

void Set(not_null<PeerData*> peer, const QString &text) {
	Load();
	const auto key = KeyFor(peer);
	const auto trimmed = text.trimmed().left(4000);
	auto &notes = State().notes;
	if (trimmed.isEmpty()) {
		notes.erase(key);
	} else {
		notes[key] = trimmed;
	}
	Save();
	State().changes.fire_copy(key);
}

void ShowEditBox(
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(QString::fromUtf8("Заметка MWTK")));
		box->setWidth(st::boxWideWidth);
		const auto field = box->addRow(object_ptr<Ui::InputField>(
			box,
			st::newGroupDescription,
			Ui::InputField::Mode::MultiLine,
			rpl::single(QString::fromUtf8("Что важно помнить об этом чате")),
			Get(peer)));
		field->setMaxLength(4000);
		field->setSubmitSettings(Ui::InputField::SubmitSettings::CtrlEnter);
		box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			rpl::single(QString::fromUtf8(
				"Заметка хранится только на этом компьютере и видна только тебе.")),
			st::boxDividerLabel));
		box->setFocusCallback([=] {
			field->setFocusFast();
		});
		const auto save = [=] {
			Set(peer, field->getLastText());
			box->closeBox();
		};
		field->submits(
		) | rpl::on_next(save, field->lifetime());
		box->addButton(rpl::single(QString::fromUtf8("Сохранить")), save);
		box->addButton(rpl::single(QString::fromUtf8("Отмена")), [=] {
			box->closeBox();
		});
		if (!Get(peer).isEmpty()) {
			box->addLeftButton(rpl::single(QString::fromUtf8("Удалить")), [=] {
				Set(peer, QString());
				box->closeBox();
			});
		}
	}));
}

} // namespace Mwtk::Notes
