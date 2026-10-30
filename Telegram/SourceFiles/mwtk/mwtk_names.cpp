/*
This file is part of MWTKgram, a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mwtk/mwtk_names.h"

#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_peer.h"
#include "data/data_peer_id.h"
#include "data/data_user.h"
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

namespace Mwtk::Names {
namespace {

struct Storage {
	bool loaded = false;
	std::map<QString, QString> names;
	std::map<const PeerData*, QString> originals;
};

[[nodiscard]] Storage &State() {
	static auto result = Storage();
	return result;
}

[[nodiscard]] QString StoragePath() {
	return cWorkingDir() + u"tdata/mwtk_names.json"_q;
}

[[nodiscard]] QString KeyFor(not_null<const PeerData*> peer) {
	return QString::number(SerializePeerId(peer->session().userPeerId()))
		+ ':'
		+ QString::number(SerializePeerId(peer->id));
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
	const auto object = QJsonDocument::fromJson(file.readAll()).object();
	for (auto i = object.begin(); i != object.end(); ++i) {
		const auto value = i.value().toString().trimmed();
		if (!value.isEmpty()) {
			state.names[i.key()] = value;
		}
	}
}

void Save() {
	auto object = QJsonObject();
	for (const auto &[key, value] : State().names) {
		object.insert(key, value);
	}
	QDir().mkpath(cWorkingDir() + u"tdata"_q);
	auto file = QSaveFile(StoragePath());
	if (file.open(QIODevice::WriteOnly)) {
		file.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
		file.commit();
	}
}

void Refresh(not_null<PeerData*> peer) {
	peer->mwtkRefreshName();
}

} // namespace

QString Get(not_null<const PeerData*> peer) {
	Load();
	const auto &names = State().names;
	const auto i = names.find(KeyFor(peer));
	return (i != names.end()) ? i->second : QString();
}

QString Original(not_null<const PeerData*> peer) {
	const auto &originals = State().originals;
	const auto i = originals.find(peer.get());
	return (i != originals.end()) ? i->second : peer->name();
}

QString Resolve(not_null<const PeerData*> peer, const QString &serverName) {
	State().originals[peer.get()] = serverName;
	const auto local = Get(peer);
	return local.isEmpty() ? serverName : local;
}

void Set(not_null<PeerData*> peer, const QString &name) {
	Load();
	auto &names = State().names;
	const auto key = KeyFor(peer);
	const auto value = name.trimmed();
	if (value.isEmpty()) {
		names.erase(key);
	} else {
		names[key] = value;
	}
	Save();
	Refresh(peer);
}

void ShowEditBox(
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(QString::fromUtf8("Своё название")));
		const auto field = box->addRow(object_ptr<Ui::InputField>(
			box,
			st::defaultInputField,
			rpl::single(Original(peer)),
			Get(peer)));
		field->setMaxLength(128);
		box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			rpl::single(QString::fromUtf8(
				"Название видишь только ты, у остальных всё как было.")),
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
			box->addLeftButton(rpl::single(QString::fromUtf8("Вернуть")), [=] {
				Set(peer, QString());
				box->closeBox();
			});
		}
	}));
}

QJsonObject Export() {
	Load();
	auto result = QJsonObject();
	for (const auto &[key, value] : State().names) {
		result.insert(key, value);
	}
	return result;
}

void Import(const QJsonObject &object) {
	Load();
	auto &names = State().names;
	for (auto i = object.begin(); i != object.end(); ++i) {
		const auto value = i.value().toString().trimmed();
		if (!value.isEmpty()) {
			names[i.key()] = value;
		}
	}
	Save();
}

} // namespace Mwtk::Names
