/*
This file is part of MWTKgram, a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mwtk/mwtk_covers.h"

#include "core/application.h"
#include "core/file_utilities.h"
#include "data/data_document.h"
#include "settings.h"
#include "window/window_session_controller.h"

#include <QtCore/QCryptographicHash>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtGui/QImageReader>

#include <map>

namespace Mwtk::CustomCovers {
namespace {

constexpr auto kSide = 640;

struct State {
	std::map<QString, QImage> cache; // key -> image (null: none)
	rpl::event_stream<> changes;
};

[[nodiscard]] State &Current() {
	static auto result = State();
	return result;
}

[[nodiscard]] QString KeyFor(not_null<DocumentData*> document) {
	auto text = QString();
	if (const auto song = document->song()) {
		text = song->performer.trimmed().toLower()
			+ u"\n"_q
			+ song->title.trimmed().toLower();
	}
	if (text.trimmed().isEmpty()) {
		text = u"doc"_q + QString::number(document->id);
	}
	return QString::fromLatin1(QCryptographicHash::hash(
		text.toUtf8(),
		QCryptographicHash::Md5).toHex());
}

[[nodiscard]] QString Folder() {
	return cWorkingDir() + u"tdata/mwtk_covers/"_q;
}

[[nodiscard]] QString PathFor(const QString &key) {
	return Folder() + key + u".jpg"_q;
}

} // namespace

QImage Get(DocumentData *document) {
	if (!document) {
		return QImage();
	}
	const auto key = KeyFor(document);
	auto &cache = Current().cache;
	if (const auto i = cache.find(key); i != end(cache)) {
		return i->second;
	}
	auto image = QImage();
	const auto path = PathFor(key);
	if (QFile::exists(path)) {
		image = QImage(path);
	}
	cache.emplace(key, image);
	return image;
}

bool Has(DocumentData *document) {
	return !Get(document).isNull();
}

void Pick(
		not_null<Window::SessionController*> controller,
		not_null<DocumentData*> document) {
	const auto key = KeyFor(document);
	FileDialog::GetOpenPath(
		Core::App().getFileDialogParent(),
		QString::fromUtf8("Обложка песни"),
		u"Image (*.jpg *.jpeg *.png *.webp *.bmp)"_q,
		crl::guard(controller, [=](const FileDialog::OpenResult &result) {
			auto image = QImage();
			if (!result.remoteContent.isEmpty()) {
				image = QImage::fromData(result.remoteContent);
			} else if (!result.paths.isEmpty()) {
				auto reader = QImageReader(result.paths.front());
				reader.setAutoTransform(true);
				image = reader.read();
			}
			if (image.isNull()) {
				controller->showToast(QString::fromUtf8(
					"Не получилось открыть картинку."));
				return;
			}
			// A square from the middle, not bigger than needed.
			const auto side = std::min(image.width(), image.height());
			image = image.copy(
				(image.width() - side) / 2,
				(image.height() - side) / 2,
				side,
				side);
			if (side > kSide) {
				image = image.scaled(
					kSide,
					kSide,
					Qt::IgnoreAspectRatio,
					Qt::SmoothTransformation);
			}
			QDir().mkpath(Folder());
			image.convertToFormat(QImage::Format_RGB32).save(
				PathFor(key),
				"JPG",
				90);
			Current().cache[key] = image;
			Current().changes.fire({});
			controller->showToast(QString::fromUtf8("Обложка сохранена."));
		}));
}

void Remove(not_null<DocumentData*> document) {
	const auto key = KeyFor(document);
	QFile::remove(PathFor(key));
	Current().cache[key] = QImage();
	Current().changes.fire({});
}

rpl::producer<> Changes() {
	return Current().changes.events();
}

} // namespace Mwtk::CustomCovers
