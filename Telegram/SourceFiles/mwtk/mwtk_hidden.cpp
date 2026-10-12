/*
This file is part of MWTKgram, a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mwtk/mwtk_hidden.h"

#include "base/event_filter.h"
#include "core/application.h"
#include "data/data_peer.h"
#include "data/data_peer_id.h"
#include "data/data_session.h"
#include "history/history.h"
#include "main/main_session.h"
#include "settings.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/password_input.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QCryptographicHash>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QRandomGenerator>
#include <QtCore/QSaveFile>
#include <QtGui/QKeyEvent>

#include <map>
#include <set>

namespace Mwtk::Hidden {
namespace {

constexpr auto kUnlockDuration = crl::time(5 * 60 * 1000);

[[nodiscard]] QString Utf(const char *text) {
	return QString::fromUtf8(text);
}

struct Storage {
	bool loaded = false;
	std::map<quint64, std::set<quint64>> accounts;
	QString salt;
	QString hash;
	crl::time unlockedAt = 0;
};

[[nodiscard]] Storage &State() {
	static auto result = Storage();
	return result;
}

[[nodiscard]] QString StoragePath() {
	return cWorkingDir() + u"tdata/mwtk_hidden.json"_q;
}

[[nodiscard]] quint64 AccountKey(not_null<const PeerData*> peer) {
	return SerializePeerId(peer->session().userPeerId());
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
	const auto root = QJsonDocument::fromJson(file.readAll()).object();
	state.salt = root.value(u"salt"_q).toString();
	state.hash = root.value(u"hash"_q).toString();
	const auto accounts = root.value(u"accounts"_q).toObject();
	for (auto i = accounts.begin(); i != accounts.end(); ++i) {
		auto &set = state.accounts[i.key().toULongLong()];
		for (const auto &value : i.value().toArray()) {
			set.insert(value.toString().toULongLong());
		}
	}
}

void Save() {
	const auto &state = State();
	auto accounts = QJsonObject();
	for (const auto &[key, set] : state.accounts) {
		auto list = QJsonArray();
		for (const auto peer : set) {
			list.append(QString::number(peer));
		}
		accounts.insert(QString::number(key), list);
	}
	auto root = QJsonObject();
	root.insert(u"salt"_q, state.salt);
	root.insert(u"hash"_q, state.hash);
	root.insert(u"accounts"_q, accounts);
	QDir().mkpath(cWorkingDir() + u"tdata"_q);
	auto file = QSaveFile(StoragePath());
	if (file.open(QIODevice::WriteOnly)) {
		file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
		file.commit();
	}
}

[[nodiscard]] QString HashPassword(const QString &salt, const QString &password) {
	auto data = (salt + u":mwtk:"_q + password).toUtf8();
	for (auto i = 0; i != 1000; ++i) {
		data = QCryptographicHash::hash(data, QCryptographicHash::Sha256);
	}
	return QString::fromLatin1(data.toHex());
}

[[nodiscard]] bool HasPassword() {
	Load();
	return !State().hash.isEmpty();
}

[[nodiscard]] bool Unlocked() {
	const auto &state = State();
	return state.unlockedAt && (crl::now() - state.unlockedAt < kUnlockDuration);
}

void RefreshInList(not_null<PeerData*> peer) {
	if (const auto history = peer->owner().historyLoaded(peer->id)) {
		history->updateChatListExistence();
	}
}

void ShowList(not_null<Window::SessionController*> controller);

[[nodiscard]] not_null<Ui::PasswordInput*> AddPasswordRow(
		not_null<Ui::GenericBox*> box,
		const QString &placeholder) {
	const auto wrap = box->addRow(object_ptr<Ui::RpWidget>(box));
	const auto field = Ui::CreateChild<Ui::PasswordInput>(
		wrap,
		st::defaultInputField,
		rpl::single(placeholder));
	wrap->resize(wrap->width(), field->height());
	wrap->widthValue(
	) | rpl::on_next([=](int width) {
		field->resize(width, field->height());
		field->move(0, 0);
	}, field->lifetime());
	return field;
}

void AskNewPassword(
		not_null<Window::SessionController*> controller,
		Fn<void()> done) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(Utf("Пароль для скрытых чатов")));
		box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			rpl::single(Utf("Скрытые чаты исчезнут из списка. Открыть их можно "
				"кнопкой «Скрытые» в боковой панели папок, в настройках MWTKgram "
				"или сочетанием Ctrl+Shift+L.")),
			st::boxDividerLabel));
		const auto first = AddPasswordRow(box, Utf("Новый пароль"));
		const auto second = AddPasswordRow(box, Utf("Повтори пароль"));
		box->setFocusCallback([=] {
			first->setFocusFast();
		});
		box->addButton(rpl::single(Utf("Сохранить")), [=] {
			const auto password = first->getLastText();
			if (password.size() < 1) {
				first->showError();
				return;
			} else if (password != second->getLastText()) {
				second->showError();
				return;
			}
			auto &state = State();
			auto salt = QByteArray(16, 0);
			for (auto &ch : salt) {
				ch = char(QRandomGenerator::global()->bounded(256));
			}
			state.salt = QString::fromLatin1(salt.toHex());
			state.hash = HashPassword(state.salt, password);
			state.unlockedAt = crl::now();
			Save();
			box->closeBox();
			if (done) {
				done();
			}
		});
		box->addButton(rpl::single(Utf("Отмена")), [=] {
			box->closeBox();
		});
	}));
}

void AskPassword(
		not_null<Window::SessionController*> controller,
		Fn<void()> done) {
	if (Unlocked()) {
		done();
		return;
	}
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(Utf("Скрытые чаты")));
		const auto field = AddPasswordRow(box, Utf("Пароль"));
		box->setFocusCallback([=] {
			field->setFocusFast();
		});
		const auto submit = [=] {
			auto &state = State();
			if (HashPassword(state.salt, field->getLastText()) != state.hash) {
				field->showError();
				return;
			}
			state.unlockedAt = crl::now();
			box->closeBox();
			done();
		};
		QObject::connect(field, &Ui::MaskedInputField::submitted, submit);
		box->addButton(rpl::single(Utf("Открыть")), submit);
		box->addButton(rpl::single(Utf("Отмена")), [=] {
			box->closeBox();
		});
	}));
}

void ShowList(not_null<Window::SessionController*> controller) {
	const auto session = &controller->session();
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(Utf("Скрытые чаты")));
		box->setWidth(st::boxWideWidth);
		const auto &set = State().accounts[SerializePeerId(
			session->userPeerId())];
		if (set.empty()) {
			box->addRow(object_ptr<Ui::FlatLabel>(
				box,
				rpl::single(Utf("Здесь пусто. Чтобы скрыть чат, нажми на него "
					"правой кнопкой → «Скрыть чат (MWTK)».")),
				st::boxDividerLabel));
		}
		for (const auto serialized : set) {
			const auto peer = session->data().peer(
				DeserializePeerId(serialized));
			const auto button = box->addRow(
				object_ptr<Ui::SettingsButton>(
					box,
					rpl::single(peer->name().isEmpty()
						? Utf("Чат")
						: peer->name()),
					st::settingsButtonNoIcon),
				style::margins());
			button->setClickedCallback([=] {
				box->closeBox();
				controller->showPeerHistory(peer);
			});
			const auto unhide = Ui::CreateChild<Ui::IconButton>(
				button,
				st::boxTitleClose);
			button->sizeValue(
			) | rpl::on_next([=](QSize size) {
				unhide->moveToRight(
					0,
					(size.height() - unhide->height()) / 2,
					size.width());
			}, unhide->lifetime());
			unhide->setClickedCallback([=] {
				crl::on_main(box, [=] {
					SetHidden(peer, false);
					box->closeBox();
					ShowList(controller);
				});
			});
		}
		box->addLeftButton(rpl::single(Utf("Сменить пароль")), [=] {
			box->closeBox();
			AskNewPassword(controller, nullptr);
		});
		box->addButton(rpl::single(Utf("Закрыть")), [=] {
			State().unlockedAt = 0;
			box->closeBox();
		});
	}));
}

} // namespace

bool IsHidden(not_null<const PeerData*> peer) {
	Load();
	const auto &accounts = State().accounts;
	if (accounts.empty()) {
		return false;
	}
	const auto i = accounts.find(AccountKey(peer));
	return (i != accounts.end())
		&& i->second.contains(SerializePeerId(peer->id));
}

void SetHidden(not_null<PeerData*> peer, bool hidden) {
	Load();
	auto &set = State().accounts[AccountKey(peer)];
	const auto id = SerializePeerId(peer->id);
	if (hidden) {
		set.insert(id);
	} else {
		set.erase(id);
	}
	Save();
	RefreshInList(peer);
}

void Toggle(
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer) {
	if (IsHidden(peer)) {
		AskPassword(controller, [=] {
			SetHidden(peer, false);
			controller->showToast(Utf("Чат снова в общем списке"));
		});
		return;
	}
	const auto hide = [=] {
		SetHidden(peer, true);
		controller->clearSectionStack();
		controller->showToast(
			Utf("Чат скрыт. Открыть скрытые: Ctrl+Shift+L"));
	};
	if (!HasPassword()) {
		AskNewPassword(controller, hide);
	} else {
		hide();
	}
}

void Open(not_null<Window::SessionController*> controller) {
	if (!HasPassword()) {
		AskNewPassword(controller, [=] { ShowList(controller); });
	} else {
		AskPassword(controller, [=] { ShowList(controller); });
	}
}

void Setup() {
	static auto installed = false;
	if (installed) {
		return;
	}
	installed = true;
	base::install_event_filter(
		QCoreApplication::instance(),
		[](not_null<QEvent*> e) {
			if (e->type() != QEvent::KeyPress) {
				return base::EventFilterResult::Continue;
			}
			const auto key = static_cast<QKeyEvent*>(e.get());
			const auto modifiers = key->modifiers()
				& (Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier);
			if (key->key() == Qt::Key_L
				&& !key->isAutoRepeat()
				&& modifiers == (Qt::ControlModifier | Qt::ShiftModifier)) {
				crl::on_main([] {
					const auto window = Core::App().activePrimaryWindow();
					if (const auto controller = window
							? window->sessionController()
							: nullptr) {
						Open(controller);
					}
				});
				return base::EventFilterResult::Cancel;
			}
			return base::EventFilterResult::Continue;
		});
}

} // namespace Mwtk::Hidden
