/*
This file is part of MWTKgram, a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mwtk/mwtk_share.h"

#include "api/api_common.h"
#include "apiwrap.h"
#include "base/options.h"
#include "core/application.h"
#include "core/file_utilities.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "main/main_session.h"
#include "mwtk/mwtk_options.h"
#include "settings.h"
#include "storage/localimageloader.h"
#include "storage/storage_media_prepare.h"
#include "ui/chat/attach/attach_prepare.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/labels.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"

#include <QtCore/QCryptographicHash>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QSaveFile>
#include <QtCore/QStandardPaths>

#include <openssl/evp.h>
#include <openssl/rand.h>

namespace Mwtk::Share {
namespace {

constexpr auto kThemeFormat = 1;
constexpr auto kSyncFormat = 1;
const auto kThemeMagic = QByteArray("MWTKTHEME");
const auto kSyncMagic = QByteArray("MWTKSYNC1");

// Options that belong to the device, never to a theme.
[[nodiscard]] bool DeviceOnly(const QString &id) {
	return id == u"mwtk-gpu"_q
		|| id == u"mwtk-laptop-mode"_q
		|| id == u"mwtk-background-photo"_q
		|| id == u"mwtk-background-video"_q
		|| id == u"mwtk-save-deleted"_q
		|| id == u"mwtk-keep-deleted-in-chat"_q
		|| id == u"mwtk-hide-chats"_q;
}

[[nodiscard]] QJsonValue ToJson(const base::options::details::ValueType &value) {
	if (const auto b = std::get_if<bool>(&value)) {
		return *b;
	} else if (const auto i = std::get_if<int>(&value)) {
		return *i;
	} else if (const auto s = std::get_if<QString>(&value)) {
		return *s;
	}
	return QJsonValue();
}

[[nodiscard]] QJsonObject CollectOptions(bool theme) {
	auto result = QJsonObject();
	for (const auto id : AllOptionIds()) {
		const auto key = QString::fromLatin1(id);
		if (theme && DeviceOnly(key)) {
			continue;
		}
		result.insert(key, ToJson(base::options::details::Lookup(id).value()));
	}
	return result;
}

void ApplyOptions(const QJsonObject &object) {
	for (const auto id : AllOptionIds()) {
		const auto key = QString::fromLatin1(id);
		if (!object.contains(key)) {
			continue;
		}
		auto &option = base::options::details::Lookup(id);
		const auto value = object.value(key);
		const auto &current = option.value();
		if (std::holds_alternative<bool>(current) && value.isBool()) {
			option.set(value.toBool());
		} else if (std::holds_alternative<int>(current) && value.isDouble()) {
			option.set(value.toInt());
		} else if (std::holds_alternative<QString>(current) && value.isString()) {
			option.set(value.toString());
		}
	}
}

[[nodiscard]] QStringList SyncFiles() {
	return {
		u"mwtk_playlists.json"_q,
		u"mwtk_notes.json"_q,
		u"mwtk_names.json"_q,
		u"mwtk_phrases.json"_q,
		u"mwtk_hidden.json"_q,
	};
}

[[nodiscard]] QByteArray KeyFor(not_null<Main::Session*> session) {
	return QCryptographicHash::hash(
		"mwtk-sync:" + QByteArray::number(session->userId().bare),
		QCryptographicHash::Sha256);
}

[[nodiscard]] QByteArray Crypt(
		const QByteArray &data,
		const QByteArray &key,
		const QByteArray &iv) {
	auto result = QByteArray(data.size(), Qt::Uninitialized);
	const auto context = EVP_CIPHER_CTX_new();
	if (!context) {
		return QByteArray();
	}
	auto length = 0;
	const auto ok = EVP_EncryptInit_ex(
			context,
			EVP_aes_256_ctr(),
			nullptr,
			reinterpret_cast<const unsigned char*>(key.constData()),
			reinterpret_cast<const unsigned char*>(iv.constData()))
		&& EVP_EncryptUpdate(
			context,
			reinterpret_cast<unsigned char*>(result.data()),
			&length,
			reinterpret_cast<const unsigned char*>(data.constData()),
			data.size());
	EVP_CIPHER_CTX_free(context);
	return ok ? result : QByteArray();
}

[[nodiscard]] QString TempPath(const QString &name) {
	const auto folder = QStandardPaths::writableLocation(
		QStandardPaths::TempLocation);
	QDir().mkpath(folder);
	return folder + '/' + name;
}

[[nodiscard]] bool WriteFile(const QString &path, const QByteArray &data) {
	auto file = QSaveFile(path);
	if (!file.open(QIODevice::WriteOnly)) {
		return false;
	}
	file.write(data);
	return file.commit();
}

void ShowDone(
		not_null<Window::SessionController*> controller,
		const QString &title,
		const QString &text,
		bool restart) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(title));
		box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			rpl::single(text),
			st::boxLabel));
		if (restart) {
			box->addButton(rpl::single(QString::fromUtf8("Перезапустить")), [] {
				Core::Restart();
			});
			box->addButton(rpl::single(QString::fromUtf8("Позже")), [=] {
				box->closeBox();
			});
		} else {
			box->addButton(rpl::single(QString::fromUtf8("Отлично")), [=] {
				box->closeBox();
			});
		}
	}));
}

void ImportTheme(
		not_null<Window::SessionController*> controller,
		const QJsonObject &root) {
	const auto name = root.value(u"name"_q).toString(
		QString::fromUtf8("Тема MWTK"));
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(QString::fromUtf8("Тема MWTK")));
		box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			rpl::single(QString::fromUtf8(
				"Применить тему «%1»? Цвета, сцена, эффекты и иконка "
				"заменятся на те, что в файле.").arg(name)),
			st::boxLabel));
		box->addButton(rpl::single(QString::fromUtf8("Применить")), [=] {
			ApplyOptions(root.value(u"options"_q).toObject());
			box->closeBox();
		});
		box->addButton(rpl::single(QString::fromUtf8("Отмена")), [=] {
			box->closeBox();
		});
	}));
}

void ImportSync(
		not_null<Window::SessionController*> controller,
		const QByteArray &bytes) {
	const auto session = &controller->session();
	const auto iv = bytes.mid(kSyncMagic.size(), 16);
	const auto encrypted = bytes.mid(kSyncMagic.size() + 16);
	const auto plain = Crypt(encrypted, KeyFor(session), iv);
	const auto root = QJsonDocument::fromJson(plain).object();
	if (root.value(u"format"_q).toInt() != kSyncFormat) {
		controller->showToast(QString::fromUtf8(
			"Этот файл синхронизации от другого аккаунта."));
		return;
	}
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(QString::fromUtf8("Синхронизация MWTK")));
		box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			rpl::single(QString::fromUtf8(
				"Загрузить настройки MWTK, плейлисты, заметки, свои названия, "
				"фразы и скрытые чаты из этого файла? Текущие заменятся.")),
			st::boxLabel));
		box->addButton(rpl::single(QString::fromUtf8("Загрузить")), [=] {
			ApplyOptions(root.value(u"options"_q).toObject());
			const auto files = root.value(u"files"_q).toObject();
			QDir().mkpath(cWorkingDir() + u"tdata"_q);
			for (const auto &name : SyncFiles()) {
				if (files.contains(name)) {
					WriteFile(
						cWorkingDir() + u"tdata/"_q + name,
						files.value(name).toString().toUtf8());
				}
			}
			box->closeBox();
			ShowDone(
				controller,
				QString::fromUtf8("Готово"),
				QString::fromUtf8(
					"Настройки загружены. Чтобы плейлисты, заметки и скрытые "
					"чаты обновились, перезапусти MWTKgram."),
				true);
		});
		box->addButton(rpl::single(QString::fromUtf8("Отмена")), [=] {
			box->closeBox();
		});
	}));
}

} // namespace

void ExportTheme(not_null<Window::SessionController*> controller) {
	auto root = QJsonObject();
	root.insert(u"format"_q, kThemeFormat);
	root.insert(u"name"_q, QString::fromUtf8("Тема MWTK от %1").arg(
		controller->session().user()->name()));
	root.insert(u"options"_q, CollectOptions(true));
	const auto data = kThemeMagic
		+ QJsonDocument(root).toJson(QJsonDocument::Indented);
	FileDialog::GetWritePath(
		Core::App().getFileDialogParent(),
		QString::fromUtf8("Сохранить тему MWTK"),
		u"MWTK theme (*.mwtktheme)"_q,
		u"MWTK.mwtktheme"_q,
		crl::guard(controller, [=](QString &&path) {
			if (path.isEmpty()) {
				return;
			}
			if (!path.endsWith(u".mwtktheme"_q, Qt::CaseInsensitive)) {
				path += u".mwtktheme"_q;
			}
			if (WriteFile(path, data)) {
				ShowDone(
					controller,
					QString::fromUtf8("Тема сохранена"),
					QString::fromUtf8(
						"Выложи файл в канал или отправь другу. В MWTKgram "
						"достаточно нажать на него, чтобы применить тему."),
					false);
			}
		}));
}

void SendSync(not_null<Window::SessionController*> controller) {
	const auto session = &controller->session();
	auto root = QJsonObject();
	root.insert(u"format"_q, kSyncFormat);
	root.insert(u"options"_q, CollectOptions(false));
	auto files = QJsonObject();
	for (const auto &name : SyncFiles()) {
		auto file = QFile(cWorkingDir() + u"tdata/"_q + name);
		if (file.open(QIODevice::ReadOnly)) {
			files.insert(name, QString::fromUtf8(file.readAll()));
		}
	}
	root.insert(u"files"_q, files);
	auto iv = QByteArray(16, Qt::Uninitialized);
	RAND_bytes(reinterpret_cast<unsigned char*>(iv.data()), iv.size());
	const auto plain = QJsonDocument(root).toJson(QJsonDocument::Compact);
	const auto encrypted = Crypt(plain, KeyFor(session), iv);
	if (encrypted.isEmpty()) {
		return;
	}
	const auto path = TempPath(u"MWTK-sync.mwtk"_q);
	if (!WriteFile(path, kSyncMagic + iv + encrypted)) {
		return;
	}
	auto list = Storage::PrepareMediaList(QStringList{ path }, 0, false);
	if (list.error != Ui::PreparedList::Error::None) {
		return;
	}
	const auto saved = session->data().history(session->user());
	auto action = Api::SendAction(saved);
	action.clearDraft = false;
	session->api().sendFiles(
		std::move(list),
		SendMediaType::File,
		nullptr,
		action);
	controller->showToast(QString::fromUtf8(
		"Файл синхронизации отправлен в «Избранное». Открой его в MWTKgram "
		"на телефоне или другом компьютере."));
}

bool HandleOpen(
		not_null<Window::SessionController*> controller,
		const QString &path) {
	const auto lower = path.toLower();
	if (!lower.endsWith(u".mwtktheme"_q) && !lower.endsWith(u".mwtk"_q)) {
		return false;
	}
	auto file = QFile(path);
	if (!file.open(QIODevice::ReadOnly) || file.size() > 8 * 1024 * 1024) {
		return false;
	}
	const auto bytes = file.readAll();
	if (bytes.startsWith(kThemeMagic)) {
		const auto root = QJsonDocument::fromJson(
			bytes.mid(kThemeMagic.size())).object();
		if (root.value(u"format"_q).toInt() == kThemeFormat) {
			ImportTheme(controller, root);
			return true;
		}
	} else if (bytes.startsWith(kSyncMagic)) {
		ImportSync(controller, bytes);
		return true;
	}
	return false;
}

} // namespace Mwtk::Share
