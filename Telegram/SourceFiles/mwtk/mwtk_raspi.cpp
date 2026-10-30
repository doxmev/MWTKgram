/*
This file is part of RASPIgram, a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mwtk/mwtk_raspi.h"

#include "api/api_common.h"
#include "apiwrap.h"
#include "base/options.h"
#include "base/timer.h"
#include "base/unixtime.h"
#include "core/application.h"
#include "data/data_session.h"
#include "history/history.h"
#include "main/main_session.h"
#include "mwtk/mwtk_options.h"
#include "settings.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_widgets.h"

#include <QtCore/QDir>
#include <QtCore/QPointer>
#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QSaveFile>
#include <QtCore/QUrlQuery>
#include <QtGui/QClipboard>
#include <QtGui/QGuiApplication>
#include <QtGui/QPainter>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>
#include <QtWidgets/QMenu>

namespace Mwtk::Raspi {
namespace {

[[nodiscard]] QString Utf(const char *text) {
	return QString::fromUtf8(text);
}

[[nodiscard]] QNetworkAccessManager &Network() {
	static auto result = std::make_unique<QNetworkAccessManager>();
	return *result;
}

[[nodiscard]] QString StoredString(const char id[]) {
	return base::options::value<QString>(id);
}

void SetString(const char id[], const QString &value) {
	base::options::lookup<QString>(id).set(QString(value));
}

// A JSON request; done gets the parsed answer or an error text.
void PostJson(
		QNetworkRequest request,
		const QJsonObject &body,
		Fn<void(QJsonObject, QString)> done) {
	request.setHeader(
		QNetworkRequest::ContentTypeHeader,
		u"application/json"_q);
	request.setTransferTimeout(90000);
	const auto reply = Network().post(
		request,
		QJsonDocument(body).toJson(QJsonDocument::Compact));
	QObject::connect(reply, &QNetworkReply::finished, [=] {
		reply->deleteLater();
		const auto data = reply->readAll();
		const auto object = QJsonDocument::fromJson(data).object();
		if (reply->error() == QNetworkReply::NoError && !object.isEmpty()) {
			done(object, QString());
			return;
		}
		auto error = object.value(u"error"_q).toObject().value(
			u"message"_q).toString();
		if (error.isEmpty()) {
			error = object.value(u"error"_q).toString();
		}
		if (error.isEmpty()) {
			error = reply->errorString();
		}
		done(QJsonObject(), error.isEmpty() ? Utf("Нет ответа") : error);
	});
}

void GetJson(const QUrl &url, Fn<void(QJsonObject, QString)> done) {
	auto request = QNetworkRequest(url);
	request.setHeader(
		QNetworkRequest::UserAgentHeader,
		u"RASPIgram (https://github.com/doxmev/RASPIgram)"_q);
	request.setTransferTimeout(20000);
	const auto reply = Network().get(request);
	QObject::connect(reply, &QNetworkReply::finished, [=] {
		reply->deleteLater();
		const auto object = QJsonDocument::fromJson(reply->readAll()).object();
		if (reply->error() != QNetworkReply::NoError || object.isEmpty()) {
			done(QJsonObject(), reply->errorString());
		} else {
			done(object, QString());
		}
	});
}

[[nodiscard]] Window::SessionController *ActiveController() {
	const auto window = Core::App().activePrimaryWindow();
	return window ? window->sessionController() : nullptr;
}

// ---------------------------------------------------------------- Checklists

struct ChecklistItem {
	QString text;
	bool done = false;
};

struct Checklist {
	QString title;
	std::vector<ChecklistItem> items;
};

[[nodiscard]] QString ChecklistsPath() {
	return cWorkingDir() + u"tdata/raspi_checklists.json"_q;
}

[[nodiscard]] std::vector<Checklist> &Lists() {
	static auto result = [] {
		auto lists = std::vector<Checklist>();
		auto file = QFile(ChecklistsPath());
		if (!file.open(QIODevice::ReadOnly)) {
			return lists;
		}
		const auto array = QJsonDocument::fromJson(file.readAll()).array();
		for (const auto &value : array) {
			const auto object = value.toObject();
			auto list = Checklist{ object.value(u"title"_q).toString() };
			for (const auto &item : object.value(u"items"_q).toArray()) {
				const auto o = item.toObject();
				list.items.push_back({
					o.value(u"text"_q).toString(),
					o.value(u"done"_q).toBool(),
				});
			}
			lists.push_back(std::move(list));
		}
		return lists;
	}();
	return result;
}

void SaveLists() {
	auto array = QJsonArray();
	for (const auto &list : Lists()) {
		auto items = QJsonArray();
		for (const auto &item : list.items) {
			items.push_back(QJsonObject{
				{ u"text"_q, item.text },
				{ u"done"_q, item.done },
			});
		}
		array.push_back(QJsonObject{
			{ u"title"_q, list.title },
			{ u"items"_q, items },
		});
	}
	QDir().mkpath(cWorkingDir() + u"tdata"_q);
	auto file = QSaveFile(ChecklistsPath());
	if (file.open(QIODevice::WriteOnly)) {
		file.write(QJsonDocument(array).toJson(QJsonDocument::Indented));
		file.commit();
	}
}

[[nodiscard]] QString ChecklistText(const Checklist &list) {
	auto result = list.title;
	for (const auto &item : list.items) {
		result += '\n';
		result += (item.done ? Utf("✅ ") : Utf("⬜ ")) + item.text;
	}
	return result;
}

void ShowChecklist(
		not_null<Window::SessionController*> controller,
		int index,
		Fn<void(QString)> insert);

void FillChecklist(
		not_null<Ui::VerticalLayout*> container,
		int index) {
	container->clear();
	auto &lists = Lists();
	if (index < 0 || index >= int(lists.size())) {
		return;
	}
	const auto &items = lists[index].items;
	if (items.empty()) {
		container->add(
			object_ptr<Ui::FlatLabel>(
				container,
				rpl::single(Utf("Пока пусто — добавь первый пункт ниже.")),
				st::boxDividerLabel),
			st::boxRowPadding);
	}
	for (auto i = 0; i != int(items.size()); ++i) {
		const auto row = container->add(
			object_ptr<Ui::RpWidget>(container),
			st::boxRowPadding);
		const auto check = Ui::CreateChild<Ui::Checkbox>(
			row,
			items[i].text,
			items[i].done,
			st::defaultBoxCheckbox);
		const auto remove = Ui::CreateChild<Ui::LinkButton>(
			row,
			Utf("✕"),
			st::defaultLinkButton);
		check->checkedChanges(
		) | rpl::on_next([=](bool checked) {
			auto &lists = Lists();
			if (index < int(lists.size()) && i < int(lists[index].items.size())) {
				lists[index].items[i].done = checked;
				SaveLists();
			}
		}, check->lifetime());
		remove->setClickedCallback([=] {
			auto &lists = Lists();
			if (index < int(lists.size()) && i < int(lists[index].items.size())) {
				auto &items = lists[index].items;
				items.erase(items.begin() + i);
				SaveLists();
				crl::on_main(container, [=] {
					FillChecklist(container, index);
					container->resizeToWidth(container->width());
				});
			}
		});
		row->widthValue(
		) | rpl::on_next([=](int width) {
			const auto skip = remove->width() + st::boxRowPadding.right();
			check->resizeToNaturalWidth(std::max(width - skip, 1));
			check->moveToLeft(0, 0, width);
			remove->moveToRight(0, (check->height() - remove->height()) / 2, width);
			row->resize(width, check->height() + st::boxLittleSkip);
		}, row->lifetime());
	}
}

void ShowChecklist(
		not_null<Window::SessionController*> controller,
		int index,
		Fn<void(QString)> insert) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		const auto &lists = Lists();
		if (index < 0 || index >= int(lists.size())) {
			box->closeBox();
			return;
		}
		box->setTitle(rpl::single(lists[index].title));
		box->setWidth(st::boxWideWidth);
		const auto items = box->addRow(
			object_ptr<Ui::VerticalLayout>(box),
			style::margins());
		FillChecklist(items, index);
		const auto field = box->addRow(object_ptr<Ui::InputField>(
			box,
			st::defaultInputField,
			rpl::single(Utf("Новый пункт и Enter"))));
		field->setMaxLength(500);
		const auto add = [=] {
			const auto text = field->getLastText().trimmed();
			auto &lists = Lists();
			if (text.isEmpty() || index >= int(lists.size())) {
				return;
			}
			lists[index].items.push_back({ text, false });
			SaveLists();
			field->clear();
			FillChecklist(items, index);
			items->resizeToWidth(items->width());
		};
		field->submits(
		) | rpl::on_next([=] {
			add();
		}, field->lifetime());
		box->setFocusCallback([=] {
			field->setFocusFast();
		});
		if (insert) {
			box->addButton(rpl::single(Utf("В сообщение")), [=] {
				insert(ChecklistText(Lists()[index]));
				box->closeBox();
			});
		}
		box->addButton(rpl::single(Utf("Скопировать")), [=] {
			QGuiApplication::clipboard()->setText(
				ChecklistText(Lists()[index]));
			controller->showToast(Utf("Список скопирован"));
		});
		box->addLeftButton(rpl::single(Utf("Удалить список")), [=] {
			auto &lists = Lists();
			if (index < int(lists.size())) {
				lists.erase(lists.begin() + index);
				SaveLists();
			}
			box->closeBox();
			ShowChecklists(controller, insert);
		});
		box->addButton(rpl::single(Utf("Готово")), [=] {
			box->closeBox();
		});
	}));
}

// ------------------------------------------------------------------ AI editor

[[nodiscard]] QString DefaultModel(int provider) {
	switch (provider) {
	case 1: return u"claude-haiku-4-5"_q;
	case 2: return u"gemini-2.5-flash"_q;
	}
	return u"gpt-4o-mini"_q;
}

[[nodiscard]] int CurrentProvider() {
	return std::clamp(OptionInt(kOptionAiProvider), 0, 3);
}

[[nodiscard]] QString CurrentModel() {
	const auto model = StoredString(kOptionAiModel).trimmed();
	return model.isEmpty() ? DefaultModel(CurrentProvider()) : model;
}

[[nodiscard]] QString OpenAiBase() {
	auto base = (CurrentProvider() == 3)
		? StoredString(kOptionAiUrl).trimmed()
		: QString();
	if (base.isEmpty()) {
		base = u"https://api.openai.com/v1"_q;
	}
	while (base.endsWith('/')) {
		base.chop(1);
	}
	return base;
}

void AskText(
		const QString &system,
		const QString &user,
		Fn<void(QString, QString)> done) {
	const auto key = StoredString(kOptionAiKey).trimmed();
	const auto model = CurrentModel();
	const auto provider = CurrentProvider();
	if (provider == 1) {
		auto request = QNetworkRequest(
			QUrl(u"https://api.anthropic.com/v1/messages"_q));
		request.setRawHeader("x-api-key", key.toUtf8());
		request.setRawHeader("anthropic-version", "2023-06-01");
		PostJson(request, QJsonObject{
			{ u"model"_q, model },
			{ u"max_tokens"_q, 1500 },
			{ u"system"_q, system },
			{ u"messages"_q, QJsonArray{ QJsonObject{
				{ u"role"_q, u"user"_q },
				{ u"content"_q, user },
			} } },
		}, [=](QJsonObject answer, QString error) {
			auto text = QString();
			for (const auto &part : answer.value(u"content"_q).toArray()) {
				text += part.toObject().value(u"text"_q).toString();
			}
			done(text.trimmed(), text.isEmpty() ? error : QString());
		});
	} else if (provider == 2) {
		auto request = QNetworkRequest(QUrl(
			u"https://generativelanguage.googleapis.com/v1beta/models/"_q
			+ model
			+ u":generateContent"_q));
		request.setRawHeader("x-goog-api-key", key.toUtf8());
		PostJson(request, QJsonObject{
			{ u"systemInstruction"_q, QJsonObject{
				{ u"parts"_q, QJsonArray{ QJsonObject{
					{ u"text"_q, system },
				} } },
			} },
			{ u"contents"_q, QJsonArray{ QJsonObject{
				{ u"role"_q, u"user"_q },
				{ u"parts"_q, QJsonArray{ QJsonObject{
					{ u"text"_q, user },
				} } },
			} } },
		}, [=](QJsonObject answer, QString error) {
			auto text = QString();
			const auto candidates = answer.value(u"candidates"_q).toArray();
			if (!candidates.isEmpty()) {
				const auto parts = candidates.first().toObject().value(
					u"content"_q).toObject().value(u"parts"_q).toArray();
				for (const auto &part : parts) {
					text += part.toObject().value(u"text"_q).toString();
				}
			}
			done(text.trimmed(), text.isEmpty() ? error : QString());
		});
	} else {
		auto request = QNetworkRequest(
			QUrl(OpenAiBase() + u"/chat/completions"_q));
		request.setRawHeader("Authorization", "Bearer " + key.toUtf8());
		PostJson(request, QJsonObject{
			{ u"model"_q, model },
			{ u"messages"_q, QJsonArray{
				QJsonObject{
					{ u"role"_q, u"system"_q },
					{ u"content"_q, system },
				},
				QJsonObject{
					{ u"role"_q, u"user"_q },
					{ u"content"_q, user },
				},
			} },
		}, [=](QJsonObject answer, QString error) {
			const auto choices = answer.value(u"choices"_q).toArray();
			const auto text = choices.isEmpty()
				? QString()
				: choices.first().toObject().value(
					u"message"_q).toObject().value(
						u"content"_q).toString().trimmed();
			done(text, text.isEmpty() ? error : QString());
		});
	}
}

void AskImage(const QString &prompt, Fn<void(QImage, QString)> done) {
	const auto key = StoredString(kOptionAiKey).trimmed();
	auto request = QNetworkRequest(
		QUrl(OpenAiBase() + u"/images/generations"_q));
	request.setRawHeader("Authorization", "Bearer " + key.toUtf8());
	PostJson(request, QJsonObject{
		{ u"model"_q, u"gpt-image-1"_q },
		{ u"prompt"_q, prompt },
		{ u"size"_q, u"1024x1024"_q },
	}, [=](QJsonObject answer, QString error) {
		const auto data = answer.value(u"data"_q).toArray();
		const auto encoded = data.isEmpty()
			? QString()
			: data.first().toObject().value(u"b64_json"_q).toString();
		auto image = QImage::fromData(
			QByteArray::fromBase64(encoded.toLatin1()));
		done(image, image.isNull()
			? (error.isEmpty() ? Utf("Картинка не пришла") : error)
			: QString());
	});
}

class ImagePreview final : public Ui::RpWidget {
public:
	using RpWidget::RpWidget;

	void setImage(QImage image) {
		_image = std::move(image);
		resizeToWidth(width());
		update();
	}

protected:
	int resizeGetHeight(int newWidth) override {
		return _image.isNull() ? 0 : std::min(newWidth, 260);
	}

	void paintEvent(QPaintEvent *e) override {
		if (_image.isNull()) {
			return;
		}
		auto p = QPainter(this);
		p.setRenderHint(QPainter::SmoothPixmapTransform);
		const auto side = std::min(width(), height());
		p.drawImage(
			QRect((width() - side) / 2, 0, side, side),
			_image);
	}

private:
	QImage _image;

};

// --------------------------------------------------------------------- Weather

[[nodiscard]] QString WeatherName(int code) {
	if (code == 0) return Utf("☀️ ясно");
	if (code <= 2) return Utf("🌤 переменная облачность");
	if (code == 3) return Utf("☁️ пасмурно");
	if (code == 45 || code == 48) return Utf("🌫 туман");
	if (code >= 51 && code <= 57) return Utf("🌦 морось");
	if (code >= 61 && code <= 67) return Utf("🌧 дождь");
	if (code >= 71 && code <= 77) return Utf("🌨 снег");
	if (code >= 80 && code <= 82) return Utf("🌧 ливень");
	if (code == 85 || code == 86) return Utf("🌨 снегопад");
	if (code >= 95) return Utf("⛈ гроза");
	return Utf("🌡");
}

[[nodiscard]] QString Degrees(double value) {
	const auto rounded = int(std::lround(value));
	return (rounded > 0 ? u"+"_q : QString())
		+ QString::number(rounded).replace('-', QChar(0x2212))
		+ QChar(0x00B0);
}

void FetchWeather(Fn<void(QString, QString)> done) {
	const auto city = StoredString(kOptionWeatherCity).trimmed();
	if (city.isEmpty()) {
		done(QString(), Utf("Сначала укажи город"));
		return;
	}
	auto geo = QUrl(u"https://geocoding-api.open-meteo.com/v1/search"_q);
	auto query = QUrlQuery();
	query.addQueryItem(u"name"_q, city);
	query.addQueryItem(u"count"_q, u"1"_q);
	query.addQueryItem(u"language"_q, u"ru"_q);
	geo.setQuery(query);
	GetJson(geo, [=](QJsonObject found, QString error) {
		const auto results = found.value(u"results"_q).toArray();
		if (results.isEmpty()) {
			done(QString(), error.isEmpty()
				? Utf("Не нашёл такой город")
				: error);
			return;
		}
		const auto place = results.first().toObject();
		const auto name = place.value(u"name"_q).toString();
		auto url = QUrl(u"https://api.open-meteo.com/v1/forecast"_q);
		auto q = QUrlQuery();
		q.addQueryItem(
			u"latitude"_q,
			QString::number(place.value(u"latitude"_q).toDouble()));
		q.addQueryItem(
			u"longitude"_q,
			QString::number(place.value(u"longitude"_q).toDouble()));
		q.addQueryItem(
			u"current"_q,
			u"temperature_2m,weather_code,wind_speed_10m"_q);
		q.addQueryItem(
			u"daily"_q,
			u"temperature_2m_max,temperature_2m_min,"
			"precipitation_probability_max,weather_code"_q);
		q.addQueryItem(u"wind_speed_unit"_q, u"ms"_q);
		q.addQueryItem(u"timezone"_q, u"auto"_q);
		q.addQueryItem(u"forecast_days"_q, u"1"_q);
		url.setQuery(q);
		GetJson(url, [=](QJsonObject forecast, QString error) {
			const auto current = forecast.value(u"current"_q).toObject();
			const auto daily = forecast.value(u"daily"_q).toObject();
			const auto first = [&](const QString &key) {
				const auto list = daily.value(key).toArray();
				return list.isEmpty() ? QJsonValue() : list.first();
			};
			if (current.isEmpty() || daily.isEmpty()) {
				done(QString(), error.isEmpty()
					? Utf("Сервис погоды не ответил")
					: error);
				return;
			}
			const auto date = QLocale(QLocale::Russian).toString(
				QDate::currentDate(),
				u"dddd, d MMMM"_q);
			auto text = Utf("Погода — ") + name + u", "_q + date + '\n';
			text += Utf("Сейчас: ")
				+ Degrees(current.value(u"temperature_2m"_q).toDouble())
				+ u", "_q
				+ WeatherName(current.value(u"weather_code"_q).toInt())
				+ '\n';
			text += Utf("Днём до ")
				+ Degrees(first(u"temperature_2m_max"_q).toDouble())
				+ Utf(", ночью до ")
				+ Degrees(first(u"temperature_2m_min"_q).toDouble())
				+ '\n';
			text += Utf("Осадки: ")
				+ QString::number(
					first(u"precipitation_probability_max"_q).toInt())
				+ Utf("%, ветер ")
				+ QString::number(
					int(std::lround(
						current.value(u"wind_speed_10m"_q).toDouble())))
				+ Utf(" м/с");
			done(text, QString());
		});
	});
}

[[nodiscard]] int WeatherMinutes() {
	return std::clamp(OptionInt(kOptionWeatherTime), 0, 24 * 60 - 1);
}

void SendWeather(
		not_null<Main::Session*> session,
		const QString &text,
		TimeId when) {
	const auto saved = session->data().history(session->userPeerId());
	auto options = Api::SendOptions();
	options.scheduled = std::max(when, base::unixtime::now() + 60);
	auto action = Api::SendAction(saved, options);
	action.clearDraft = false;
	auto message = Api::MessageToSend(action);
	message.textWithTags = TextWithTags{ text };
	message.webPage.removed = true;
	session->api().sendMessage(std::move(message));
}

void CheckWeather() {
	static auto attemptAt = crl::time(0);
	if (!OptionValue(kOptionWeather)
		|| StoredString(kOptionWeatherCity).trimmed().isEmpty()) {
		return;
	}
	const auto now = QDateTime::currentDateTime();
	const auto today = now.date().toString(Qt::ISODate);
	if (StoredString(kOptionWeatherShown) == today) {
		return;
	}
	const auto minutes = now.time().hour() * 60 + now.time().minute();
	// Asked a minute early: the reminder is scheduled right at the time.
	if (minutes + 1 < WeatherMinutes()) {
		return;
	}
	if (attemptAt && crl::now() - attemptAt < 30 * 60 * crl::time(1000)) {
		return;
	}
	const auto controller = ActiveController();
	if (!controller) {
		return;
	}
	attemptAt = crl::now();
	const auto session = base::make_weak(&controller->session());
	const auto at = QDateTime(
		now.date(),
		QTime(WeatherMinutes() / 60, WeatherMinutes() % 60));
	FetchWeather([=](QString text, QString error) {
		const auto strong = session.get();
		if (!strong || text.isEmpty()) {
			return;
		}
		SetString(kOptionWeatherShown, today);
		SendWeather(strong, text, TimeId(at.toSecsSinceEpoch()));
	});
}

} // namespace

void Setup() {
	static auto timer = std::unique_ptr<base::Timer>();
	if (timer) {
		return;
	}
	timer = std::make_unique<base::Timer>([] { CheckWeather(); });
	timer->callEach(60 * crl::time(1000));
	static auto lifetime = rpl::lifetime();
	OptionChanges(
		kOptionWeather
	) | rpl::on_next([] {
		CheckWeather();
	}, lifetime);
}

void AttachToField(
		not_null<Ui::InputField*> field,
		not_null<Window::SessionController*> controller) {
	const auto weak = base::make_weak(controller);
	const auto insert = [=](QString text) {
		field->setText(text);
		field->setCursorPosition(text.size());
		field->setFocus();
	};
	field->addContextMenuHook([=](Ui::InputField::ContextMenuRequest request) {
		request.menu->addSeparator();
		request.menu->addAction(Utf("AI-редактор…"), [=] {
			if (const auto strong = weak.get()) {
				ShowAiEditor(strong, field->getLastText(), insert);
			}
		});
		request.menu->addAction(Utf("Чеклисты…"), [=] {
			if (const auto strong = weak.get()) {
				ShowChecklists(strong, insert);
			}
		});
	});
}

void ShowChecklists(
		not_null<Window::SessionController*> controller,
		Fn<void(QString)> insert) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(Utf("Чеклисты")));
		box->setWidth(st::boxWideWidth);
		const auto &lists = Lists();
		if (lists.empty()) {
			box->addRow(object_ptr<Ui::FlatLabel>(
				box,
				rpl::single(Utf("Списков пока нет. Придумай название ниже "
					"и нажми Enter. Списки хранятся только на этом компьютере.")),
				st::boxDividerLabel));
		}
		for (auto i = 0; i != int(lists.size()); ++i) {
			const auto &list = lists[i];
			const auto done = ranges::count_if(list.items, [](const auto &item) {
				return item.done;
			});
			const auto link = box->addRow(object_ptr<Ui::LinkButton>(
				box,
				list.title
					+ u"  ("_q
					+ QString::number(done)
					+ '/'
					+ QString::number(list.items.size())
					+ ')',
				st::defaultLinkButton));
			link->setClickedCallback([=] {
				box->closeBox();
				ShowChecklist(controller, i, insert);
			});
		}
		const auto field = box->addRow(object_ptr<Ui::InputField>(
			box,
			st::defaultInputField,
			rpl::single(Utf("Новый список"))));
		field->setMaxLength(100);
		const auto create = [=] {
			const auto title = field->getLastText().trimmed();
			if (title.isEmpty()) {
				field->showError();
				return;
			}
			Lists().push_back(Checklist{ title });
			SaveLists();
			const auto index = int(Lists().size()) - 1;
			box->closeBox();
			ShowChecklist(controller, index, insert);
		};
		field->submits(
		) | rpl::on_next([=] {
			create();
		}, field->lifetime());
		box->setFocusCallback([=] {
			field->setFocusFast();
		});
		box->addButton(rpl::single(Utf("Создать")), create);
		box->addButton(rpl::single(Utf("Закрыть")), [=] {
			box->closeBox();
		});
	}));
}

QString AiProviderName(int index) {
	switch (index) {
	case 1: return Utf("Claude");
	case 2: return Utf("Gemini");
	case 3: return Utf("Другой");
	}
	return Utf("OpenAI");
}

void ShowAiSettings(not_null<Window::SessionController*> controller) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(Utf("Ключ для AI-редактора")));
		box->setWidth(st::boxWideWidth);
		box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			rpl::single(Utf("Сервис: ")
				+ AiProviderName(CurrentProvider())
				+ Utf(" (меняется в настройках RASPIgram). "
					"Ключ хранится только на этом компьютере, "
					"запросы идут напрямую в сервис и оплачиваются с этого ключа.")),
			st::boxDividerLabel));
		const auto key = box->addRow(object_ptr<Ui::InputField>(
			box,
			st::defaultInputField,
			rpl::single(Utf("API-ключ")),
			StoredString(kOptionAiKey)));
		const auto model = box->addRow(object_ptr<Ui::InputField>(
			box,
			st::defaultInputField,
			rpl::single(Utf("Модель (пусто — ") + DefaultModel(CurrentProvider()) + ')'),
			StoredString(kOptionAiModel)));
		const auto url = (CurrentProvider() == 3)
			? box->addRow(object_ptr<Ui::InputField>(
				box,
				st::defaultInputField,
				rpl::single(Utf("Адрес API, например https://openrouter.ai/api/v1")),
				StoredString(kOptionAiUrl)))
			: nullptr;
		box->setFocusCallback([=] {
			key->setFocusFast();
		});
		box->addButton(rpl::single(Utf("Сохранить")), [=] {
			SetString(kOptionAiKey, key->getLastText().trimmed());
			SetString(kOptionAiModel, model->getLastText().trimmed());
			if (url) {
				SetString(kOptionAiUrl, url->getLastText().trimmed());
			}
			box->closeBox();
		});
		box->addButton(rpl::single(Utf("Отмена")), [=] {
			box->closeBox();
		});
	}));
}

void ShowAiEditor(
		not_null<Window::SessionController*> controller,
		QString initial,
		Fn<void(QString)> insert) {
	if (StoredString(kOptionAiKey).trimmed().isEmpty()) {
		controller->showToast(Utf("Сначала вставь свой API-ключ"));
		ShowAiSettings(controller);
		return;
	}
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(Utf("AI-редактор · ")
			+ AiProviderName(CurrentProvider())));
		box->setWidth(st::boxWideWidth);
		const auto input = box->addRow(object_ptr<Ui::InputField>(
			box,
			st::newGroupDescription,
			Ui::InputField::Mode::MultiLine,
			rpl::single(Utf("Тема поста или твой текст")),
			initial));
		input->setMaxLength(8000);
		const auto status = box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			QString(),
			st::boxDividerLabel));
		const auto output = box->addRow(object_ptr<Ui::InputField>(
			box,
			st::newGroupDescription,
			Ui::InputField::Mode::MultiLine,
			rpl::single(Utf("Здесь появится результат")),
			QString()));
		output->setMaxLength(16000);
		const auto preview = box->addRow(
			object_ptr<ImagePreview>(box),
			style::margins());
		const auto busy = box->lifetime().make_state<bool>(false);
		const auto image = box->lifetime().make_state<QImage>();
		const auto weak = QPointer<Ui::GenericBox>(box.get());
		const auto run = [=](QString system) {
			const auto text = input->getLastText().trimmed();
			if (*busy || text.isEmpty()) {
				if (text.isEmpty()) {
					input->showError();
				}
				return;
			}
			*busy = true;
			status->setText(Utf("Думаю…"));
			AskText(system, text, [=](QString result, QString error) {
				if (!weak) {
					return;
				}
				*busy = false;
				if (result.isEmpty()) {
					status->setText(Utf("Ошибка: ") + error);
					return;
				}
				status->setText(QString());
				output->setText(result);
			});
		};
		const auto row = box->addRow(object_ptr<Ui::RpWidget>(box));
		auto buttons = std::vector<not_null<Ui::RoundButton*>>();
		const auto addMode = [&](const QString &text, Fn<void()> callback) {
			const auto button = Ui::CreateChild<Ui::RoundButton>(
				row,
				rpl::single(text),
				st::defaultLightButton);
			button->setTextTransform(Ui::RoundButtonTextTransform::NoTransform);
			button->setClickedCallback(std::move(callback));
			buttons.push_back(button);
		};
		addMode(Utf("Пост"), [=] {
			run(Utf("Ты помогаешь вести Telegram-канал. Напиши готовый пост "
				"по теме или черновику пользователя: живо, по делу, с абзацами, "
				"можно пару уместных эмодзи. Ответь только текстом поста."));
		});
		addMode(Utf("Заголовки"), [=] {
			run(Utf("Предложи 5 коротких цепляющих заголовков для этого "
				"поста. Ответь только списком, по одному в строке."));
		});
		addMode(Utf("Улучшить"), [=] {
			run(Utf("Улучши текст: исправь ошибки, сделай его понятнее и "
				"живее, сохрани смысл и манеру автора. Ответь только "
				"готовым текстом."));
		});
		const auto images = (CurrentProvider() == 0 || CurrentProvider() == 3);
		if (images) {
			addMode(Utf("Картинка"), [=] {
				const auto text = input->getLastText().trimmed();
				if (*busy || text.isEmpty()) {
					return;
				}
				*busy = true;
				status->setText(Utf("Рисую… это может занять минуту"));
				AskImage(text, [=](QImage result, QString error) {
					if (!weak) {
						return;
					}
					*busy = false;
					if (result.isNull()) {
						status->setText(Utf("Ошибка: ") + error);
						return;
					}
					*image = result;
					preview->setImage(result);
					QGuiApplication::clipboard()->setImage(result);
					status->setText(Utf("Картинка скопирована — вставь её в чат через Ctrl+V"));
				});
			});
		}
		row->widthValue(
		) | rpl::on_next([=](int width) {
			auto x = 0;
			auto y = 0;
			auto height = 0;
			const auto skip = st::boxLittleSkip;
			for (const auto button : buttons) {
				if (x > 0 && x + button->width() > width) {
					x = 0;
					y += height + skip;
				}
				button->moveToLeft(x, y, width);
				x += button->width() + skip;
				height = std::max(height, button->height());
			}
			row->resize(width, y + height);
		}, row->lifetime());
		box->setFocusCallback([=] {
			input->setFocusFast();
		});
		if (insert) {
			box->addButton(rpl::single(Utf("В сообщение")), [=] {
				const auto text = output->getLastText().trimmed();
				if (text.isEmpty()) {
					return;
				}
				insert(text);
				box->closeBox();
			});
		}
		box->addButton(rpl::single(Utf("Скопировать")), [=] {
			const auto text = output->getLastText().trimmed();
			if (!text.isEmpty()) {
				QGuiApplication::clipboard()->setText(text);
				controller->showToast(Utf("Текст скопирован"));
			}
		});
		box->addLeftButton(rpl::single(Utf("Ключ")), [=] {
			ShowAiSettings(controller);
		});
		box->addButton(rpl::single(Utf("Закрыть")), [=] {
			box->closeBox();
		});
	}));
}

void ShowWeatherNow(not_null<Window::SessionController*> controller) {
	const auto weak = base::make_weak(controller);
	controller->showToast(Utf("Смотрю погоду…"));
	FetchWeather([=](QString text, QString error) {
		const auto strong = weak.get();
		if (!strong) {
			return;
		}
		if (text.isEmpty()) {
			strong->showToast(Utf("Погода: ") + error);
			return;
		}
		strong->show(Box([=](not_null<Ui::GenericBox*> box) {
			box->setTitle(rpl::single(Utf("Погода")));
			box->addRow(object_ptr<Ui::FlatLabel>(
				box,
				rpl::single(text),
				st::boxLabel));
			box->addButton(rpl::single(Utf("Отправить в «Избранное»")), [=] {
				SendWeather(&strong->session(), text, 0);
				strong->showToast(Utf("Придёт в «Избранное» через минуту"));
				box->closeBox();
			});
			box->addButton(rpl::single(Utf("Закрыть")), [=] {
				box->closeBox();
			});
		}));
	});
}

QString WeatherTimeText() {
	const auto minutes = WeatherMinutes();
	return QString::asprintf("%02d:%02d", minutes / 60, minutes % 60);
}

void EditWeatherCity(not_null<Window::SessionController*> controller) {
	EditTextOption(
		controller,
		Utf("Город для погоды"),
		Utf("Например, Москва"),
		kOptionWeatherCity,
		64);
}

void EditWeatherTime(not_null<Window::SessionController*> controller) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(Utf("Во сколько присылать погоду")));
		const auto field = box->addRow(object_ptr<Ui::InputField>(
			box,
			st::defaultInputField,
			rpl::single(Utf("Время, например 08:00")),
			WeatherTimeText()));
		const auto save = [=] {
			const auto time = QTime::fromString(
				field->getLastText().trimmed(),
				u"H:mm"_q);
			if (!time.isValid()) {
				field->showError();
				return;
			}
			base::options::lookup<int>(kOptionWeatherTime).set(
				time.hour() * 60 + time.minute());
			// A new time today: the weather may come again.
			SetString(kOptionWeatherShown, QString());
			box->closeBox();
		};
		field->submits(
		) | rpl::on_next([=] {
			save();
		}, field->lifetime());
		box->setFocusCallback([=] {
			field->setFocusFast();
		});
		box->addButton(rpl::single(Utf("Сохранить")), save);
		box->addButton(rpl::single(Utf("Отмена")), [=] {
			box->closeBox();
		});
	}));
}

void EditTextOption(
		not_null<Window::SessionController*> controller,
		const QString &title,
		const QString &placeholder,
		const char id[],
		int maxLength) {
	const auto option = QString::fromLatin1(id);
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		const auto key = option.toLatin1();
		box->setTitle(rpl::single(title));
		const auto field = box->addRow(object_ptr<Ui::InputField>(
			box,
			st::defaultInputField,
			rpl::single(placeholder),
			base::options::value<QString>(key.constData())));
		field->setMaxLength(maxLength);
		const auto save = [=] {
			base::options::lookup<QString>(key.constData()).set(
				field->getLastText().trimmed());
			box->closeBox();
		};
		field->submits(
		) | rpl::on_next([=] {
			save();
		}, field->lifetime());
		box->setFocusCallback([=] {
			field->setFocusFast();
		});
		box->addButton(rpl::single(Utf("Сохранить")), save);
		box->addButton(rpl::single(Utf("Отмена")), [=] {
			box->closeBox();
		});
	}));
}

} // namespace Mwtk::Raspi
