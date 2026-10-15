/*
This file is part of MWTKgram, a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mwtk/mwtk_updater.h"

#include "mwtk/mwtk_build.h"
#include "base/timer.h"
#include "core/application.h"
#include "settings.h"
#include "ui/layers/generic_box.h"
#include "ui/toast/toast.h"
#include "ui/widgets/labels.h"
#include "window/window_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QProcess>
#include <QtCore/QStandardPaths>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

namespace Mwtk::Updater {
namespace {

constexpr auto kReleasesUrl
	= "https://api.github.com/repos/doxmev/MWTKgram/releases?per_page=20";
constexpr auto kInstallerName = "MWTKgram-Setup.exe";
constexpr auto kFirstCheckDelay = crl::time(20 * 1000);
constexpr auto kCheckPeriod = crl::time(6 * 60 * 60 * 1000);

struct Found {
	int build = 0;
	QString url;
};

struct State {
	std::unique_ptr<QNetworkAccessManager> network;
	std::unique_ptr<base::Timer> timer;
	int offered = 0;
	bool checking = false;
	bool downloading = false;
};

[[nodiscard]] State &Current() {
	static auto result = State();
	return result;
}

[[nodiscard]] QNetworkAccessManager *Network() {
	auto &state = Current();
	if (!state.network) {
		state.network = std::make_unique<QNetworkAccessManager>();
	}
	return state.network.get();
}

[[nodiscard]] QNetworkRequest MakeRequest(const QUrl &url, int timeout) {
	auto request = QNetworkRequest(url);
	request.setHeader(
		QNetworkRequest::UserAgentHeader,
		u"MWTKgram-Updater"_q);
	request.setAttribute(
		QNetworkRequest::RedirectPolicyAttribute,
		QNetworkRequest::NoLessSafeRedirectPolicy);
	request.setTransferTimeout(timeout);
	return request;
}

void Toast(const QString &text) {
	if (const auto window = Core::App().activePrimaryWindow()) {
		window->showToast(TextWithEntities{ text });
	}
}

[[nodiscard]] Found Parse(const QByteArray &json) {
	auto result = Found();
	for (const auto &value : QJsonDocument::fromJson(json).array()) {
		const auto release = value.toObject();
		if (release.value(u"draft"_q).toBool()) {
			continue;
		}
		const auto tag = release.value(u"tag_name"_q).toString();
		if (!tag.startsWith(u"build-"_q)) {
			continue;
		}
		const auto build = tag.mid(6).toInt();
		if (build <= result.build) {
			continue;
		}
		for (const auto &asset : release.value(u"assets"_q).toArray()) {
			const auto object = asset.toObject();
			if (object.value(u"name"_q).toString()
				== QString::fromLatin1(kInstallerName)) {
				result.build = build;
				result.url = object.value(
					u"browser_download_url"_q).toString();
				break;
			}
		}
	}
	return result;
}

void RunInstaller(const QString &path) {
#ifdef Q_OS_WIN
	// The installer closes nothing by itself: the app quits right after
	// starting it, the installer waits for the file and starts it again.
	const auto started = QProcess::startDetached(path, {
		u"/SILENT"_q,
		u"/SUPPRESSMSGBOXES"_q,
		u"/NORESTART"_q,
		u"/OLDDIR="_q + QDir::toNativeSeparators(cExeDir()),
	});
	if (started) {
		Core::Quit();
	} else {
		Toast(QString::fromUtf8("Не получилось запустить установщик."));
	}
#else // Q_OS_WIN
	Toast(QString::fromUtf8("Обновление ставится установщиком только на Windows."));
#endif // Q_OS_WIN
}

void Download(Found found) {
	auto &state = Current();
	if (state.downloading) {
		return;
	}
	state.downloading = true;
	Toast(QString::fromUtf8("Скачиваю обновление…"));
	const auto reply = Network()->get(MakeRequest(QUrl(found.url), 60000));
	QObject::connect(reply, &QNetworkReply::finished, [=] {
		reply->deleteLater();
		Current().downloading = false;
		if (reply->error() != QNetworkReply::NoError) {
			Toast(QString::fromUtf8("Не получилось скачать обновление. Проверь интернет."));
			return;
		}
		const auto bytes = reply->readAll();
		if (bytes.size() < 1024 * 1024) {
			Toast(QString::fromUtf8("Файл обновления повреждён, попробуй позже."));
			return;
		}
		const auto folder = QStandardPaths::writableLocation(
			QStandardPaths::TempLocation);
		const auto path = folder
			+ u"/MWTKgram-Setup-%1.exe"_q.arg(found.build);
		auto file = QFile(path);
		if (!file.open(QIODevice::WriteOnly)
			|| file.write(bytes) != bytes.size()) {
			Toast(QString::fromUtf8("Не получилось сохранить обновление."));
			return;
		}
		file.close();
		RunInstaller(path);
	});
}

void Offer(Found found) {
	const auto window = Core::App().activePrimaryWindow();
	if (!window) {
		return;
	}
	Current().offered = found.build;
	window->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(QString::fromUtf8("Вышло обновление")));
		box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			QString::fromUtf8(
				"Доступна новая версия MWTKgram (сборка %1, у тебя %2).\n\n"
				"Установщик скачается, обновит клиент и запустит его снова. "
				"Чаты и вход в аккаунт сохранятся."
			).arg(found.build).arg(CurrentBuild()),
			st::boxLabel));
		box->addButton(rpl::single(QString::fromUtf8("Обновить")), [=] {
			box->closeBox();
			Download(found);
		});
		box->addButton(rpl::single(QString::fromUtf8("Позже")), [=] {
			box->closeBox();
		});
	}));
}

void Check(bool manual) {
	auto &state = Current();
	if (state.checking || state.downloading) {
		return;
	}
	if (!CurrentBuild()) {
		if (manual) {
			Toast(QString::fromUtf8("Это локальная сборка, она не обновляется."));
		}
		return;
	}
	state.checking = true;
	const auto reply = Network()->get(
		MakeRequest(QUrl(QString::fromLatin1(kReleasesUrl)), 20000));
	QObject::connect(reply, &QNetworkReply::finished, [=] {
		reply->deleteLater();
		Current().checking = false;
		if (reply->error() != QNetworkReply::NoError) {
			if (manual) {
				Toast(QString::fromUtf8("Не получилось проверить обновления."));
			}
			return;
		}
		const auto found = Parse(reply->readAll());
		if (found.build > CurrentBuild() && !found.url.isEmpty()) {
			if (manual || found.build != Current().offered) {
				Offer(found);
			}
		} else if (manual) {
			Toast(QString::fromUtf8("У тебя последняя версия."));
		}
	});
}

} // namespace

int CurrentBuild() {
	return MWTK_BUILD_NUMBER;
}

void Setup() {
	auto &state = Current();
	if (state.timer || !CurrentBuild()) {
		return;
	}
	state.timer = std::make_unique<base::Timer>([] {
		Check(false);
		Current().timer->callOnce(kCheckPeriod);
	});
	state.timer->callOnce(kFirstCheckDelay);
}

void CheckNow() {
	Check(true);
}

} // namespace Mwtk::Updater
